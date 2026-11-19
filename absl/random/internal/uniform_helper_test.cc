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

#include "absl/random/internal/uniform_helper.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/numeric/int128.h"
#include "absl/numeric/internal/representation.h"

namespace {

using absl::IntervalClosedClosedTag;
using absl::IntervalClosedOpenTag;
using absl::IntervalOpenClosedTag;
using absl::IntervalOpenOpenTag;
using absl::random_internal::is_uniform_range_valid;
using absl::random_internal::uniform_inferred_return_t;
using absl::random_internal::uniform_lower_bound;
using absl::random_internal::uniform_upper_bound;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsFalse;
using ::testing::IsNan;
using ::testing::IsTrue;

class UniformHelperTest : public testing::Test {};

TEST_F(UniformHelperTest, UniformBoundFunctionsGeneral) {
  constexpr IntervalClosedClosedTag IntervalClosedClosed;
  constexpr IntervalClosedOpenTag IntervalClosedOpen;
  constexpr IntervalOpenClosedTag IntervalOpenClosed;
  constexpr IntervalOpenOpenTag IntervalOpenOpen;

  // absl::uniform_int_distribution natively assumes IntervalClosedClosed
  // absl::uniform_real_distribution natively assumes IntervalClosedOpen

  EXPECT_EQ(uniform_lower_bound(IntervalOpenClosed, 0, 100), 1);
  EXPECT_EQ(uniform_lower_bound(IntervalOpenOpen, 0, 100), 1);
  EXPECT_GT(uniform_lower_bound<float>(IntervalOpenClosed, 0, 1.0), 0);
  EXPECT_GT(uniform_lower_bound<float>(IntervalOpenOpen, 0, 1.0), 0);
  EXPECT_GT(uniform_lower_bound<double>(IntervalOpenClosed, 0, 1.0), 0);
  EXPECT_GT(uniform_lower_bound<double>(IntervalOpenOpen, 0, 1.0), 0);

  EXPECT_EQ(uniform_lower_bound(IntervalClosedClosed, 0, 100), 0);
  EXPECT_EQ(uniform_lower_bound(IntervalClosedOpen, 0, 100), 0);
  EXPECT_EQ(uniform_lower_bound<float>(IntervalClosedClosed, 0, 1.0), 0);
  EXPECT_EQ(uniform_lower_bound<float>(IntervalClosedOpen, 0, 1.0), 0);
  EXPECT_EQ(uniform_lower_bound<double>(IntervalClosedClosed, 0, 1.0), 0);
  EXPECT_EQ(uniform_lower_bound<double>(IntervalClosedOpen, 0, 1.0), 0);

  EXPECT_EQ(uniform_upper_bound(IntervalOpenOpen, 0, 100), 99);
  EXPECT_EQ(uniform_upper_bound(IntervalClosedOpen, 0, 100), 99);
  EXPECT_EQ(uniform_upper_bound<float>(IntervalOpenOpen, 0, 1.0), 1.0);
  EXPECT_EQ(uniform_upper_bound<float>(IntervalClosedOpen, 0, 1.0), 1.0);
  EXPECT_EQ(uniform_upper_bound<double>(IntervalOpenOpen, 0, 1.0), 1.0);
  EXPECT_EQ(uniform_upper_bound<double>(IntervalClosedOpen, 0, 1.0), 1.0);

  EXPECT_EQ(uniform_upper_bound(IntervalOpenClosed, 0, 100), 100);
  EXPECT_EQ(uniform_upper_bound(IntervalClosedClosed, 0, 100), 100);
  EXPECT_GT(uniform_upper_bound<float>(IntervalOpenClosed, 0, 1.0), 1.0);
  EXPECT_GT(uniform_upper_bound<float>(IntervalClosedClosed, 0, 1.0), 1.0);
  EXPECT_GT(uniform_upper_bound<double>(IntervalOpenClosed, 0, 1.0), 1.0);
  EXPECT_GT(uniform_upper_bound<double>(IntervalClosedClosed, 0, 1.0), 1.0);

  // Negative value tests
  EXPECT_EQ(uniform_lower_bound(IntervalOpenClosed, -100, -1), -99);
  EXPECT_EQ(uniform_lower_bound(IntervalOpenOpen, -100, -1), -99);
  EXPECT_GT(uniform_lower_bound<float>(IntervalOpenClosed, -2.0, -1.0), -2.0);
  EXPECT_GT(uniform_lower_bound<float>(IntervalOpenOpen, -2.0, -1.0), -2.0);
  EXPECT_GT(uniform_lower_bound<double>(IntervalOpenClosed, -2.0, -1.0), -2.0);
  EXPECT_GT(uniform_lower_bound<double>(IntervalOpenOpen, -2.0, -1.0), -2.0);

  EXPECT_EQ(uniform_lower_bound(IntervalClosedClosed, -100, -1), -100);
  EXPECT_EQ(uniform_lower_bound(IntervalClosedOpen, -100, -1), -100);
  EXPECT_EQ(uniform_lower_bound<float>(IntervalClosedClosed, -2.0, -1.0), -2.0);
  EXPECT_EQ(uniform_lower_bound<float>(IntervalClosedOpen, -2.0, -1.0), -2.0);
  EXPECT_EQ(uniform_lower_bound<double>(IntervalClosedClosed, -2.0, -1.0),
            -2.0);
  EXPECT_EQ(uniform_lower_bound<double>(IntervalClosedOpen, -2.0, -1.0), -2.0);

  EXPECT_EQ(uniform_upper_bound(IntervalOpenOpen, -100, -1), -2);
  EXPECT_EQ(uniform_upper_bound(IntervalClosedOpen, -100, -1), -2);
  EXPECT_EQ(uniform_upper_bound<float>(IntervalOpenOpen, -2.0, -1.0), -1.0);
  EXPECT_EQ(uniform_upper_bound<float>(IntervalClosedOpen, -2.0, -1.0), -1.0);
  EXPECT_EQ(uniform_upper_bound<double>(IntervalOpenOpen, -2.0, -1.0), -1.0);
  EXPECT_EQ(uniform_upper_bound<double>(IntervalClosedOpen, -2.0, -1.0), -1.0);

  EXPECT_EQ(uniform_upper_bound(IntervalOpenClosed, -100, -1), -1);
  EXPECT_EQ(uniform_upper_bound(IntervalClosedClosed, -100, -1), -1);
  EXPECT_GT(uniform_upper_bound<float>(IntervalOpenClosed, -2.0, -1.0), -1.0);
  EXPECT_GT(uniform_upper_bound<float>(IntervalClosedClosed, -2.0, -1.0), -1.0);
  EXPECT_GT(uniform_upper_bound<double>(IntervalOpenClosed, -2.0, -1.0), -1.0);
  EXPECT_GT(uniform_upper_bound<double>(IntervalClosedClosed, -2.0, -1.0),
            -1.0);

  EXPECT_GT(uniform_lower_bound(IntervalOpenClosed, 1.0, 2.0), 1.0);
  EXPECT_GT(uniform_lower_bound(IntervalOpenClosed, 1.0, +0.0), 1.0);
  EXPECT_GT(uniform_lower_bound(IntervalOpenClosed, 1.0, -0.0), 1.0);
  EXPECT_GT(uniform_lower_bound(IntervalOpenClosed, 1.0, -1.0), 1.0);
}

TEST_F(UniformHelperTest, UniformBoundFunctionsIntBounds) {
  // Verifies the saturating nature of uniform_lower_bound and
  // uniform_upper_bound
  constexpr IntervalOpenOpenTag IntervalOpenOpen;

  // uint max.
  constexpr auto m = (std::numeric_limits<uint64_t>::max)();

  EXPECT_EQ(1, uniform_lower_bound(IntervalOpenOpen, 0u, 0u));
  EXPECT_EQ(m, uniform_lower_bound(IntervalOpenOpen, m, m));
  EXPECT_EQ(m, uniform_lower_bound(IntervalOpenOpen, m - 1, m - 1));
  EXPECT_EQ(0, uniform_upper_bound(IntervalOpenOpen, 0u, 0u));
  EXPECT_EQ(m - 1, uniform_upper_bound(IntervalOpenOpen, m, m));

  // int min/max
  constexpr auto l = (std::numeric_limits<int64_t>::min)();
  constexpr auto r = (std::numeric_limits<int64_t>::max)();
  EXPECT_EQ(1, uniform_lower_bound(IntervalOpenOpen, 0, 0));
  EXPECT_EQ(l + 1, uniform_lower_bound(IntervalOpenOpen, l, l));
  EXPECT_EQ(r, uniform_lower_bound(IntervalOpenOpen, r - 1, r - 1));
  EXPECT_EQ(r, uniform_lower_bound(IntervalOpenOpen, r, r));
  EXPECT_EQ(-1, uniform_upper_bound(IntervalOpenOpen, 0, 0));
  EXPECT_EQ(l, uniform_upper_bound(IntervalOpenOpen, l, l));
  EXPECT_EQ(r - 1, uniform_upper_bound(IntervalOpenOpen, r, r));
}

TEST_F(UniformHelperTest, UniformBoundFunctionsRealBounds) {
  // absl::uniform_real_distribution natively assumes IntervalClosedOpen;
  // use the inverse here so each bound has to change.
  constexpr IntervalOpenClosedTag IntervalOpenClosed;
  constexpr IntervalOpenOpenTag IntervalOpenOpen;

  // Open lower bounds always step upward toward +inf regardless of b.
  const double e = std::nextafter(1.0, 2.0);  // 1 + epsilon
  const double f = std::nextafter(1.0, 0.0);  // 1 - epsilon
  EXPECT_EQ(e, uniform_lower_bound(IntervalOpenClosed, 1.0, 1.0));
  EXPECT_EQ(std::nextafter(1.0f, 2.0f),
            uniform_lower_bound(IntervalOpenClosed, 1.0f, 1.0f));

  // rightmost and leftmost finite values.
  constexpr auto r = (std::numeric_limits<double>::max)();
  const auto re = std::nexttoward(r, 0.0);
  constexpr auto l = -r;
  const auto le = std::nexttoward(l, 0.0);
  constexpr double inf = std::numeric_limits<double>::infinity();

  EXPECT_EQ(le, uniform_lower_bound(IntervalOpenClosed, l, l));    // (l,l)
  EXPECT_EQ(inf, uniform_lower_bound(IntervalOpenClosed, r, r));   // (r,r)
  EXPECT_EQ(le, uniform_lower_bound(IntervalOpenClosed, l, r));    // (l,r)
  EXPECT_EQ(le, uniform_lower_bound(IntervalOpenClosed, l, 0.0));  // (l, 0)
  EXPECT_EQ(le, uniform_lower_bound(IntervalOpenClosed, l, le));   // (l, le)
  EXPECT_EQ(r, uniform_lower_bound(IntervalOpenClosed, re, r));    // (re, r)

  EXPECT_EQ(le, uniform_upper_bound(IntervalOpenClosed, l, l));   // (l,l)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, r, r));    // (r,r)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, l, r));    // (l,r)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, l, re));   // (l,re)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, 0.0, r));  // (0, r)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, re, r));   // (re, r)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, le, re));  // (le, re)

  // (1.0, 1.0 + epsilon)
  EXPECT_EQ(e, uniform_lower_bound(IntervalOpenClosed, 1.0, e));
  EXPECT_EQ(std::nextafter(e, 2.0),
            uniform_upper_bound(IntervalOpenClosed, 1.0, e));

  // (1.0-epsilon, 1.0)
  EXPECT_EQ(1.0, uniform_lower_bound(IntervalOpenClosed, f, 1.0));
  EXPECT_EQ(e, uniform_upper_bound(IntervalOpenClosed, f, 1.0));

  // denorm cases.
  const double g = std::numeric_limits<double>::denorm_min();
  const double h = std::nextafter(g, 1.0);

  // (0, denorm_min)
  EXPECT_EQ(g, uniform_lower_bound(IntervalOpenClosed, 0.0, g));
  EXPECT_EQ(h, uniform_upper_bound(IntervalOpenClosed, 0.0, g));

  // (denorm_min, 1.0)
  EXPECT_EQ(h, uniform_lower_bound(IntervalOpenClosed, g, 1.0));
  EXPECT_EQ(e, uniform_upper_bound(IntervalOpenClosed, g, 1.0));

  // Inverted bounds still step upward rather than toward b.
  EXPECT_EQ(e, uniform_lower_bound(IntervalOpenClosed, 1.0, -1.0));

  // Non-finite bounds are preserved without stepping to finite extremes.
  EXPECT_EQ(-inf, uniform_lower_bound(IntervalOpenClosed, -inf, 1.0));
  EXPECT_EQ(-inf, uniform_lower_bound(IntervalOpenOpen, -inf, 1.0));
  EXPECT_EQ(inf, uniform_lower_bound(IntervalOpenClosed, inf, 1.0));
  EXPECT_EQ(inf, uniform_lower_bound(IntervalOpenOpen, inf, 1.0));
  EXPECT_EQ(inf, uniform_upper_bound(IntervalOpenClosed, 0.0, inf));
  EXPECT_EQ(-inf, uniform_upper_bound(IntervalOpenClosed, -inf, -inf));
}

template <typename FloatType>
class UniformHelperFloatTest : public testing::Test {};

using FloatTypes =
    std::conditional_t<absl::numeric_internal::IsDoubleDouble(),
                       ::testing::Types<float, double>,
                       ::testing::Types<float, double, long double>>;
TYPED_TEST_SUITE(UniformHelperFloatTest, FloatTypes);

TYPED_TEST(UniformHelperFloatTest, NonFiniteBoundsPreserved) {
  using T = TypeParam;
  constexpr IntervalClosedClosedTag kClosed;
  constexpr IntervalClosedOpenTag kClosedOpen;
  constexpr IntervalOpenClosedTag kOpenClosed;
  constexpr IntervalOpenOpenTag kOpen;
  constexpr T kInf = std::numeric_limits<T>::infinity();
  constexpr T kNan = std::numeric_limits<T>::quiet_NaN();
  constexpr T kMax = (std::numeric_limits<T>::max)();
  constexpr T kZero{0};
  constexpr T kOne{1};

  // Lower bounds preserve +/-inf and NaN instead of stepping to finite values.
  EXPECT_THAT(uniform_lower_bound(kOpenClosed, -kInf, kOne), Eq(-kInf));
  EXPECT_THAT(uniform_lower_bound(kOpen, -kInf, kOne), Eq(-kInf));
  EXPECT_THAT(uniform_lower_bound(kOpenClosed, kInf, kMax), Eq(kInf));
  EXPECT_THAT(uniform_lower_bound(kOpen, kInf, kMax), Eq(kInf));
  EXPECT_THAT(uniform_lower_bound(kOpenClosed, kNan, kOne), IsNan());
  EXPECT_THAT(uniform_lower_bound(kOpen, kNan, kOne), IsNan());
  EXPECT_THAT(uniform_lower_bound(kClosed, -kInf, kOne), Eq(-kInf));
  EXPECT_THAT(uniform_lower_bound(kClosedOpen, kNan, kOne), IsNan());

  // Upper bounds preserve +/-inf and NaN for closed-upper and open-upper tags.
  EXPECT_THAT(uniform_upper_bound(kClosed, kZero, kInf), Eq(kInf));
  EXPECT_THAT(uniform_upper_bound(kOpenClosed, kZero, kInf), Eq(kInf));
  EXPECT_THAT(uniform_upper_bound(kClosed, kZero, -kInf), Eq(-kInf));
  EXPECT_THAT(uniform_upper_bound(kOpenClosed, kZero, -kInf), Eq(-kInf));
  EXPECT_THAT(uniform_upper_bound(kClosed, kZero, kNan), IsNan());
  EXPECT_THAT(uniform_upper_bound(kOpenClosed, kZero, kNan), IsNan());
  EXPECT_THAT(uniform_upper_bound(kClosedOpen, kZero, kInf), Eq(kInf));
  EXPECT_THAT(uniform_upper_bound(kOpen, kZero, -kInf), Eq(-kInf));
  EXPECT_THAT(uniform_upper_bound(kOpen, kZero, kNan), IsNan());

  // Stepping finite open lower bounds always moves upward toward +inf.
  EXPECT_THAT(uniform_lower_bound(kOpenClosed, kMax, kMax), Eq(kInf));
  EXPECT_THAT(uniform_lower_bound(kOpen, kMax, kZero), Eq(kInf));
  EXPECT_THAT(uniform_lower_bound(kOpenClosed, kOne, -kOne), Gt(kOne));
  EXPECT_THAT(uniform_lower_bound(kOpen, kOne, kOne), Gt(kOne));
}

TYPED_TEST(UniformHelperFloatTest, IsUniformRangeValidFloat) {
  using T = TypeParam;
  constexpr IntervalClosedClosedTag kClosed;
  constexpr IntervalClosedOpenTag kClosedOpen;
  constexpr IntervalOpenClosedTag kOpenClosed;
  constexpr IntervalOpenOpenTag kOpen;
  constexpr T kInf = std::numeric_limits<T>::infinity();
  constexpr T kNan = std::numeric_limits<T>::quiet_NaN();
  constexpr T kMax = (std::numeric_limits<T>::max)();
  constexpr T kLowest = std::numeric_limits<T>::lowest();
  const T kNextOne = std::nextafter(T{1}, T{2});
  const T kPrevOne = std::nextafter(T{1}, T{0});
  const T kOnePlusTwoUlp = std::nextafter(kNextOne, T{2});
  const T kPrevMax = std::nextafter(kMax, T{0});
  const T kNextLowest = std::nextafter(kLowest, T{0});

  // Equal endpoints: [x, x] and [x, x) remain valid; (x, x] and (x, x) reject.
  EXPECT_THAT(is_uniform_range_valid(kClosed, T{1}, T{1}), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosedOpen, T{1}, T{1}), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, T{1}, T{1}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, T{1}, T{1}), IsFalse());

  EXPECT_THAT(is_uniform_range_valid(kClosed, T{-0.0}, T{0.0}), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosedOpen, T{-0.0}, T{0.0}), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, T{-0.0}, T{0.0}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, T{-0.0}, T{0.0}), IsFalse());

  // Adjacent endpoints and finite extremes.
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, T{1}, kNextOne), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kOpen, T{1}, kNextOne), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosed, kMax, kMax), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosedOpen, kMax, kMax), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, kMax, kMax), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, kMax, kMax), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosed, T{0}, kMax), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, kPrevMax, kMax), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosed, kLowest, T{0}), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosed, kLowest / T{2}, kMax / T{2}),
              IsTrue());

  // Inverted bounds are always rejected.
  EXPECT_THAT(is_uniform_range_valid(kClosed, kOnePlusTwoUlp, T{1}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosedOpen, kOnePlusTwoUlp, T{1}),
              IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, kOnePlusTwoUlp, T{1}),
              IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, T{1}, kPrevOne), IsFalse());

  // Non-finite and overflowing bounds are always rejected.
  EXPECT_THAT(is_uniform_range_valid(kOpen, -kInf, T{1}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, -kInf, T{1}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosed, T{0}, kInf), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, T{0}, kInf), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, kNextLowest, -kInf),
              IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, kInf, kMax), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosed, -kInf, -kInf), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosed, kInf, kInf), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, kNan, T{1}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosedOpen, T{0}, kNan), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosed, kLowest, kMax), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosedOpen, kLowest, kMax), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, kLowest, kMax), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, kLowest, kMax), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kLowest, kMax), IsFalse());

