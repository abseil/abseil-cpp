// Copyright 2019 The Abseil Authors.
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
//
#ifndef ABSL_RANDOM_INTERNAL_UNIFORM_HELPER_H_
#define ABSL_RANDOM_INTERNAL_UNIFORM_HELPER_H_

#include <cmath>
#include <limits>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/meta/type_traits.h"
#include "absl/random/internal/traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

template <typename IntType>
class uniform_int_distribution;

template <typename RealType>
class uniform_real_distribution;

// Interval tag types which specify whether the interval is open or closed
// on either boundary.

namespace random_internal {
template <typename T>
struct TagTypeCompare {};

template <typename T>
constexpr bool operator==(TagTypeCompare<T>, TagTypeCompare<T>) {
  // Tags are mono-states. They always compare equal.
  return true;
}
template <typename T>
constexpr bool operator!=(TagTypeCompare<T>, TagTypeCompare<T>) {
  return false;
}

}  // namespace random_internal

struct IntervalClosedClosedTag
    : public random_internal::TagTypeCompare<IntervalClosedClosedTag> {};
struct IntervalClosedOpenTag
    : public random_internal::TagTypeCompare<IntervalClosedOpenTag> {};
struct IntervalOpenClosedTag
    : public random_internal::TagTypeCompare<IntervalOpenClosedTag> {};
struct IntervalOpenOpenTag
    : public random_internal::TagTypeCompare<IntervalOpenOpenTag> {};

