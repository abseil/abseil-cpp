// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/random/zipf_distribution.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

namespace {

// Returns v / (v + x) for v > 0 and 0 <= x <= 1e300. The direct form costs a
// single division; dividing through by v is only needed where v + x could
// overflow.
double RatioVOverVPlusX(double v, double x) {
  return (v <= 1e300) ? v / (v + x) : 1.0 / (1.0 + (x / v));
}

// Returns log(exp(a) + exp(b)) without overflow or underflow of exp().
double LogAddExp(double a, double b) {
  return (std::max)(a, b) + std::log1p(std::exp(-std::abs(a - b)));
}

}  // namespace

ZipfParam::ZipfParam(double max_kd, double q, double v)
    : q_(q),
      v_(v),
      max_kd_(max_kd),
      qm1_(q - 1.0),
      qm1_inv_(0.0),
      xm_(max_kd + 0.5),
      quadratic_(q == 2.0) {
  assert(max_kd >= 0.0);
  assert(std::isfinite(q) && q > 1.0);
  assert(std::isfinite(v) && v > 0.0);
  qm1_inv_ = 1.0 / qm1_;
  if (quadratic_) {
    // D(x) = (xm - x) * (v / (v + x)) * cm with cm = v / (v + xm).
    exp_zm_ = 0.0;
    zm_or_cm_ = RatioVOverVPlusX(v_, xm_);
    // D(0) = v * xm / (v + xm) is symmetric in (v, xm). Scaling the smaller
    // of the two by the ratio of the larger, which lies in [0.5, 1], keeps
    // every intermediate away from overflow and subnormals.
    const double lo = (std::min)(v_, xm_);
    const double hi = (std::max)(v_, xm_);
    d0_ = lo * RatioVOverVPlusX(hi, lo);
  } else {
    const double log_xm = Log1pRatio(xm_);
    if (q_ * log_xm <= 1e-40) {
      // Across [0, xm_], (1 + t / v)^(-q) == 1.0 to > 130 bits. Use the exact
      // linear limit D(x) = xm_ - x to avoid subnormal underflow in zm or
      // (q - 1) / v * delta when q -> 1+ and v is near DBL_MAX.
      zm_or_cm_ = 0.0;
      exp_zm_ = 0.0;
      d0_ = xm_;
    } else {
      const double zm = -qm1_ * log_xm;
      zm_or_cm_ = zm;
      double neg_expm1_zm;
      if (zm >= -0.5) {
        exp_zm_ = std::expm1(zm);
        neg_expm1_zm = -exp_zm_;
      } else {
        exp_zm_ = (zm > -746.0) ? std::exp(zm) : 0.0;
        neg_expm1_zm = 1.0 - exp_zm_;
      }
      d0_ = v_ * (neg_expm1_zm * qm1_inv_);
    }
  }

  d0x5_ = (std::min)(TailIntegral(0.5), d0_);
  total_hat_ = d0x5_ + 1.0;

  if (max_kd_ == 0.0) {
    s_ = 0.0;
  } else if (v_ == 1.0 && quadratic_) {
    s_ = 0.46153846153846123;
  } else {
    s_ = ComputeS();
  }
}

double ZipfParam::Log1pRatio(double x) const {
  const double ratio = x / v_;
  if (std::isfinite(ratio)) {
    return std::log1p(ratio);
  }
  // When x / v_ overflows double, 1 + x / v_ == x / v_ to full precision.
  return std::log(x) - std::log(v_);
}

double ZipfParam::VExpm1(double t) const {
  if (t <= 709.0) {
    return v_ * std::expm1(t);
  }
  // When t > 709, exp(t) - 1 == exp(t) to full precision; combine logs to
  // avoid premature overflow when v_ is very small.
  return std::exp(std::log(v_) + t);
}

double ZipfParam::ScaleQm1OverV(double delta) const {
  // Callers pass 0 < delta <= 0.5 * d0_ <= 0.5 * v_ / qm1_, so qm1_ * delta
  // cannot overflow when v_ >= 1.
  return (v_ < 1.0) ? (delta / v_) * qm1_ : (qm1_ * delta) / v_;
}

double ZipfParam::TailIntegral(double x) const {
  if (x >= xm_) {
    return 0.0;
  }
  if (quadratic_) {
    return ((xm_ - x) * RatioVOverVPlusX(v_, x)) * zm_or_cm_;
  }
  if (zm_or_cm_ == 0.0) {
    return xm_ - x;
  }
  const double zx = -qm1_ * Log1pRatio(x);
  if (zx < -746.0) {
    return 0.0;
  }
  const double exp_zx = std::exp(zx);
  const double neg_expm1_diff = -std::expm1(zm_or_cm_ - zx);
  // 1 - exp(zm - zx) <= min(1, zx - zm) and log1p(xm / v) <= xm / v bound
  // v * (neg_expm1_diff / (q - 1)) by xm_, so the inner product can not
  // overflow for any v > 0. Scaling by exp_zx <= 1 last (rather than forming
  // v * exp_zx first) keeps it from underflowing into subnormals before the
  // final product does.
  return exp_zx * (v_ * (neg_expm1_diff * qm1_inv_));
}

