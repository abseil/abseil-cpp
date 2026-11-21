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

#ifndef ABSL_RANDOM_BETA_DISTRIBUTION_H_
#define ABSL_RANDOM_BETA_DISTRIBUTION_H_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <ios>
#include <istream>
#include <limits>
#include <ostream>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/meta/type_traits.h"
#include "absl/random/bernoulli_distribution.h"
#include "absl/random/internal/fast_uniform_bits.h"
#include "absl/random/internal/generate_real.h"
#include "absl/random/internal/iostream_state_saver.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// Loop-invariant Cheng parameters precomputed by param_type from
// (a, b, y, method, inverted); subnormal_a_frac is a / (a + b) < min_normal.
template <typename CalcType>
struct BetaChengState {
  CalcType a;
  CalcType b;
  CalcType y;
  CalcType r;
  CalcType a_frac;
  CalcType b_frac;
  bool use_algorithm_ba;
  bool inverted;
  bool subnormal_a_frac;
};

template <typename CalcType>
inline BetaChengState<CalcType> MakeBetaChengState(CalcType a, CalcType b,
                                                   CalcType y,
                                                   bool use_algorithm_ba,
                                                   bool inverted) {
  static constexpr CalcType kMinNormal = (std::numeric_limits<CalcType>::min)();
  const CalcType r = a / b;
  const CalcType a_frac = r / (CalcType(1) + r);
  const CalcType b_frac = CalcType(1) - a_frac;
  return BetaChengState<CalcType>{a,
                                  b,
                                  y,
                                  r,
                                  a_frac,
                                  b_frac,
                                  use_algorithm_ba,
                                  inverted,
                                  a_frac < kMinNormal};
}

// Evaluates one rejection-sampling step of Joehnk's algorithm for u, v in
// (0, 1). Returns a variate in [0, 1] on accept or -1 on reject.
double BetaJoehnkStep(double u, double v, double alpha, double beta,
                      double inv_alpha, double inv_beta, bool try_direct_pow);
long double BetaJoehnkStep(double u, double v, long double alpha,
                           long double beta, long double inv_alpha,
                           long double inv_beta, bool try_direct_pow);

// Evaluates one rejection-sampling step of Cheng's BA/BB algorithm for u1, u2
// in (0, 1). Returns a variate in [0, 1] on accept or -1 on reject.
double BetaChengStep(double u1, double u2, const BetaChengState<double>& state);
long double BetaChengStep(double u1, double u2,
                          const BetaChengState<long double>& state);

}  // namespace random_internal

// absl::beta_distribution:
// Generate a floating-point variate conforming to a Beta distribution:
//   pdf(x) \propto x^(alpha-1) * (1-x)^(beta-1),
// where the params alpha and beta are positive real values; zero is also
// accepted as a degenerate limit ((0, b) -> 0, (a, 0) -> 1, and
// (0, 0) -> Bernoulli(0.5)).
//
// For positive alpha and beta, the support is the open interval (0, 1), but
// the return value might be equal to 0 or 1, due to numerical errors when
// alpha and beta are very different (or both very small).
//
// Usage note: One usage is that alpha and beta are counts of number of
// successes and failures. When the total number of trials are large, consider
// approximating a beta distribution with a Gaussian distribution with the same
// mean and variance. One could use the skewness, which depends only on the
// smaller of alpha and beta when the number of trials are sufficiently large,
// to quantify how far a beta distribution is from the normal distribution.
template <typename RealType = double>
class beta_distribution {
 public:
  using result_type = RealType;

  class param_type {
   public:
    using distribution_type = beta_distribution;

