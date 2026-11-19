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

#ifndef ABSL_RANDOM_POISSON_DISTRIBUTION_H_
#define ABSL_RANDOM_POISSON_DISTRIBUTION_H_

#include <cassert>
#include <cmath>
#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>

#include "absl/base/config.h"
#include "absl/random/bernoulli_distribution.h"
#include "absl/random/internal/fast_uniform_bits.h"
#include "absl/random/internal/generate_real.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// Ratio-of-uniforms computes variates in IEEE-754 `double`, which represents
// consecutive integers exactly up to 2^53 (~9e15). Cap `mean` at 1e15 so that
// variates around `mean` preserve unit integer resolution.
inline constexpr double kPoissonMaxMean = 1e15;

// Computes g(x, mean) = (x + 0.5) * ln(mean / x) + (x - mean) - R(x), where
// d = x - mean and R(x) is the Stirling log-factorial remainder. Because
// ln(P(X = x)) = g(x, mean) - 0.5 * ln(2 * pi * mean), the difference
// g(x, mean) - g(mode, mean) equals ln(P(X = x) / P(X = mode)) without
// subtracting large O(mean * ln(mean)) terms.
double PoissonLogRatioTerm(double x, double d, double mean);

// Computes ln(P(X = x) / P(X = mode)) for integer x >= 0, given d = x - mean,
// lmu = ln(mean), and g_mode = PoissonLogRatioTerm(mode, mode - mean, mean).
double PoissonLogRatio(double x, double d, double mean, double lmu,
                       double g_mode);

}  // namespace random_internal

// absl::poisson_distribution:
// Generates discrete variates conforming to a Poisson distribution.
//   p(n) = (mean^n / n!) exp(-mean)
//
// Depending on the parameter, the distribution selects one of the following
// algorithms:
// * The standard algorithm, attributed to Knuth, extended using a split method
// for larger values
// * The "Ratio of Uniforms as a convenient method for sampling from classical
// discrete distributions", Stadlober, 1989.
// http://www.sciencedirect.com/science/article/pii/0377042790903495
//
// NOTE: param_type.mean() is a double, which permits values larger than
// poisson_distribution<IntType>::max(), however this should be avoided and
// the distribution results are limited to the max() value.
//
// The goals of this implementation are to provide good performance while still
// being thread-safe: This limits the implementation to not using lgamma
// provided by <math.h>.
//
template <typename IntType = int>
class poisson_distribution {
 public:
  using result_type = IntType;

  class param_type {
   public:
    using distribution_type = poisson_distribution;
    explicit param_type(double mean = 1.0);

    double mean() const { return mean_; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.mean_ == b.mean_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class poisson_distribution;

    double mean_;
    double emu_;  // exp(-mean_) for Knuth, -expm1(-mean_) for tiny mean
    double lmu_;  // ln(mean_)
    double s_;
    double log_k_;
    int split_;  // -1: tiny mean, 0: ratio-of-uniforms, >0: Knuth splits

    static_assert(random_internal::IsIntegral<IntType>::value,
                  "Class-template absl::poisson_distribution<> must be "
                  "parameterized using an integral type.");
  };

  poisson_distribution() : poisson_distribution(1.0) {}

  explicit poisson_distribution(double mean) : param_(mean) {}

  explicit poisson_distribution(const param_type& p) : param_(p) {}

  void reset() {}

  // generating functions
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
  result_type(max)() const { return (std::numeric_limits<result_type>::max)(); }

  double mean() const { return param_.mean(); }

