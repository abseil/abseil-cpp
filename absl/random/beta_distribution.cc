// Copyright 2026 The Abseil Authors.
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

#include "absl/random/beta_distribution.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

// Fused multiply-add for the Horner series below. Use std::fma only when
// hardware FMA is available (software libm fma() adds significant call
// overhead); otherwise fall back to `a * b + c`, which differs by only a few
// ULP. The explicit call matters because some toolchains disable
// floating-point contraction, so `a * b + c` is not fused even on FMA hardware.
#if defined(FP_FAST_FMA) || defined(__FP_FAST_FMA) || defined(__FMA__) || \
    (defined(_MSC_VER) && defined(__AVX2__)) || defined(__aarch64__) ||   \
    defined(_M_ARM64)
inline double Fma(double a, double b, double c) { return std::fma(a, b, c); }
#else
inline double Fma(double a, double b, double c) { return a * b + c; }
#endif

#if defined(FP_FAST_FMAL) || defined(__FP_FAST_FMAL)
inline long double Fma(long double a, long double b, long double c) {
  return std::fma(a, b, c);
}
#else
inline long double Fma(long double a, long double b, long double c) {
  return a * b + c;
}
#endif

// The truncation bounds of the series below intentionally limit 128-bit
// `long double` to ~2e-20 relative accuracy.

// Horner evaluation of sum_{k=2}^{14} x^(k-1) / k! = (expm1(x) - x) / x for
// |x| <= 0.25. Truncation error is <= 0.25^14 / 15! < 3e-21.
template <typename T>
T Expm1MinusXOverXImpl(T x) {
  T p = T(1.14707455977297247138516979786821057e-11L);           // 1 / 14!
  p = Fma(p, x, T(1.60590438368216145993923771701549479e-10L));  // 1 / 13!
  p = Fma(p, x, T(2.08767569878680989792100903212014323e-9L));   // 1 / 12!
  p = Fma(p, x, T(2.50521083854417187750521083854417188e-8L));   // 1 / 11!
  p = Fma(p, x, T(2.75573192239858906525573192239858907e-7L));   // 1 / 10!
  p = Fma(p, x, T(2.75573192239858906525573192239858907e-6L));   // 1 / 9!
  p = Fma(p, x, T(2.48015873015873015873015873015873016e-5L));   // 1 / 8!
  p = Fma(p, x, T(1.98412698412698412698412698412698413e-4L));   // 1 / 7!
  p = Fma(p, x, T(1.38888888888888888888888888888888889e-3L));   // 1 / 6!
  p = Fma(p, x, T(8.33333333333333333333333333333333333e-3L));   // 1 / 5!
  p = Fma(p, x, T(4.16666666666666666666666666666666667e-2L));   // 1 / 4!
  p = Fma(p, x, T(1.66666666666666666666666666666666667e-1L));   // 1 / 3!
  p = Fma(p, x, T(0.5));                                         // 1 / 2!
  return x * p;
}

// Using w = u / (2 + u), log(1 + u) = 2 * atanh(w), so
//   (log1p(u) - u) / u
//     = w * (-1 + (2 * w / (2 + u)) * sum_{k=0}^{10} w^(2k) / (2k + 3)).
// For |u| <= 0.25, |w| <= 1/7, so truncation error <= (1/7)^22 / 25 < 2e-20.
template <typename T>
T Log1pMinusXOverXImpl(T u) {
  const T denom = T(2) + u;
  const T w = u / denom;
  const T w2 = w * w;
  T p = T(4.34782608695652173913043478260869565e-2L);            // 1 / 23
  p = Fma(p, w2, T(4.76190476190476190476190476190476190e-2L));  // 1 / 21
  p = Fma(p, w2, T(5.26315789473684210526315789473684211e-2L));  // 1 / 19
  p = Fma(p, w2, T(5.88235294117647058823529411764705882e-2L));  // 1 / 17
  p = Fma(p, w2, T(6.66666666666666666666666666666666667e-2L));  // 1 / 15
  p = Fma(p, w2, T(7.69230769230769230769230769230769231e-2L));  // 1 / 13
  p = Fma(p, w2, T(9.09090909090909090909090909090909091e-2L));  // 1 / 11
  p = Fma(p, w2, T(1.11111111111111111111111111111111111e-1L));  // 1 / 9
  p = Fma(p, w2, T(1.42857142857142857142857142857142857e-1L));  // 1 / 7
  p = Fma(p, w2, T(2.00000000000000000000000000000000000e-1L));  // 1 / 5
  p = Fma(p, w2, T(3.33333333333333333333333333333333333e-1L));  // 1 / 3
  return w * Fma(T(2) * w / denom, p, T(-1));
}

