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

#ifndef ABSL_RANDOM_ZIPF_DISTRIBUTION_H_
#define ABSL_RANDOM_ZIPF_DISTRIBUTION_H_

#include <cassert>
#include <cmath>
#include <cstdint>
#include <ios>
#include <istream>
#include <limits>
#include <ostream>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/random/internal/fast_uniform_bits.h"
#include "absl/random/internal/generate_real.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace random_internal {
struct ZipfDistributionTestPeer;

// Returns the largest IEEE-754 double <= k, which round-trips into IntType
// without [conv.fpint] overflow or exceeding k. Masking bits below the top
// double significand bits in integer arithmetic avoids x87 excess precision
// (FLT_EVAL_METHOD == 2) rounding the result above k.
template <typename IntType>
double MaxIntegralDoubleLe(IntType k) {
  if (!(k > IntType{0})) {
    return 0.0;
  }
  if constexpr (std::numeric_limits<IntType>::digits <=
                std::numeric_limits<double>::digits) {
    return static_cast<double>(k);
  } else {
    using UnsignedT = typename MakeUnsigned<IntType>::type;
    UnsignedT uk = static_cast<UnsignedT>(k);
    const int width = BitWidth(uk);
    if (width > std::numeric_limits<double>::digits) {
      const int shift = width - std::numeric_limits<double>::digits;
      uk = (uk >> shift) << shift;
    }
    return static_cast<double>(uk);
  }
}

// ZipfParam holds the IntType-independent state and math of Algorithm ZRI
// for absl::zipf_distribution, expressed via the normalized tail integral
//
//   D(x) = \int_x^{xm} (1 + t / v)^(-q) dt,  xm = max_kd + 0.5,
//
// which scales the unnormalized PMF by v^q so f(0) = 1.0 and avoids
// cancellation as q -> 1+, overflow when v < 1, underflow when q is steep, and
// subtraction of v at large v.
//
// Only the libm-free inversion paths (q == 2 and the linear limit) are inline;
// setup, the general inversion, and the squeeze-miss evaluation are cold or
// libm-bound and live in zipf_distribution.cc.
class ZipfParam {
 public:
  // Preconditions: max_kd >= 0 is integral, q > 1, v > 0.
  ZipfParam(double max_kd, double q, double v);

  double q() const { return q_; }
  double v() const { return v_; }
  double max_kd() const { return max_kd_; }
  double d0x5() const { return d0x5_; }
  double total_hat() const { return total_hat_; }
  double s() const { return s_; }

  // Returns D(x), clamped to 0 for x >= xm.
  double TailIntegral(double x) const;

  // Returns x in [0, xm] such that D(x) == d.
  double TailIntegralInv(double d) const {
    if (d > 0.0 && d >= d0_) {
      return 0.0;
    }
    return TailIntegralInvBelowD0(d);
  }

  // TailIntegralInv() for d < D(0), which the sampling loop has already
  // established via d < d0x5() <= D(0).
  double TailIntegralInvBelowD0(double d) const {
    if (d <= 0.0) {
      return xm_;
    }
    if (quadratic_) {
      const double x = (d0_ - d) / (zm_or_cm_ + (d / v_));
      return (x < xm_) ? x : xm_;
    }
    if (zm_or_cm_ == 0.0) {
      return d0_ - d;
    }
    return TailIntegralInvGeneral(d);
  }

  // Exact ZRI acceptance test d <= D(k + 0.5) + f(k) for integral k >= 1,
  // evaluated only when the squeeze k - x <= s() misses.
  bool AcceptsHatSqueezeMiss(double k, double d) const;

 private:
  // TailIntegralInv() for 0 < d < D(0) when q != 2 outside the linear limit.
  double TailIntegralInvGeneral(double d) const;

  double Log1pRatio(double x) const;         // log(1 + x / v)
  double VExpm1(double t) const;             // v * (exp(t) - 1)
  double ScaleQm1OverV(double delta) const;  // (q - 1) / v * delta
  double PowNegativeQ(double k) const;       // f(k) = (1 + k / v)^(-q)
  double ComputeS() const;

  double q_;
  double v_;
  double max_kd_;     // largest double <= k
  double qm1_;        // q - 1
  double qm1_inv_;    // 1 / (q - 1)
  double xm_;         // max_kd_ + 0.5
  double zm_or_cm_;   // q == 2 ? v / (v + xm_) : -(q - 1) * log(1 + xm_ / v),
                      // or 0 for the linear limit D(x) = xm_ - x
  double exp_zm_;     // q != 2: expm1(zm) if zm >= -0.5 else exp(zm)
  double d0_;         // D(0)
  double d0x5_;       // D(0.5)
  double total_hat_;  // D(0.5) + f(0) == D(0.5) + 1.0
  double s_;          // ZRI squeeze threshold, guaranteed in [0, 0.5)
  bool quadratic_;    // q == 2
};

}  // namespace random_internal

// absl::zipf_distribution produces random integer-values in the range [0, k],
// distributed according to the unnormalized discrete probability function:
//
//  P(x) = (v + x) ^ -q
//
// The parameter `v` must be greater than 0 and the parameter `q` must be
// greater than 1. If either of these parameters take invalid values then the
// behavior is undefined.
//
// IntType is the result_type generated by the generator. It must be of integral
// type; a static_assert ensures this is the case.
//
// The implementation is based on W.Hormann, G.Derflinger:
//
// "Rejection-Inversion to Generate Variates from Monotone Discrete
// Distributions"
//
// http://eeyore.wu-wien.ac.at/papers/96-04-04.wh-der.ps.gz
//
template <typename IntType = int>
class zipf_distribution {
 public:
  using result_type = IntType;