    explicit param_type(result_type alpha = result_type(1),
                        result_type beta = result_type(1))
        : alpha_(alpha), beta_(beta) {
      assert(alpha >= 0);
      assert(beta >= 0);
      assert(alpha <= (std::numeric_limits<result_type>::max)());
      assert(beta <= (std::numeric_limits<result_type>::max)());
      if (alpha == 0 || beta == 0) {
        method_ = DEGENERATE_SMALL;
        if (alpha == beta) {
          inverted_ = false;
          x_ = calc_type(0.5);
        } else {
          inverted_ = (beta < alpha);
          x_ = calc_type(0);
        }
        return;
      }
      // a = min(beta, alpha), b = max(beta, alpha).
      calc_type a;
      calc_type b;
      if (beta < alpha) {
        inverted_ = true;
        a = static_cast<calc_type>(beta);
        b = static_cast<calc_type>(alpha);
      } else {
        inverted_ = false;
        a = static_cast<calc_type>(alpha);
        b = static_cast<calc_type>(beta);
      }
      // For threshold values, see also:
      // Evaluation of Beta Generation Algorithms, Ying-Chao Hung, et. al.
      // February, 2009.
      if ((b < calc_type(1) && a + b <= calc_type(1.2)) ||
          a <= static_cast<calc_type>(ThresholdForSmallA())) {
        // Choose Joehnk over Cheng when it's faster or when Cheng encounters
        // numerical issues.
        const calc_type inv_alpha =
            calc_type(1) / static_cast<calc_type>(alpha_);
        const calc_type inv_beta = calc_type(1) / static_cast<calc_type>(beta_);
        if (b < kMinJoehnkB || std::isinf(inv_alpha) || std::isinf(inv_beta)) {
          // Either max(alpha, beta) < 2^(min_exponent / 2), so log(u) / alpha
          // and log(v) / beta could both overflow to -inf, or the smaller
          // parameter is < 1 / max, so its reciprocal overflows to +inf. In
          // both limits X lies in (min_normal, 1 - epsilon) only with
          // probability O(min(alpha, beta) * log(max)), so Beta(alpha, beta)
          // collapses to Bernoulli(alpha / (alpha + beta)) (same mean);
          // rescale by 2^-exp_b to avoid overflow and subnormal underflow.
          method_ = DEGENERATE_SMALL;
          int exp_b = 0;
          std::frexp(b, &exp_b);
          const calc_type a_s = std::scalbn(a, -exp_b);
          const calc_type b_s = std::scalbn(b, -exp_b);
          x_ = a_s / (a_s + b_s);
          return;
        }
        method_ = JOEHNK;
        joehnk_ = JoehnkState{inv_alpha, inv_beta};
        return;
      }
      // If both parameters are at or above ThresholdForLargeA() (since a <= b,
      // checking a suffices), the relative standard deviation is
      // <= 1 / sqrt(ThresholdForLargeA()) << ULP, so the variate collapses to
      // the mean a / (a + b). Rescale by b so a + b never overflows.
      if (a >= static_cast<calc_type>(ThresholdForLargeA())) {
        method_ = DEGENERATE_LARGE;
        // Note: on PPC for long double, evaluating
        // `std::numeric_limits::max() / ThresholdForLargeA` results in NaN.
        const calc_type r = a / b;
        x_ = (inverted_ ? calc_type(1) : r) / (calc_type(1) + r);
        return;
      }
      calc_type y;
      if (a <= calc_type(1)) {
        method_ = CHENG_BA;
        y = calc_type(1) / a;
      } else {
        method_ = CHENG_BB;
        // Compute y = sqrt((a + b - 2) / (2ab - a - b)) with positive terms so
        // a, b -> 1+ cannot cancel, rescaling when 2ab could overflow.
        if (b < kMaxDirectChengB) {
          const calc_type am1 = a - calc_type(1);
          const calc_type bm1 = b - calc_type(1);
          y = std::sqrt((am1 + bm1) / (am1 * b + a * bm1));
        } else {
          const calc_type fa = (a - calc_type(1)) / a;
          const calc_type fb = (b - calc_type(1)) / b;
          y = std::sqrt((fa / b + fb / a) / (fa + fb));
        }
      }
      cheng_ = random_internal::MakeBetaChengState(a, b, y, method_ == CHENG_BA,
                                                   inverted_);
    }