  friend bool operator==(const poisson_distribution& a,
                         const poisson_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const poisson_distribution& a,
                         const poisson_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  param_type param_;
  random_internal::FastUniformBits<uint64_t> fast_u64_;
};

// -----------------------------------------------------------------------------
// Implementation details follow
// -----------------------------------------------------------------------------

template <typename IntType>
poisson_distribution<IntType>::param_type::param_type(double mean)
    : mean_(mean), emu_(0.0), lmu_(0.0), s_(0.0), log_k_(0.0), split_(0) {
  assert(mean >= 0);
  assert(mean <=
         static_cast<double>((std::numeric_limits<result_type>::max)()));
  assert(mean <= random_internal::kPoissonMaxMean);
  if (mean_ < 1e-4) {
    // For tiny lambda, exp(-lambda) loses relative precision near 1.0 (and
    // rounds to >= 1 - 2^-53 for lambda < 1.665e-16). Use exact Bernoulli +
    // arrival-time thinning via -expm1(-lambda).
    split_ = -1;
    emu_ = -std::expm1(-mean_);
  } else if (mean_ < 10) {
    // For small lambda, use the knuth method.
    split_ = 1;
    emu_ = std::exp(-mean_);
  } else if (mean_ <= 50) {
    // Use split-knuth method.
    split_ = 1 + static_cast<int>(mean_ / 10.0);
    emu_ = std::exp(-mean_ / static_cast<double>(split_));
  } else {
    // Use ratio of uniforms method.
    constexpr double k2E = 0.7357588823428846;
    constexpr double kSA = 0.4494580810294493;

    lmu_ = std::log(mean_);
    double a = mean_ + 0.5;
    s_ = kSA + std::sqrt(k2E * a);
    const double mode = std::ceil(mean_) - 1.0;
    log_k_ = random_internal::PoissonLogRatioTerm(mode, mode - mean_, mean_);
  }
}

template <typename IntType>
template <typename URBG>
typename poisson_distribution<IntType>::result_type
poisson_distribution<IntType>::operator()(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  using random_internal::GeneratePositiveTag;
  using random_internal::GenerateRealFromBits;
  using random_internal::GenerateSignedTag;

  if (p.split_ < 0) {
    // Exact Poisson sampling for tiny mean (< 1e-4) via arrival-time thinning:
    // P(X >= 1) = 1 - exp(-mean) = -expm1(-mean). Conditional on X >= 1, the
    // first arrival time T1 satisfies rem = mean - T1 = log1p(V * expm1(mean))
    // for V ~ Uniform[0, 1), and (X - 1 | T1) ~ Poisson(rem).
    if (p.mean_ == 0.0) return 0;
    const result_type max_n = (max)();
    result_type n = 0;
    double rem = p.mean_;
    double prob = p.emu_;
    while (bernoulli_distribution(prob)(g)) {
      ++n;
      if (n == max_n) return max_n;
      const double v =
          GenerateRealFromBits<double, GeneratePositiveTag, true>(fast_u64_(g));
      rem = std::log1p(v * std::expm1(rem));
      if (rem == 0.0) break;
      prob = -std::expm1(-rem);
    }
    return n;
  }

  if (p.split_ > 0) {
    // Use Knuth's algorithm with range splitting to avoid floating-point
    // errors. Knuth's algorithm is: Ui is a sequence of uniform variates on
    // [0, 1); return the number of variates required for product(Ui) <=
    // exp(-lambda).
    //
    // The expected number of variates required for Knuth's method can be
    // computed as follows:
    // The expected value of U is 0.5, so solving for 0.5^n < exp(-lambda) gives
    // the expected number of uniform variates
    // required for a given lambda, which is:
    //  lambda = [2, 5,  9, 10, 11, 12, 13, 14, 15, 16, 17]
    //  n      = [3, 8, 13, 15, 16, 18, 19, 21, 22, 24, 25]
    //
    const result_type max_n = (max)();
    result_type n = 0;
    for (int split = p.split_; split > 0; --split) {
      double r = 1.0;
      for (;;) {
        r *= GenerateRealFromBits<double, GeneratePositiveTag, true>(
            fast_u64_(g));  // U[0, 1)
        if (r <= p.emu_) break;
        ++n;
        if (n == max_n) return max_n;
      }
    }
    return n;
  }

  // Use ratio of uniforms method.
  //
  // Let u ~ Uniform(0, 1), v ~ Uniform(-1, 1),
  //     a = lambda + 1/2,
  //     s = 1.5 - sqrt(3/e) + sqrt(2(lambda + 1/2)/e),
  //     x = s * v/u + a.
  // P(floor(x) = k | u^2 < f(floor(x))/k), where
  // f(m) = lambda^m exp(-lambda)/ m!, for 0 <= m, and f(m) = 0 otherwise,
  // and k = max(f).
  const double a_int = std::floor(p.mean_);
  const double mean_frac = p.mean_ - a_int;
  const double a_frac = mean_frac + 0.5;
  for (;;) {
    const double u = GenerateRealFromBits<double, GeneratePositiveTag, false>(
        fast_u64_(g));  // U(0, 1)
    const double v = GenerateRealFromBits<double, GenerateSignedTag, false>(
        fast_u64_(g));  // U(-1, 1)

    const double dx = std::floor(p.s_ * v / u + a_frac);
    const double x = a_int + dx;
    if (x < 0) continue;  // f(negative) = 0
    const double d = dx - mean_frac;
    if (2.0 * std::log(u) <
        random_internal::PoissonLogRatio(x, d, p.mean_, p.lmu_, p.log_k_)) {
      constexpr double kMax =
          static_cast<double>((std::numeric_limits<result_type>::max)());
      if constexpr (std::numeric_limits<result_type>::digits >
                    std::numeric_limits<double>::digits) {
        // For integers wider than double's significand (e.g. 64-bit, 128-bit),
        // kMax rounds up to a power of two, which is one past (max)().
        return x >= kMax ? (max)() : static_cast<result_type>(x);
      } else {
        return x > kMax ? (max)() : static_cast<result_type>(x);
      }
    }
  }
}

template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const poisson_distribution<IntType>& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  os.precision(random_internal::stream_precision_helper<double>::kPrecision);
  os << x.mean();
  return os;
}

template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    poisson_distribution<IntType>& x) {     // NOLINT(runtime/references)
  using param_type = typename poisson_distribution<IntType>::param_type;

  auto saver = random_internal::make_istream_state_saver(is);
  double mean = random_internal::read_floating_point<double>(is);
  if (!is.fail()) {
    x.param(param_type(mean));
  }
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_POISSON_DISTRIBUTION_H_