  class param_type {
   public:
    using distribution_type = zipf_distribution;

    // Preconditions: k >= 0, v > 0, q > 1
    // The preconditions are validated when NDEBUG is not defined via
    // a pair of assert() directives.
    // If NDEBUG is defined and either or both of these parameters take invalid
    // values, the behavior of the class is undefined.
    explicit param_type(result_type k = (std::numeric_limits<IntType>::max)(),
                        double q = 2.0, double v = 1.0);

    result_type k() const { return k_; }
    double q() const { return zri_.q(); }
    double v() const { return zri_.v(); }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.k_ == b.k_ && a.q() == b.q() && a.v() == b.v();
    }
    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class zipf_distribution;
    friend struct random_internal::ZipfDistributionTestPeer;

    IntType k_;
    random_internal::ZipfParam zri_;

    static_assert(random_internal::IsIntegral<IntType>::value,
                  "Class-template absl::zipf_distribution<> must be "
                  "parameterized using an integral type.");
  };

  zipf_distribution()
      : zipf_distribution((std::numeric_limits<IntType>::max)()) {}

  explicit zipf_distribution(result_type k, double q = 2.0, double v = 1.0)
      : param_(k, q, v) {}

  explicit zipf_distribution(const param_type& p) : param_(p) {}

  void reset() {}

  template <typename URBG>
  result_type operator()(URBG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  template <typename URBG>
  result_type operator()(URBG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  result_type k() const { return param_.k(); }
  double q() const { return param_.q(); }
  double v() const { return param_.v(); }

  param_type param() const { return param_; }
  void param(const param_type& p) { param_ = p; }

  result_type(min)() const { return 0; }
  result_type(max)() const { return k(); }

  friend bool operator==(const zipf_distribution& a,
                         const zipf_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const zipf_distribution& a,
                         const zipf_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  param_type param_;
};

// --------------------------------------------------------------------------
// Implementation details follow
// --------------------------------------------------------------------------

template <typename IntType>
zipf_distribution<IntType>::param_type::param_type(
    typename zipf_distribution<IntType>::result_type k, double q, double v)
    : k_(k), zri_(random_internal::MaxIntegralDoubleLe(k), q, v) {
  // q and v are validated in ZipfParam::ZipfParam().
  assert(k >= 0);
}

template <typename IntType>
template <typename URBG>
typename zipf_distribution<IntType>::result_type
zipf_distribution<IntType>::operator()(
    URBG& g, const param_type& p) {  // NOLINT(runtime/references)
  const random_internal::ZipfParam& zri = p.zri_;
  using random_internal::GeneratePositiveTag;
  using random_internal::GenerateRealFromBits;
  random_internal::FastUniformBits<uint64_t> fast_u64;
  double k;
  for (;;) {
    // w ~ U[0, 1).
    const double w =
        GenerateRealFromBits<double, GeneratePositiveTag, true>(fast_u64(g));
    const double d = w * zri.total_hat();
    if (d >= zri.d0x5()) {
      return 0;
    }
    const double x = zri.TailIntegralInvBelowD0(d);
    k = std::rint(x);
    if (k < 1.0) {
      // d >= d0x5() already accounts for the full mass of k = 0; clamping to
      // k = 1 prevents double-counting k = 0 if rounding yields x <= 0.5, and
      // since k - x >= 0.5 > s() it falls through to the exact rejection test.
      k = 1.0;
    }
    if (!(k <= zri.max_kd())) {  // Also rejects NaN.
      continue;
    }
    if (k - x <= zri.s() || zri.AcceptsHatSqueezeMiss(k, d)) {
      break;
    }
  }
  // k is integral with 1 <= k <= max_kd() <= p.k_, and max_kd() converts to
  // IntType exactly (see MaxIntegralDoubleLe), so the cast neither overflows
  // nor exceeds p.k_.
  return static_cast<IntType>(k);
}

template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const zipf_distribution<IntType>& x) {
  using stream_type =
      typename random_internal::stream_format_type<IntType>::type;
  auto saver = random_internal::make_ostream_state_saver(os);
  os.precision(random_internal::stream_precision_helper<double>::kPrecision);
  os << static_cast<stream_type>(x.k()) << os.fill() << x.q() << os.fill()
     << x.v();
  return os;
}

template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    zipf_distribution<IntType>& x) {        // NOLINT(runtime/references)
  using result_type = typename zipf_distribution<IntType>::result_type;
  using param_type = typename zipf_distribution<IntType>::param_type;

  auto saver = random_internal::make_istream_state_saver(is);
  result_type k = 0;
  if (!random_internal::read_integral(is, k)) {
    return is;
  }
  const double q = random_internal::read_floating_point<double>(is);
  if (is.fail()) return is;
  const double v = random_internal::read_floating_point<double>(is);
  if (is.fail()) return is;

  if constexpr (std::numeric_limits<result_type>::is_signed) {
    if (k < result_type{0}) {
      is.setstate(std::ios_base::failbit);
      return is;
    }
  }
  if (std::isfinite(q) && q > 1.0 && std::isfinite(v) && v > 0.0) {
    x.param(param_type(k, q, v));
  } else {
    is.setstate(std::ios_base::failbit);
  }
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_ZIPF_DISTRIBUTION_H_
