// Copyright 2024 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_RANDOM_INTERNAL_MOCK_VALIDATORS_H_
#define ABSL_RANDOM_INTERNAL_MOCK_VALIDATORS_H_

#include <limits>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/uniform_helper.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

template <typename NumType>
class UniformDistributionValidator {
 public:
  // Handle absl::Uniform<NumType>(gen, absl::IntervalTag, lo, hi).
  template <typename TagType>
  static void Validate(NumType x, TagType tag, NumType lo, NumType hi) {
    // For invalid ranges, absl::Uniform() simply returns one of the bounds.
    if (x == lo && lo == hi) return;

    ValidateImpl(std::is_floating_point<NumType>{}, x, tag, lo, hi);
  }

  // Handle absl::Uniform<NumType>(gen, lo, hi).
  static void Validate(NumType x, NumType lo, NumType hi) {
    Validate(x, IntervalClosedOpenTag(), lo, hi);
  }

  // Handle absl::Uniform<NumType>(gen).
  static void Validate(NumType) {
    // absl::Uniform<NumType>(gen) spans the entire range of `NumType`, so any
    // value is okay. This overload exists because the validation logic attempts
    // to call it anyway rather than adding extra SFINAE.
  }

 private:
  static absl::string_view TagLbBound(IntervalClosedOpenTag) { return "["; }
  static absl::string_view TagLbBound(IntervalOpenOpenTag) { return "("; }
  static absl::string_view TagLbBound(IntervalClosedClosedTag) { return "["; }
  static absl::string_view TagLbBound(IntervalOpenClosedTag) { return "("; }
  static absl::string_view TagUbBound(IntervalClosedOpenTag) { return ")"; }
  static absl::string_view TagUbBound(IntervalOpenOpenTag) { return ")"; }
  static absl::string_view TagUbBound(IntervalClosedClosedTag) { return "]"; }
  static absl::string_view TagUbBound(IntervalOpenClosedTag) { return "]"; }

  static bool CheckLowerBound(IntervalClosedOpenTag, NumType x, NumType lo) {
    return lo <= x;
  }
  static bool CheckLowerBound(IntervalClosedClosedTag, NumType x, NumType lo) {
    return lo <= x;
  }
  static bool CheckLowerBound(IntervalOpenOpenTag, NumType x, NumType lo) {
    return lo < x;
  }
  static bool CheckLowerBound(IntervalOpenClosedTag, NumType x, NumType lo) {
    return lo < x;
  }
  static bool CheckUpperBound(IntervalClosedOpenTag, NumType x, NumType hi) {
    return x < hi;
  }
  static bool CheckUpperBound(IntervalOpenOpenTag, NumType x, NumType hi) {
    return x < hi;
  }
  static bool CheckUpperBound(IntervalClosedClosedTag, NumType x, NumType hi) {
    return x <= hi;
  }
  static bool CheckUpperBound(IntervalOpenClosedTag, NumType x, NumType hi) {
    return x <= hi;
  }

  template <typename TagType>
  static void ValidateImpl(std::true_type /* is_floating_point */, NumType x,
                           TagType tag, NumType lo, NumType hi) {
    if (lo > hi && (x == lo || x == hi)) {
      return;
    }
    // For open intervals between adjacent floats (e.g. (1, nextafter(1, 2))),
    // no representable value lies strictly inside (lo, hi); allow either bound.
    if constexpr (std::is_same_v<TagType, IntervalOpenOpenTag>) {
      if (uniform_lower_bound(tag, lo, hi) >=
              uniform_upper_bound(tag, lo, hi) &&
          (x == lo || x == hi)) {
        return;
      }
    }
    auto fmt = [](NumType v) {
      return absl::StrFormat("%.*g", std::numeric_limits<NumType>::max_digits10,
                             v);
    };
    ABSL_INTERNAL_CHECK(
        CheckLowerBound(tag, x, lo) && CheckUpperBound(tag, x, hi),
        absl::StrCat(fmt(x), " is not in ", TagLbBound(tag), fmt(lo), ", ",
                     fmt(hi), TagUbBound(tag)));
  }

  template <typename TagType>
  static void ValidateImpl(std::false_type /* is_floating_point */, NumType x,
                           TagType tag, NumType lo, NumType hi) {
    using stream_type =
        typename random_internal::stream_format_type<NumType>::type;

    const NumType lb = uniform_lower_bound(tag, lo, hi);
    const NumType ub = uniform_upper_bound(tag, lo, hi);
    if (lb > ub && (x == lo || x == hi)) {
      return;
    }
    ABSL_INTERNAL_CHECK(
        lb <= x && x <= ub,
        absl::StrCat(stream_type{x}, " is not in ", TagLbBound(tag),
                     stream_type{lo}, ", ", stream_type{hi}, TagUbBound(tag)));
  }
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_MOCK_VALIDATORS_H_