template <typename T>
T BetaJoehnkStepImpl(double u_in, double v_in, T alpha, T beta, T inv_alpha,
                     T inv_beta, bool try_direct_pow) {
  static constexpr T kLn2 =
      T(0.693147180559945309417232121458176568L);  // log(2)
  const T u = static_cast<T>(u_in);
  const T v = static_cast<T>(v_in);

  if (try_direct_pow) {
    const T x = std::pow(u, inv_alpha);
    const T y = std::pow(v, inv_beta);
    const T z = x + y;
    if (z > T(1)) {
      // Reject if and only if `x + y > 1.0`.
      return T(-1);
    }
    if (z < T(1) && (std::min)(x, y) >= (std::numeric_limits<T>::min)()) {
      // Only return x / z when both x and y are normal; otherwise fall through
      // to the log-transform path to avoid subnormal flush.
      return x / z;
    }
  }

  // Log transform.
  // x = log(pow(u, 1 / alpha)), y = log(pow(v, 1 / beta)).
  // Since u, v in (0, 1), x and y are negative (possibly -inf for the smaller
  // parameter, but never both -inf because param_type routes
  // max(alpha, beta) < 2^(min_exponent / 2) to DEGENERATE_SMALL).
  const T x = std::log(u) / alpha;
  const T y = std::log(v) / beta;
  const T m = (std::max)(x, y);
  // Reject iff log(e^x + e^y) > 0. Since log(e^x + e^y) <= m + log(2),
  // m <= -log(2) guarantees acceptance without computing exp/log1p.
  if (m > -kLn2 && m + std::log1p(std::exp(-std::abs(x - y))) > T(0)) {
    return T(-1);
  }
  if (x >= y) {
    return T(1) / (T(1) + std::exp(y - x));
  }
  const T e = std::exp(x - y);
  return e / (T(1) + e);
}