#if !((defined(__i386__) || defined(_M_IX86)) && FLT_EVAL_METHOD != 0)
  // Exact overflow boundary around max() + 0.5 * ulp(max()).
  // Skipped on 32-bit x86 with x87 excess precision where intermediate float
  // and double arithmetic retains 64-bit significand precision.
  const T kUlpMax = kMax - kPrevMax;
  const T kHalfUlpMax = kUlpMax / T{2};
  const T kBelowHalfUlpMax = std::nextafter(kHalfUlpMax, T{0});
  EXPECT_THAT(is_uniform_range_valid(kClosed, -T{1}, kMax), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosed, -kBelowHalfUlpMax, kMax),
              IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosed, -kHalfUlpMax, kMax), IsFalse());
  EXPECT_THAT(
      is_uniform_range_valid(kClosed, -(kUlpMax + kHalfUlpMax), kPrevMax),
      IsFalse());
#endif
}

template <typename IntType>
class UniformHelperIntTest : public testing::Test {};

using IntTypes = ::testing::Types<int8_t, uint8_t, int32_t, int64_t, uint64_t,
                                  absl::int128, absl::uint128>;
TYPED_TEST_SUITE(UniformHelperIntTest, IntTypes);

TYPED_TEST(UniformHelperIntTest, IsUniformRangeValidInt) {
  using T = TypeParam;
  constexpr IntervalClosedClosedTag kClosed;
  constexpr IntervalClosedOpenTag kClosedOpen;
  constexpr IntervalOpenClosedTag kOpenClosed;
  constexpr IntervalOpenOpenTag kOpen;
  constexpr T kMin = (std::numeric_limits<T>::min)();
  constexpr T kMax = (std::numeric_limits<T>::max)();

  // Empty boundary ranges that previously saturated into valid singletons.
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, kMax, kMax), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosedOpen, kMin, kMin), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, kMin, kMin), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, kMax, kMax), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, kMin, static_cast<T>(kMin + 1)),
              IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, static_cast<T>(kMax - 1), kMax),
              IsFalse());

  // Valid boundary ranges.
  EXPECT_THAT(is_uniform_range_valid(kClosed, kMin, kMin), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosed, kMax, kMax), IsTrue());
  EXPECT_THAT(
      is_uniform_range_valid(kClosedOpen, kMin, static_cast<T>(kMin + 1)),
      IsTrue());
  EXPECT_THAT(
      is_uniform_range_valid(kOpenClosed, static_cast<T>(kMax - 1), kMax),
      IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kOpen, kMin, static_cast<T>(kMin + 2)),
              IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kOpen, static_cast<T>(kMax - 2), kMax),
              IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosed, kMin, kMax), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kOpen, kMin, kMax), IsTrue());

  // Equal and inverted interior ranges.
  EXPECT_THAT(is_uniform_range_valid(kClosed, T{5}, T{5}), IsTrue());
  EXPECT_THAT(is_uniform_range_valid(kClosedOpen, T{5}, T{5}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, T{5}, T{5}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, T{5}, T{5}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosed, T{6}, T{5}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kClosedOpen, T{6}, T{5}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpenClosed, T{6}, T{5}), IsFalse());
  EXPECT_THAT(is_uniform_range_valid(kOpen, T{6}, T{5}), IsFalse());
}

