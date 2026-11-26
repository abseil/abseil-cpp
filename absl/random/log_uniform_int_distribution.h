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

#ifndef ABSL_RANDOM_LOG_UNIFORM_INT_DISTRIBUTION_H_
#define ABSL_RANDOM_LOG_UNIFORM_INT_DISTRIBUTION_H_

#include <algorithm>
#include <ios>
#include <istream>
#include <limits>
#include <ostream>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/traits.h"
#include "absl/random/uniform_int_distribution.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// log_uniform_int_distribution:
//
// Returns a random variate R in range [min, max] such that
// floor(log(R - min, base)) is uniformly distributed.
// Uniformity is ensured by partitioning [min, max] into buckets starting at
// min, min + 1, min + base, min + base^2, ... (up to max), choosing a bucket
// uniformly, and then sampling uniformly within it.
//
template <typename IntType = int>
class log_uniform_int_distribution {
 private:
  using unsigned_type =
      typename random_internal::make_unsigned_bits<IntType>::type;

 public:
  using result_type = IntType;

  class param_type {
   public:
    using distribution_type = log_uniform_int_distribution;

    explicit param_type(
        result_type min = 0,
        result_type max = (std::numeric_limits<result_type>::max)(),
        result_type base = 2)
        : min_(min),
          max_(max),
          base_(base),
          range_(static_cast<unsigned_type>(max_) -
                 static_cast<unsigned_type>(min_)),
          log_range_(0) {
      ABSL_HARDENING_ASSERT(max_ >= min_);
      ABSL_HARDENING_ASSERT(base_ > 1);

      if (base_ == 2) {
        // Determine where the first set bit is on range(), giving a log2(range)
        // value which can be used to construct bounds.
        log_range_ = (std::min)(random_internal::BitWidth(range()),
                                std::numeric_limits<unsigned_type>::digits);
      } else if (base_ > 2 && range() > 0) {
        // An out-of-contract base_ (<= 1) skips this branch entirely to avoid
        // division by 0 and guarantee termination.
        const unsigned_type ubase = static_cast<unsigned_type>(base_);
        const unsigned_type limit = range() / ubase;
        unsigned_type pow = 1;
        int log_range = 1;
        while (pow <= limit) {
          pow *= ubase;
          ++log_range;
        }
        log_range_ = log_range;
      }
    }

    result_type(min)() const { return min_; }
    result_type(max)() const { return max_; }
    result_type base() const { return base_; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.min_ == b.min_ && a.max_ == b.max_ && a.base_ == b.base_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class log_uniform_int_distribution;

    int log_range() const { return log_range_; }
    unsigned_type range() const { return range_; }

    result_type min_;
    result_type max_;
    result_type base_;
    unsigned_type range_;  // max - min
    int log_range_;        // range_ != 0 ? floor(log(range_, base_)) + 1 : 0

    static_assert(random_internal::IsIntegral<IntType>::value,
                  "Class-template absl::log_uniform_int_distribution<> must be "
                  "parameterized using an integral type.");
  };

  log_uniform_int_distribution() : log_uniform_int_distribution(0) {}

  explicit log_uniform_int_distribution(
      result_type min,
      result_type max = (std::numeric_limits<result_type>::max)(),
      result_type base = 2)
      : param_(min, max, base) {}

  explicit log_uniform_int_distribution(const param_type& p) : param_(p) {}

  void reset() {}

  // generating functions
  template <typename URBG>
  result_type operator()(URBG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  template <typename URBG>
  result_type operator()(URBG& g,  // NOLINT(runtime/references)
                         const param_type& p) {
    return static_cast<result_type>(static_cast<unsigned_type>((p.min)()) +
                                    Generate(g, p));
  }

  result_type(min)() const { return (param_.min)(); }
  result_type(max)() const { return (param_.max)(); }
  result_type base() const { return param_.base(); }

  param_type param() const { return param_; }
  void param(const param_type& p) { param_ = p; }

  friend bool operator==(const log_uniform_int_distribution& a,
                         const log_uniform_int_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const log_uniform_int_distribution& a,
                         const log_uniform_int_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  // Returns a log-uniform variate in the range [0, p.range()]. The caller
  // should add min() to shift the result to the correct range.
  template <typename URNG>
  unsigned_type Generate(URNG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  param_type param_;
};

template <typename IntType>
template <typename URBG>
typename log_uniform_int_distribution<IntType>::unsigned_type
log_uniform_int_distribution<IntType>::Generate(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  // sample e over [0, log_range]. Map the results of e to this:
  // 0 => 0
  // 1 => [1, base-1]
  // 2 => [base, (base^2)-1]
  // n => [base^(n-1)..(base^n)-1]
  const int e = absl::uniform_int_distribution<int>(0, p.log_range())(g);
  if (e == 0) {
    return 0;
  }
  const int d = e - 1;

  // param_type establishes base^(log_range - 1) <= range < base^log_range
  // whenever log_range > 0. Thus base^d <= range for every e in [1, log_range],
  // and base^e <= range (so base^e fits in unsigned_type) if e < log_range.
  const unsigned_type ubase = static_cast<unsigned_type>(p.base());
  unsigned_type lo;
  if (ubase == 2) {
    lo = static_cast<unsigned_type>(unsigned_type{1} << d);
  } else {
    lo = 1;
    unsigned_type factor = ubase;
    for (int exp = d; exp > 0; exp >>= 1) {
      if ((exp & 1) != 0) {
        lo *= factor;
      }
      // Skip the unused final squaring: for uint16_t, squaring a large factor
      // (e.g. 50000) promotes to signed int and overflows.
      if (exp > 1) {
        factor *= factor;
      }
    }
  }
  const unsigned_type hi = (e == p.log_range())
                               ? p.range()
                               : static_cast<unsigned_type>((lo * ubase) - 1);

  // choose uniformly over [lo, hi]
  return absl::uniform_int_distribution<unsigned_type>(lo, hi)(g);
}

template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const log_uniform_int_distribution<IntType>& x) {
  using stream_type =
      typename random_internal::stream_format_type<IntType>::type;
  auto saver = random_internal::make_ostream_state_saver(os);
  os << static_cast<stream_type>((x.min)()) << os.fill()
     << static_cast<stream_type>((x.max)()) << os.fill()
     << static_cast<stream_type>(x.base());
  return os;
}

template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,       // NOLINT(runtime/references)
    log_uniform_int_distribution<IntType>& x) {  // NOLINT(runtime/references)
  using param_type = typename log_uniform_int_distribution<IntType>::param_type;
  using result_type =
      typename log_uniform_int_distribution<IntType>::result_type;

  auto saver = random_internal::make_istream_state_saver(is);
  result_type min_val = 0;
  result_type max_val = 0;
  result_type base_val = 0;
  if (!random_internal::read_integral(is, min_val) ||
      !random_internal::read_integral(is, max_val) ||
      !random_internal::read_integral(is, base_val)) {
    return is;
  }
  if (max_val < min_val || base_val <= 1) {
    // The input violates the param_type preconditions; signal failure by
    // setting the failbit instead of constructing an invalid param_type.
    is.setstate(std::ios_base::failbit);
  } else {
    x.param(param_type(min_val, max_val, base_val));
  }
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_LOG_UNIFORM_INT_DISTRIBUTION_H_