    result_type alpha() const { return alpha_; }
    result_type beta() const { return beta_; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.alpha_ == b.alpha_ && a.beta_ == b.beta_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class beta_distribution;

    using calc_type =
        std::conditional_t<std::is_same_v<RealType, float>, double, RealType>;

#ifdef _MSC_VER
    // MSVC does not have constexpr implementations for std::log and std::exp
    // so they are computed at runtime.
#define ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR
#else
#define ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR constexpr
#endif

    // The threshold for whether std::exp(1/a) is finite.
    // Note that this value is quite large, and a smaller a is NOT abnormal.
    static ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR result_type
    ThresholdForSmallA() {
      return result_type(1) /
             std::log((std::numeric_limits<result_type>::max)());
    }

    // The threshold for whether a * std::log(a) is finite.
    static ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR result_type
    ThresholdForLargeA() {
      return std::exp(
          std::log((std::numeric_limits<result_type>::max)()) -
          std::log(std::log((std::numeric_limits<result_type>::max)())) -
          ThresholdPadding());
    }

#undef ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR

    // Pad the threshold for large A for long double on PPC. This is done via a
    // template specialization below.
    static constexpr result_type ThresholdPadding() { return 0; }

    // Exact 2^exp for normal radix-2 exponents (std::ldexp is not constexpr
    // before C++23). Steps by 2^+-32 so large long double exponents fold fast.
    static constexpr calc_type Pow2(int exp) {
      constexpr calc_type kTwo32 = calc_type(uint64_t{1} << 32);
      constexpr calc_type kTwoM32 = calc_type(1) / kTwo32;
      calc_type x = calc_type(1);
      while (exp >= 32) {
        x *= kTwo32;
        exp -= 32;
      }
      while (exp <= -32) {
        x *= kTwoM32;
        exp += 32;
      }
      while (exp > 0) {
        x *= calc_type(2);
        --exp;
      }
      while (exp < 0) {
        x *= calc_type(0.5);
        ++exp;
      }
      return x;
    }

    static constexpr calc_type kMinJoehnkB =
        Pow2(std::numeric_limits<calc_type>::min_exponent / 2);
    static constexpr calc_type kMaxDirectChengB =
        Pow2(std::numeric_limits<calc_type>::max_exponent / 2 - 1);

    enum Method {
      JOEHNK,    // Uses algorithm Joehnk
      CHENG_BA,  // Uses algorithm BA in Cheng
      CHENG_BB,  // Uses algorithm BB in Cheng

      // Note: See also:
      //   Hung et al. Evaluation of beta generation algorithms. Communications
      //   in Statistics-Simulation and Computation 38.4 (2009): 750-770.
      // especially:
      //   Zechner, Heinz, and Ernst Stadlober. Generating beta variates via
      //   patchwork rejection. Computing 50.1 (1993): 1-18.

      DEGENERATE_SMALL,  // a is abnormally small.
      DEGENERATE_LARGE,  // a is abnormally large.
    };

    struct JoehnkState {
      calc_type inv_alpha;
      calc_type inv_beta;
    };

    result_type alpha_;
    result_type beta_;
    Method method_{};
    // Whether alpha_ != min(alpha_, beta_), i.e. true iff alpha_ > beta_.
    bool inverted_{};

    union {
      // Bernoulli p in DEGENERATE_SMALL, or mean in DEGENERATE_LARGE.
      calc_type x_{};
      // 1 / alpha_ and 1 / beta_ in JOEHNK.
      JoehnkState joehnk_;
      // Loop-invariant Cheng state (including Cheng's "beta" as cheng_.y) in
      // CHENG_BA and CHENG_BB, precomputed once so operator() performs no
      // divisions before sampling.
      random_internal::BetaChengState<calc_type> cheng_;
    };

    static_assert(std::is_floating_point_v<RealType>,
                  "Class-template absl::beta_distribution<> must be "
                  "parameterized using a floating-point type.");
  };

  beta_distribution() : beta_distribution(1) {}

  explicit beta_distribution(result_type alpha, result_type beta = 1)
      : param_(alpha, beta) {}

  explicit beta_distribution(const param_type& p) : param_(p) {}

  void reset() {}