namespace random_internal {

// In the absence of an explicitly provided return-type, the template
// "uniform_inferred_return_t<A, B>" is used to derive a suitable type, based on
// the data-types of the endpoint-arguments {A lo, B hi}.
//
// Given endpoints {A lo, B hi}, one of {A, B} will be chosen as the
// return-type, if one type can be implicitly converted into the other, in a
// lossless way. The template "is_widening_convertible" implements the
// compile-time logic for deciding if such a conversion is possible.
//
// If no such conversion between {A, B} exists, then the overload for
// absl::Uniform() will be discarded, and the call will be ill-formed.
// Return-type for absl::Uniform() when the return-type is inferred.
template <typename A, typename B>
using uniform_inferred_return_t = std::enable_if_t<
    std::disjunction_v<is_widening_convertible<A, B>,
                       is_widening_convertible<B, A>>,
    std::conditional_t<is_widening_convertible<A, B>::value, B, A>>;

// The functions
//    uniform_lower_bound(tag, a, b)
// and
//    uniform_upper_bound(tag, a, b)
// are used as implementation-details for absl::Uniform().
//
// Conceptually,
//    [a, b] == [uniform_lower_bound(IntervalClosedClosed, a, b),
//               uniform_upper_bound(IntervalClosedClosed, a, b)]
//    (a, b) == [uniform_lower_bound(IntervalOpenOpen, a, b),
//               uniform_upper_bound(IntervalOpenOpen, a, b)]
//    [a, b) == [uniform_lower_bound(IntervalClosedOpen, a, b),
//               uniform_upper_bound(IntervalClosedOpen, a, b)]
//    (a, b] == [uniform_lower_bound(IntervalOpenClosed, a, b),
//               uniform_upper_bound(IntervalOpenClosed, a, b)]
//
template <typename IntType, typename Tag>
typename std::enable_if_t<
    std::conjunction_v<
        IsIntegral<IntType>,
        std::disjunction<std::is_same<Tag, IntervalOpenClosedTag>,
                         std::is_same<Tag, IntervalOpenOpenTag>>>,
    IntType>
uniform_lower_bound(Tag, IntType a, IntType) {
  return a < (std::numeric_limits<IntType>::max)() ? (a + 1) : a;
}

template <typename FloatType, typename Tag>
typename std::enable_if_t<
    std::conjunction_v<
        std::is_floating_point<FloatType>,
        std::disjunction<std::is_same<Tag, IntervalOpenClosedTag>,
                         std::is_same<Tag, IntervalOpenOpenTag>>>,
    FloatType>
uniform_lower_bound(Tag, FloatType a, FloatType) {
  return std::isfinite(a)
             ? std::nextafter(a, std::numeric_limits<FloatType>::infinity())
             : a;
}

template <typename NumType, typename Tag>
typename std::enable_if_t<
    std::disjunction_v<std::is_same<Tag, IntervalClosedClosedTag>,
                       std::is_same<Tag, IntervalClosedOpenTag>>,
    NumType>
uniform_lower_bound(Tag, NumType a, NumType) {
  return a;
}

template <typename IntType, typename Tag>
typename std::enable_if_t<
    std::conjunction_v<
        IsIntegral<IntType>,
        std::disjunction<std::is_same<Tag, IntervalClosedOpenTag>,
                         std::is_same<Tag, IntervalOpenOpenTag>>>,
    IntType>
uniform_upper_bound(Tag, IntType, IntType b) {
  return b > (std::numeric_limits<IntType>::min)() ? (b - 1) : b;
}

template <typename FloatType, typename Tag>
typename std::enable_if_t<
    std::conjunction_v<
        std::is_floating_point<FloatType>,
        std::disjunction<std::is_same<Tag, IntervalClosedOpenTag>,
                         std::is_same<Tag, IntervalOpenOpenTag>>>,
    FloatType>
uniform_upper_bound(Tag, FloatType, FloatType b) {
  return b;
}

template <typename IntType, typename Tag>
typename std::enable_if_t<
    std::conjunction_v<
        IsIntegral<IntType>,
        std::disjunction<std::is_same<Tag, IntervalClosedClosedTag>,
                         std::is_same<Tag, IntervalOpenClosedTag>>>,
    IntType>
uniform_upper_bound(Tag, IntType, IntType b) {
  return b;
}

template <typename FloatType, typename Tag>
typename std::enable_if_t<
    std::conjunction_v<
        std::is_floating_point<FloatType>,
        std::disjunction<std::is_same<Tag, IntervalClosedClosedTag>,
                         std::is_same<Tag, IntervalOpenClosedTag>>>,
    FloatType>
uniform_upper_bound(Tag, FloatType, FloatType b) {
  return std::isfinite(b)
             ? std::nextafter(b, (std::numeric_limits<FloatType>::max)())
             : b;
}

// Returns whether the bounds are valid for the underlying distribution.
// Two-argument overloads expect inputs already resolved via uniform_*_bound.
// Three-argument overloads validate raw (tag, lo, hi) before shifting bounds.
//
// The C++ standard constraints in [rand.dist.uni.int] are:
//    requires: lo <= hi.
//
// In uniform_int_distribution, {lo, hi} are closed, closed. Thus:
//   [0, 0] is legal.
//   [0, 0) is not legal, but [0, 1) is, which translates to [0, 0].
//   (0, 1) is not legal, but (0, 2) is, which translates to [1, 1].
//   (0, 0] is not legal, but (0, 1] is, which translates to [1, 1].
//
// The C++ standard constraints in [rand.dist.uni.real] are:
//    requires: lo <= hi.
//    requires: (hi - lo) <= numeric_limits<T>::max().
//
// In uniform_real_distribution, {lo, hi} are closed, open. Thus:
//   [0, 0] is legal.
//   [0, 0) is legal (degenerates to returning 0).
//   (0, 0) is not legal, but (0, 0 + epsilon) is.
//   (0, 0] is not legal, but (0, 0 + epsilon] is.
//
template <typename FloatType>
std::enable_if_t<std::is_floating_point_v<FloatType>, bool>
IsUniformRangeFinite(FloatType lo, FloatType hi) {
  if (!std::isfinite(lo) || !std::isfinite(hi)) {
    return false;
  }
  if (lo >= 0 || hi <= 0) {
    return true;
  }
  // Compute a half-scaled difference to avoid overflow (and MSVC C4756 on
  // constant expressions) while matching std::isfinite(hi - lo) in IEEE-754
  // binary floating-point.
  constexpr FloatType kHalfMax =
      FloatType{0.5} * (std::numeric_limits<FloatType>::max)();
  return (FloatType{0.5} * hi) - (FloatType{0.5} * lo) <= kHalfMax;
}

template <typename FloatType>
std::enable_if_t<std::is_floating_point_v<FloatType>, bool>
is_uniform_range_valid(FloatType a, FloatType b) {
  return a <= b && IsUniformRangeFinite(a, b);
}

template <typename IntType>
std::enable_if_t<IsIntegral<IntType>::value, bool> is_uniform_range_valid(
    IntType a, IntType b) {
  return a <= b;
}

template <typename IntType>
std::enable_if_t<IsIntegral<IntType>::value, bool> is_uniform_range_valid(
    IntervalClosedClosedTag, IntType lo, IntType hi) {
  return lo <= hi;
}

template <typename IntType, typename Tag>
std::enable_if_t<
    std::conjunction_v<
        IsIntegral<IntType>,
        std::disjunction<std::is_same<Tag, IntervalClosedOpenTag>,
                         std::is_same<Tag, IntervalOpenClosedTag>>>,
    bool>
is_uniform_range_valid(Tag, IntType lo, IntType hi) {
  return lo < hi;
}

template <typename IntType>
std::enable_if_t<IsIntegral<IntType>::value, bool> is_uniform_range_valid(
    IntervalOpenOpenTag, IntType lo, IntType hi) {
  // Short-circuit guarantees hi > min(), preventing underflow in hi - 1.
  return lo < hi && lo < static_cast<IntType>(hi - 1);
}

template <typename FloatType, typename Tag>
std::enable_if_t<
    std::conjunction_v<
        std::is_floating_point<FloatType>,
        std::disjunction<std::is_same<Tag, IntervalClosedClosedTag>,
                         std::is_same<Tag, IntervalClosedOpenTag>>>,
    bool>
is_uniform_range_valid(Tag, FloatType lo, FloatType hi) {
  return lo <= hi && IsUniformRangeFinite(lo, hi);
}

template <typename FloatType, typename Tag>
std::enable_if_t<std::conjunction_v<
                     std::is_floating_point<FloatType>,
                     std::disjunction<std::is_same<Tag, IntervalOpenClosedTag>,
                                      std::is_same<Tag, IntervalOpenOpenTag>>>,
                 bool>
is_uniform_range_valid(Tag, FloatType lo, FloatType hi) {
  return lo < hi && IsUniformRangeFinite(lo, hi);
}

// UniformDistribution selects either absl::uniform_int_distribution
// or absl::uniform_real_distribution depending on the NumType parameter.
template <typename NumType>
using UniformDistribution =
    std::conditional_t<IsIntegral<NumType>::value,
                       absl::uniform_int_distribution<NumType>,
                       absl::uniform_real_distribution<NumType>>;

template <typename NumType, bool IsIntegralType = IsIntegral<NumType>::value>
class UniformDistributionWrapperBase;

template <typename IntType>
class UniformDistributionWrapperBase<IntType, true>
    : public absl::uniform_int_distribution<IntType> {
 public:
  using Base = absl::uniform_int_distribution<IntType>;

  template <typename TagType>
  explicit UniformDistributionWrapperBase(TagType, IntType lo, IntType hi)
      : Base(uniform_lower_bound<IntType>(TagType{}, lo, hi),
             uniform_upper_bound<IntType>(TagType{}, lo, hi)) {}

  explicit UniformDistributionWrapperBase(IntType lo, IntType hi)
      : Base(uniform_lower_bound<IntType>(IntervalClosedOpenTag(), lo, hi),
             uniform_upper_bound<IntType>(IntervalClosedOpenTag(), lo, hi)) {}

  explicit UniformDistributionWrapperBase()
      : Base(std::numeric_limits<IntType>::lowest(),
             (std::numeric_limits<IntType>::max)()) {}
};

template <typename FloatType>
class UniformDistributionWrapperBase<FloatType, false>
    : public absl::uniform_real_distribution<FloatType> {
 public:
  using Base = absl::uniform_real_distribution<FloatType>;
  using result_type = FloatType;

  template <typename TagType>
  explicit UniformDistributionWrapperBase(TagType, FloatType lo, FloatType hi)
      : Base(uniform_lower_bound<FloatType>(TagType{}, lo, hi), hi),
        closed_upper_(
            std::disjunction_v<std::is_same<TagType, IntervalClosedClosedTag>,
                               std::is_same<TagType, IntervalOpenClosedTag>>) {}

  explicit UniformDistributionWrapperBase(FloatType lo, FloatType hi)
      : Base(lo, hi), closed_upper_(false) {}

  explicit UniformDistributionWrapperBase()
      : Base(std::numeric_limits<FloatType>::lowest(),
             (std::numeric_limits<FloatType>::max)()),
        closed_upper_(false) {}

  template <typename URBG>
  result_type operator()(URBG& gen) {  // NOLINT(runtime/references)
    if (closed_upper_) {
      return Base::GenerateClosed(gen, Base::param());
    }
    return Base::operator()(gen);
  }

  template <typename URBG>
  result_type operator()(URBG& gen,  // NOLINT(runtime/references)
                         const typename Base::param_type& p) {
    if (closed_upper_) {
      return Base::GenerateClosed(gen, p);
    }
    return Base::operator()(gen, p);
  }

  result_type b() const {
    return closed_upper_ ? uniform_upper_bound<FloatType>(
                               IntervalClosedClosedTag{}, Base::a(), Base::b())
                         : Base::b();
  }

  result_type(max)() const { return b(); }

  friend bool operator==(const UniformDistributionWrapperBase& a,
                         const UniformDistributionWrapperBase& b) {
    return static_cast<const Base&>(a) == static_cast<const Base&>(b) &&
           a.closed_upper_ == b.closed_upper_;
  }

  friend bool operator!=(const UniformDistributionWrapperBase& a,
                         const UniformDistributionWrapperBase& b) {
    return !(a == b);
  }

 private:
  bool closed_upper_;
};

// UniformDistributionWrapper is used as the underlying distribution type
// by the absl::Uniform template function. It selects the proper Abseil
// uniform distribution and provides constructor overloads that match the
// expected parameter order as well as adjusting distribution bounds based
// on the tag.
template <typename NumType>
struct UniformDistributionWrapper
    : public UniformDistributionWrapperBase<NumType> {
  using Base = UniformDistributionWrapperBase<NumType>;
  using Base::Base;
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_UNIFORM_HELPER_H_