template <typename T>
T BetaChengStepImpl(double u1, double u2, const BetaChengState<T>& state) {
  static constexpr T kLogFour =
      T(1.3862943611198906188344642429163531361L);  // log(4)
  static constexpr T kS =
      T(2.6094379124341003746007593332261876L);  // 1 + log(5)
  static constexpr T kMaxQuarter = (std::numeric_limits<T>::max)() / T(4);

  const T logit = static_cast<T>(std::log(u1 / (1.0 - u1)));
  const T v = state.y * logit;

  // Evaluate a single transcendental per draw and derive the other. Only
  // large a (> 1e4, where |v| < ~0.5 always) needs expm1(v) itself: there
  // a * (expm1(v) - v) and the output's offset from a / (a + b) would lose
  // up to a * eps to cancellation. Otherwise exp(v) - 1 has absolute error
  // <= 1.5 * eps for |v| <= 0.5 (relative error <= 2.5 * eps beyond), which
  // perturbs s, lhs, and u by < 4e-12 and the output by a few ULP.
  const bool precise = state.a > T(1e4);
  T exp_v;
  T expm1_v;
  if (precise && std::abs(v) <= T(0.5)) {
    expm1_v = std::expm1(v);
    exp_v = T(1) + expm1_v;
  } else {
    exp_v = std::exp(v);
    if (!std::isfinite(exp_v)) {
      // exp(v) > max implies X = W / (b + W) with W = a * exp(v) > a * max:
      // -(a + b) * log1p(u) dominates a * v + logit - t except on target mass
      // < 1e-180, so rejecting introduces no measurable bias.
      return T(-1);
    }
    expm1_v = exp_v - T(1);
  }

  const T a = state.a;

  // u = (a / (a + b)) * expm1(v). Finite since a_frac <= 0.5, or, when
  // a_frac is subnormal, a < min_normal * max (an overflowed u on
  // double-double is still rejected below via log1p(u) > kMaxQuarter / b).
  T u;
  if (state.subnormal_a_frac) {
    // a_frac < min_normal implies 1 + r == 1, so a / (a + b) == a / b; avoid
    // subnormal rounding in a_frac.
    const T b = state.b;
    if (std::abs(expm1_v) > kMaxQuarter) {
      u = a * (expm1_v / b);
    } else {
      u = (a * expm1_v) / b;
    }
  } else {
    u = state.a_frac * expm1_v;
  }

  // Start the division early so that it overlaps the acceptance test.
  const T inv_1pu = T(1) / (T(1) + u);

  // Cheng's squeeze: s = a + (gamma * v - log(4)) - a * exp(v)
  //                    = logit - a * (expm1(v) - v) - log(4),
  // since gamma = a + 1 / y and v / y = logit. For large a, evaluate
  // a * (expm1(v) - v) without cancellation or v^2 underflow; otherwise the
  // direct form's absolute error (<= a * eps) is negligible, and grouping it
  // as below shortens the dependency chain after exp(v).
  T a_expm1_minus_v = T(0);
  T s;
  if (precise) {
    a_expm1_minus_v = (std::abs(v) <= T(0.25))
                          ? (a * v) * Expm1MinusXOverXImpl(v)
                          : a * (expm1_v - v);
    s = logit - a_expm1_minus_v - kLogFour;
  } else {
    s = (logit - kLogFour + a * (v + T(1))) - a * exp_v;
  }
  // u1, u2 >= 2^-65, so z >= 2^-195 >> DBL_MIN cannot underflow in double.
  const double z = u1 * u1 * u2;
  bool accepted = !state.use_algorithm_ba && s + kS >= T(5) * static_cast<T>(z);
  if (!accepted) {
    const T t = static_cast<T>(std::log(z));
    if (!state.use_algorithm_ba && s >= t) {
      accepted = true;
    } else {
      // Cheng's exact acceptance:
      //   lhs = -(a + b) * log(1 + u) + a * v + logit - log(4),
      // where u = (a / (a + b)) * expm1(v). For large a and |u| <= 0.25,
      // rewrite using (a + b) * u == a * expm1(v) to eliminate O(a * v)
      // cancellation (otherwise its absolute error a * |v| * eps is < 1e-12):
      //   -(a + b) * log(1 + u) + a * v
      //     = -(a * expm1(v)) * ((log1p(u) - u) / u) - a * (expm1(v) - v).
      // Note |u| <= 0.25 implies |a * expm1(v)| <= 0.25 * (a + b) <= 0.5 * b.
      T lhs;
      if (precise && std::abs(u) <= T(0.25)) {
        lhs = -(state.a * expm1_v) * Log1pMinusXOverXImpl(u) - a_expm1_minus_v +
              logit - kLogFour;
      } else {
        const T log1p_u = std::log1p(u);
        const T b = state.b;
        if (log1p_u > kMaxQuarter / b) {
          // Exact: (a + b) * log1p(u) - a * v >= b * log1p(u) + a * log(a_frac)
          // > max / 4 - max / 97 (as a < max / log(max)) >> logit - t.
          return T(-1);
        }
        lhs =
            -(b * log1p_u) * (T(1) + state.r) + state.a * v + logit - kLogFour;
      }
      accepted = (lhs >= t);
    }
  }
  if (!accepted) {
    return T(-1);
  }

  // Reconstruct X = W / (b + W) (or 1 - X if inverted), where W = a * exp(v),
  // as X = (a_frac + u) / (1 + u). The common paths avoid data-dependent
  // branches, which would mispredict on a large fraction of accepted draws.
  T res;
  const T b_frac = state.b_frac;
  if (state.subnormal_a_frac) {
    // a_frac < min_normal implies 1 + r == 1, so a / (a + b) == a / b; scale
    // by a / b directly to avoid subnormal rounding in a_frac.
    if (state.inverted) {
      res = b_frac * inv_1pu;
    } else if (u > T(1)) {
      res = T(1) - b_frac * inv_1pu;
    } else if (exp_v > kMaxQuarter) {
      res = (state.a * (exp_v / state.b)) * inv_1pu;
    } else {
      res = ((state.a * exp_v) / state.b) * inv_1pu;
    }
  } else {
    const T a_frac = state.a_frac;
    if (precise && std::abs(v) <= T(0.5)) {
      // X is a / (a + b) plus a tiny offset; add the offset explicitly so
      // that it is not lost to rounding.
      const T delta = (b_frac * u) * inv_1pu;
      res = state.inverted ? (b_frac - delta) : (a_frac + delta);
    } else {
      // 1 - X = b_frac / (1 + u) is accurate to 2 ULP. For u > 1 (X > 1/2),
      // X = 1 - (1 - X) is then accurate to 1 ULP of 1.
      const T one_minus_x = b_frac * inv_1pu;
      const T x = (u > T(1)) ? T(1) - one_minus_x : (a_frac * exp_v) * inv_1pu;
      res = state.inverted ? one_minus_x : x;
    }
  }
  // u > -1 (since u >= -a_frac), so every branch above is non-negative.
  assert(res >= T(0));
  return res;
}

}  // namespace

double BetaJoehnkStep(double u, double v, double alpha, double beta,
                      double inv_alpha, double inv_beta, bool try_direct_pow) {
  return BetaJoehnkStepImpl(u, v, alpha, beta, inv_alpha, inv_beta,
                            try_direct_pow);
}

long double BetaJoehnkStep(double u, double v, long double alpha,
                           long double beta, long double inv_alpha,
                           long double inv_beta, bool try_direct_pow) {
  return BetaJoehnkStepImpl(u, v, alpha, beta, inv_alpha, inv_beta,
                            try_direct_pow);
}

double BetaChengStep(double u1, double u2,
                     const BetaChengState<double>& state) {
  return BetaChengStepImpl(u1, u2, state);
}

long double BetaChengStep(double u1, double u2,
                          const BetaChengState<long double>& state) {
  return BetaChengStepImpl(u1, u2, state);
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