struct Invalid {};

template <typename A, typename B>
auto InferredUniformReturnT(int) -> uniform_inferred_return_t<A, B>;

template <typename, typename>
Invalid InferredUniformReturnT(...);

// Given types <A, B, Expect>, CheckArgsInferType() verifies that
//
//   uniform_inferred_return_t<A, B> and
//   uniform_inferred_return_t<B, A>
//
// returns the type "Expect".
//
// This interface can also be used to assert that a given inferred return types
// are invalid. Writing:
//
//   CheckArgsInferType<float, int, Invalid>()
//
// will assert that this overload does not exist.
template <typename A, typename B, typename Expect>
void CheckArgsInferType() {
  static_assert(
      std::conjunction_v<
          std::is_same<Expect, decltype(InferredUniformReturnT<A, B>(0))>,
          std::is_same<Expect, decltype(InferredUniformReturnT<B, A>(0))>>);
}

TEST_F(UniformHelperTest, UniformTypeInference) {
  // Infers common types.
  CheckArgsInferType<uint16_t, uint16_t, uint16_t>();
  CheckArgsInferType<uint32_t, uint32_t, uint32_t>();
  CheckArgsInferType<uint64_t, uint64_t, uint64_t>();
  CheckArgsInferType<int16_t, int16_t, int16_t>();
  CheckArgsInferType<int32_t, int32_t, int32_t>();
  CheckArgsInferType<int64_t, int64_t, int64_t>();
  CheckArgsInferType<float, float, float>();
  CheckArgsInferType<double, double, double>();

  // Properly promotes uint16_t.
  CheckArgsInferType<uint16_t, uint32_t, uint32_t>();
  CheckArgsInferType<uint16_t, uint64_t, uint64_t>();
  CheckArgsInferType<uint16_t, int32_t, int32_t>();
  CheckArgsInferType<uint16_t, int64_t, int64_t>();
  CheckArgsInferType<uint16_t, float, float>();
  CheckArgsInferType<uint16_t, double, double>();

  // Properly promotes int16_t.
  CheckArgsInferType<int16_t, int32_t, int32_t>();
  CheckArgsInferType<int16_t, int64_t, int64_t>();
  CheckArgsInferType<int16_t, float, float>();
  CheckArgsInferType<int16_t, double, double>();

  // Invalid (u)int16_t-pairings do not compile.
  // See "CheckArgsInferType" comments above, for how this is achieved.
  CheckArgsInferType<uint16_t, int16_t, Invalid>();
  CheckArgsInferType<int16_t, uint32_t, Invalid>();
  CheckArgsInferType<int16_t, uint64_t, Invalid>();

  // Properly promotes uint32_t.
  CheckArgsInferType<uint32_t, uint64_t, uint64_t>();
  CheckArgsInferType<uint32_t, int64_t, int64_t>();
  CheckArgsInferType<uint32_t, double, double>();

  // Properly promotes int32_t.
  CheckArgsInferType<int32_t, int64_t, int64_t>();
  CheckArgsInferType<int32_t, double, double>();

  // Invalid (u)int32_t-pairings do not compile.
  CheckArgsInferType<uint32_t, int32_t, Invalid>();
  CheckArgsInferType<int32_t, uint64_t, Invalid>();
  CheckArgsInferType<int32_t, float, Invalid>();
  CheckArgsInferType<uint32_t, float, Invalid>();

  // Invalid (u)int64_t-pairings do not compile.
  CheckArgsInferType<uint64_t, int64_t, Invalid>();
  CheckArgsInferType<int64_t, float, Invalid>();
  CheckArgsInferType<int64_t, double, Invalid>();

  // Properly promotes float.
  CheckArgsInferType<float, double, double>();
}

}  // namespace