double ZipfParam::TailIntegralInvGeneral(double d) const {
  double z;
  if (d >= 0.5 * d0_) {
    // When d is in the upper half of [0, D(0)], d0_ - d is exact by Sterbenz's
    // lemma and u = (q - 1) / v * (D(0) - d) <= 0.5, avoiding catastrophic
    // cancellation of exp(zm_) + b near 1.0 when x << v << xm_.
    const double u = ScaleQm1OverV(d0_ - d);
    assert(u < 1.0);
    z = std::log1p(-u);
  } else {
    const double b = ScaleQm1OverV(d);
    if (zm_or_cm_ >= -0.5) {
      const double s = exp_zm_ + b;
      if (s <= -1.0) return xm_;
      if (s >= 0.0) return 0.0;
      z = std::log1p(s);
    } else if (zm_or_cm_ >= -708.0) {
      const double val = exp_zm_ + b;
      if (val >= 1.0) return 0.0;
      z = std::log(val);
    } else if (b == 0.0) {
      return xm_;
    } else {
      // exp(zm_) underflows; evaluate log(exp(zm_) + b) in log space.
      z = LogAddExp(zm_or_cm_, std::log(b));
    }
  }
  return (std::min)(VExpm1(-z * qm1_inv_), xm_);
}

double ZipfParam::ComputeS() const {
  // Solve \int_{1 - s}^{1.5} (v + x)^(-q) dx = (v + 1)^(-q) directly on
  // [1 - s, 1.5] with y = v + 1, independent of xm_. With r = 1 / y and
  // q1 = q - 1 > 0:
  //   (1 - s * r)^(-q1) = (1 + 0.5 * r)^(-q1) + q1 * r.
  // Because s(k) increases with k >= 1 and 0 < s(1) < 0.5, stepping down below
  // transcendental rounding error and clamping to [0, nextafter(0.5, 0)) gives
  // a sound squeeze threshold for all k >= 1 and ensures k - x == 0 <= s_ for
  // all x >= 2^52 (where every double is an integer, and k + 0.5 == k for
  // k >= 2^53).
  const double y = v_ + 1.0;
  double s;
  if (quadratic_) {
    const double r = 1.0 / y;
    s = (0.5 * (1.0 + r)) / (1.0 + r * (1.0 + 0.5 * r));
  } else if (q_ / y <= 1e-8) {
    // For q / y <= 1e-8, 0.5 - s_exact < 0.085 * (q / y)^2 < 1e-17 < 2^-54,
    // avoiding subnormal underflow of qm1_ / y when v_ is huge and q_ -> 1+.
    s = std::nextafter(0.5, 0.0);
  } else {
    const double qr = qm1_ / y;
    if (qr >= 1e300) {
      // (1 + 0.5 / y)^(-qm1_) <= 1 is negligible relative to qr >= 1e300.
      const double log_lhs_over_qm1 = (std::log(qm1_) - std::log(y)) / qm1_;
      s = -y * std::expm1(-log_lhs_over_qm1);
    } else if (y >= 1e100) {
      // 0.5 / y <= 5e-101 so log1p(0.5 / y) == 0.5 / y and
      // log1p(a) / qm1_ < 1e-89, avoiding subnormal underflow at huge v_.
      const double a = std::expm1(-0.5 * qr) + qr;
      s = std::log1p(a) / qr;
    } else {
      const double a = std::expm1(-qm1_ * std::log1p(0.5 / y)) + qr;
      const double log_lhs_over_qm1 = std::log1p(a) / qm1_;
      s = -y * std::expm1(-log_lhs_over_qm1);
    }
  }

  if (!(std::isfinite(s) && s > 0.0)) return 0.0;
  constexpr double kSafetyFactor =
      1.0 - 8.0 * std::numeric_limits<double>::epsilon();
  return (std::min)(std::nextafter(s * kSafetyFactor, 0.0),
                    std::nextafter(0.5, 0.0));
}

double ZipfParam::PowNegativeQ(double k) const {
  assert(k >= 1.0);
  if (quadratic_) {
    const double r = RatioVOverVPlusX(v_, k);
    return r * r;
  }
  const double arg = -q_ * Log1pRatio(k);
  return (arg > -746.0) ? std::exp(arg) : 0.0;
}

bool ZipfParam::AcceptsHatSqueezeMiss(double k, double d) const {
  return d <= TailIntegral(k + 0.5) + PowNegativeQ(k);
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