  // Generating functions
  template <typename URBG>
  result_type operator()(URBG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  template <typename URBG>
  result_type operator()(URBG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  param_type param() const { return param_; }
  void param(const param_type& p) { param_ = p; }

  result_type(min)() const { return 0; }
  result_type(max)() const { return 1; }

  result_type alpha() const { return param_.alpha(); }
  result_type beta() const { return param_.beta(); }

  friend bool operator==(const beta_distribution& a,
                         const beta_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const beta_distribution& a,
                         const beta_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  using calc_type = typename param_type::calc_type;

  template <typename URBG>
  result_type AlgorithmJoehnk(URBG& g,  // NOLINT(runtime/references)
                              const param_type& p);

  template <typename URBG>
  result_type AlgorithmCheng(URBG& g,  // NOLINT(runtime/references)
                             const param_type& p);

  template <typename URBG>
  result_type DegenerateCase(URBG& g,  // NOLINT(runtime/references)
                             const param_type& p) {
    if (p.method_ == param_type::DEGENERATE_SMALL) {
      const double prob = static_cast<double>(p.x_);
      if (prob == 0.0) {
        return p.inverted_ ? result_type(1) : result_type(0);
      }
      const bool hit = absl::bernoulli_distribution(prob)(g);
      return (hit != p.inverted_) ? result_type(1) : result_type(0);
    }
    return static_cast<result_type>(p.x_);
  }

  param_type param_;
  random_internal::FastUniformBits<uint64_t> fast_u64_;
};

#if defined(__powerpc64__) || defined(__PPC64__) || defined(__powerpc__) || \
    defined(__ppc__) || defined(__PPC__)
// PPC needs a more stringent boundary for long double.
template <>
constexpr long double
beta_distribution<long double>::param_type::ThresholdPadding() {
  return 10;
}
#endif

template <typename RealType>
template <typename URBG>
typename beta_distribution<RealType>::result_type
beta_distribution<RealType>::AlgorithmJoehnk(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  using random_internal::GeneratePositiveTag;
  using random_internal::GenerateRealFromBits;

  // Based on Joehnk, M. D. Erzeugung von betaverteilten und gammaverteilten
  // Zufallszahlen. Metrika 8.1 (1964): 5-15.
  // This method is described in Knuth, Vol 2 (Third Edition), pp 134.
  // For exponents above 500, pow(u, 1 / alpha) is subnormal for u < 0.24
  // (i.e., on > 24% of draws), which falls back to log space anyway.
  static constexpr calc_type kMaxDirectPowExponent = calc_type(500);
  const bool try_direct_pow = !std::is_same_v<float, result_type> &&
                              p.joehnk_.inv_alpha >= calc_type(1) &&
                              p.joehnk_.inv_beta >= calc_type(1) &&
                              p.joehnk_.inv_alpha <= kMaxDirectPowExponent &&
                              p.joehnk_.inv_beta <= kMaxDirectPowExponent;

  for (;;) {
    const double u =
        GenerateRealFromBits<double, GeneratePositiveTag, false>(fast_u64_(g));
    const double v =
        GenerateRealFromBits<double, GeneratePositiveTag, false>(fast_u64_(g));
    const calc_type res = random_internal::BetaJoehnkStep(
        u, v, static_cast<calc_type>(p.alpha_), static_cast<calc_type>(p.beta_),
        p.joehnk_.inv_alpha, p.joehnk_.inv_beta, try_direct_pow);
    if (res >= calc_type(0)) {
      return static_cast<result_type>(res);
    }
  }
}

template <typename RealType>
template <typename URBG>
typename beta_distribution<RealType>::result_type
beta_distribution<RealType>::AlgorithmCheng(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  using random_internal::GeneratePositiveTag;
  using random_internal::GenerateRealFromBits;

  // Based on Cheng, Russell CH. Generating beta variates with nonintegral
  // shape parameters. Communications of the ACM 21.4 (1978): 317-322.
  // (https://dl.acm.org/citation.cfm?id=359482).
  for (;;) {
    const double u1 =
        GenerateRealFromBits<double, GeneratePositiveTag, false>(fast_u64_(g));
    const double u2 =
        GenerateRealFromBits<double, GeneratePositiveTag, false>(fast_u64_(g));
    const calc_type res = random_internal::BetaChengStep(u1, u2, p.cheng_);
    if (res >= calc_type(0)) {
      return (std::min)(static_cast<result_type>(res), result_type(1));
    }
  }
}

template <typename RealType>
template <typename URBG>
typename beta_distribution<RealType>::result_type
beta_distribution<RealType>::operator()(URBG& g,  // NOLINT(runtime/references)
                                        const param_type& p) {
  switch (p.method_) {
    case param_type::JOEHNK:
      return AlgorithmJoehnk(g, p);
    case param_type::CHENG_BA:
    case param_type::CHENG_BB:
      return AlgorithmCheng(g, p);
    default:
      return DegenerateCase(g, p);
  }
}

template <typename CharT, typename Traits, typename RealType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const beta_distribution<RealType>& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  os.precision(random_internal::stream_precision_helper<RealType>::kPrecision);
  os << x.alpha() << os.fill() << x.beta();
  return os;
}

template <typename CharT, typename Traits, typename RealType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    beta_distribution<RealType>& x) {       // NOLINT(runtime/references)
  using result_type = typename beta_distribution<RealType>::result_type;
  using param_type = typename beta_distribution<RealType>::param_type;

  auto saver = random_internal::make_istream_state_saver(is);
  const result_type alpha =
      random_internal::read_floating_point<result_type>(is);
  if (is.fail()) return is;
  const result_type beta =
      random_internal::read_floating_point<result_type>(is);
  if (!is.fail()) {
    constexpr result_type kMax = (std::numeric_limits<result_type>::max)();
    if (alpha >= result_type(0) && beta >= result_type(0) && alpha <= kMax &&
        beta <= kMax) {
      x.param(param_type(alpha, beta));
    } else {
      is.setstate(std::ios_base::failbit);
    }
  }
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_BETA_DISTRIBUTION_H_
