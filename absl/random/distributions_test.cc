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

#include "absl/random/distributions.h"

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/meta/type_traits.h"
#include "absl/numeric/int128.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"

namespace {

constexpr int kSize = 400000;

class RandomDistributionsTest : public testing::Test {};

struct Invalid {};

template <typename A, typename B>
auto InferredUniformReturnT(int)
    -> decltype(absl::Uniform(std::declval<absl::InsecureBitGen&>(),
                              std::declval<A>(), std::declval<B>()));

template <typename, typename>
Invalid InferredUniformReturnT(...);

template <typename TagType, typename A, typename B>
auto InferredTaggedUniformReturnT(int)
    -> decltype(absl::Uniform(std::declval<TagType>(),
                              std::declval<absl::InsecureBitGen&>(),
                              std::declval<A>(), std::declval<B>()));

template <typename, typename, typename>
Invalid InferredTaggedUniformReturnT(...);

// Given types <A, B, Expect>, CheckArgsInferType() verifies that
//
//   absl::Uniform(gen, A{}, B{})
//
// returns the type "Expect".
//
// This interface can also be used to assert that a given absl::Uniform()
// overload does not exist / will not compile. Given types <A, B>, the
// expression
//
//   decltype(absl::Uniform(..., std::declval<A>(), std::declval<B>()))
//
// will not compile, leaving the definition of InferredUniformReturnT<A, B> to
// resolve (via SFINAE) to the overload which returns type "Invalid". This
// allows tests to assert that an invocation such as
//
//   absl::Uniform(gen, 1.23f, std::numeric_limits<int>::max() - 1)
//
// should not compile, since neither type, float nor int, can precisely
// represent both endpoint-values. Writing:
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
  static_assert(
      std::conjunction_v<
          std::is_same<Expect, decltype(InferredTaggedUniformReturnT<
                                        absl::IntervalOpenOpenTag, A, B>(0))>,
          std::is_same<Expect, decltype(InferredTaggedUniformReturnT<
                                        absl::IntervalOpenOpenTag, B, A>(0))>>);
}

template <typename A, typename B, typename ExplicitRet>
auto ExplicitUniformReturnT(int) -> decltype(absl::Uniform<ExplicitRet>(
    std::declval<absl::InsecureBitGen&>(), std::declval<A>(),
    std::declval<B>()));

template <typename, typename, typename ExplicitRet>
Invalid ExplicitUniformReturnT(...);

template <typename TagType, typename A, typename B, typename ExplicitRet>
auto ExplicitTaggedUniformReturnT(int) -> decltype(absl::Uniform<ExplicitRet>(
    std::declval<TagType>(), std::declval<absl::InsecureBitGen&>(),
    std::declval<A>(), std::declval<B>()));

template <typename, typename, typename, typename ExplicitRet>
Invalid ExplicitTaggedUniformReturnT(...);

// Given types <A, B, Expect>, CheckArgsReturnExpectedType() verifies that
//
//   absl::Uniform<Expect>(gen, A{}, B{})
//
// returns the type "Expect", and that the function-overload has the signature
//
//   Expect(URBG&, Expect, Expect)
template <typename A, typename B, typename Expect>
void CheckArgsReturnExpectedType() {
  static_assert(
      std::conjunction_v<
          std::is_same<Expect,
                       decltype(ExplicitUniformReturnT<A, B, Expect>(0))>,
          std::is_same<Expect,
                       decltype(ExplicitUniformReturnT<B, A, Expect>(0))>>);
  static_assert(
      std::conjunction_v<
          std::is_same<Expect,
                       decltype(ExplicitTaggedUniformReturnT<
                                absl::IntervalOpenOpenTag, A, B, Expect>(0))>,
          std::is_same<Expect,
                       decltype(ExplicitTaggedUniformReturnT<
                                absl::IntervalOpenOpenTag, B, A, Expect>(0))>>);
}

// Takes the type of `absl::Uniform<R>(gen)` if valid or `Invalid` otherwise.
template <typename R>
auto UniformNoBoundsReturnT(int)
    -> decltype(absl::Uniform<R>(std::declval<absl::InsecureBitGen&>()));

template <typename>
Invalid UniformNoBoundsReturnT(...);

TEST_F(RandomDistributionsTest, UniformTypeInference) {
  // Infers common types.
  CheckArgsInferType<uint16_t, uint16_t, uint16_t>();
  CheckArgsInferType<uint32_t, uint32_t, uint32_t>();
  CheckArgsInferType<uint64_t, uint64_t, uint64_t>();
  CheckArgsInferType<int16_t, int16_t, int16_t>();
  CheckArgsInferType<int32_t, int32_t, int32_t>();
  CheckArgsInferType<int64_t, int64_t, int64_t>();
  CheckArgsInferType<float, float, float>();
  CheckArgsInferType<double, double, double>();

  // Explicitly-specified return-values override inferences.
  CheckArgsReturnExpectedType<int16_t, int16_t, int32_t>();
  CheckArgsReturnExpectedType<uint16_t, uint16_t, int32_t>();
  CheckArgsReturnExpectedType<int16_t, int16_t, int64_t>();
  CheckArgsReturnExpectedType<int16_t, int32_t, int64_t>();
  CheckArgsReturnExpectedType<int16_t, int32_t, double>();
  CheckArgsReturnExpectedType<float, float, double>();
  CheckArgsReturnExpectedType<int, int, int16_t>();

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

TEST_F(RandomDistributionsTest, UniformExamples) {
  // Examples.
  absl::InsecureBitGen gen;
  EXPECT_NE(1, absl::Uniform(gen, static_cast<uint16_t>(0), 1.0f));
  EXPECT_NE(1, absl::Uniform(gen, 0, 1.0));
  EXPECT_NE(1, absl::Uniform(absl::IntervalOpenOpen, gen,
                             static_cast<uint16_t>(0), 1.0f));
  EXPECT_NE(1, absl::Uniform(absl::IntervalOpenOpen, gen, 0, 1.0));
  EXPECT_NE(1, absl::Uniform(absl::IntervalOpenOpen, gen, -1, 1.0));
  EXPECT_NE(1, absl::Uniform<double>(absl::IntervalOpenOpen, gen, -1, 1));
  EXPECT_NE(1, absl::Uniform<float>(absl::IntervalOpenOpen, gen, 0, 1));
  EXPECT_NE(1, absl::Uniform<float>(gen, 0, 1));
}

TEST_F(RandomDistributionsTest, UniformNoBounds) {
  absl::InsecureBitGen gen;

  absl::Uniform<uint8_t>(gen);
  absl::Uniform<uint16_t>(gen);
  absl::Uniform<uint32_t>(gen);
  absl::Uniform<uint64_t>(gen);
  absl::Uniform<absl::uint128>(gen);

  // Compile-time validity tests.

  // Allows unsigned ints.
  testing::StaticAssertTypeEq<uint8_t,
                              decltype(UniformNoBoundsReturnT<uint8_t>(0))>();
  testing::StaticAssertTypeEq<uint16_t,
                              decltype(UniformNoBoundsReturnT<uint16_t>(0))>();
  testing::StaticAssertTypeEq<uint32_t,
                              decltype(UniformNoBoundsReturnT<uint32_t>(0))>();
  testing::StaticAssertTypeEq<uint64_t,
                              decltype(UniformNoBoundsReturnT<uint64_t>(0))>();
  testing::StaticAssertTypeEq<
      absl::uint128, decltype(UniformNoBoundsReturnT<absl::uint128>(0))>();

  // Disallows signed ints.
  testing::StaticAssertTypeEq<Invalid,
                              decltype(UniformNoBoundsReturnT<int8_t>(0))>();
  testing::StaticAssertTypeEq<Invalid,
                              decltype(UniformNoBoundsReturnT<int16_t>(0))>();
  testing::StaticAssertTypeEq<Invalid,
                              decltype(UniformNoBoundsReturnT<int32_t>(0))>();
  testing::StaticAssertTypeEq<Invalid,
                              decltype(UniformNoBoundsReturnT<int64_t>(0))>();
  testing::StaticAssertTypeEq<
      Invalid, decltype(UniformNoBoundsReturnT<absl::int128>(0))>();

  // Disallows float types.
  testing::StaticAssertTypeEq<Invalid,
                              decltype(UniformNoBoundsReturnT<float>(0))>();
  testing::StaticAssertTypeEq<Invalid,
                              decltype(UniformNoBoundsReturnT<double>(0))>();
}

TEST_F(RandomDistributionsTest, UniformNonsenseRanges) {
  // The ranges used in this test are undefined behavior.
  // The results are arbitrary and subject to future changes.

#if (defined(__i386__) || defined(_M_IX86)) && FLT_EVAL_METHOD != 0
  // We're using an x87-compatible FPU, and intermediate operations can be
  // performed with 80-bit floats. This produces slightly different results from
  // what we expect below.
  GTEST_SKIP()
      << "Skipping the test because we detected x87 floating-point semantics";
#endif

  absl::InsecureBitGen gen;

  // <uint>
  EXPECT_EQ(0, absl::Uniform<uint64_t>(gen, 0, 0));
  EXPECT_EQ(1, absl::Uniform<uint64_t>(gen, 1, 0));
  EXPECT_EQ(0, absl::Uniform<uint64_t>(absl::IntervalOpenOpen, gen, 0, 0));
  EXPECT_EQ(1, absl::Uniform<uint64_t>(absl::IntervalOpenOpen, gen, 1, 0));

  constexpr auto m = (std::numeric_limits<uint64_t>::max)();

  EXPECT_EQ(m, absl::Uniform(gen, m, m));
  EXPECT_EQ(m, absl::Uniform(gen, m, m - 1));
  EXPECT_EQ(m - 1, absl::Uniform(gen, m - 1, m));
  EXPECT_EQ(m, absl::Uniform(absl::IntervalOpenOpen, gen, m, m));
  EXPECT_EQ(m, absl::Uniform(absl::IntervalOpenOpen, gen, m, m - 1));
  EXPECT_EQ(m - 1, absl::Uniform(absl::IntervalOpenOpen, gen, m - 1, m));

  // <int>
  EXPECT_EQ(0, absl::Uniform<int64_t>(gen, 0, 0));
  EXPECT_EQ(1, absl::Uniform<int64_t>(gen, 1, 0));
  EXPECT_EQ(0, absl::Uniform<int64_t>(absl::IntervalOpenOpen, gen, 0, 0));
  EXPECT_EQ(1, absl::Uniform<int64_t>(absl::IntervalOpenOpen, gen, 1, 0));

  constexpr auto l = (std::numeric_limits<int64_t>::min)();
  constexpr auto r = (std::numeric_limits<int64_t>::max)();

  EXPECT_EQ(l, absl::Uniform(gen, l, l));
  EXPECT_EQ(r, absl::Uniform(gen, r, r));
  EXPECT_EQ(r, absl::Uniform(gen, r, r - 1));
  EXPECT_EQ(r - 1, absl::Uniform(gen, r - 1, r));
  EXPECT_EQ(l, absl::Uniform(absl::IntervalOpenOpen, gen, l, l));
  EXPECT_EQ(r, absl::Uniform(absl::IntervalOpenOpen, gen, r, r));
  EXPECT_EQ(r, absl::Uniform(absl::IntervalOpenOpen, gen, r, r - 1));
  EXPECT_EQ(r - 1, absl::Uniform(absl::IntervalOpenOpen, gen, r - 1, r));

  // <double>
  const double e = std::nextafter(1.0, 2.0);  // 1 + epsilon
  const double f = std::nextafter(1.0, 0.0);  // 1 - epsilon
  const double g = std::numeric_limits<double>::denorm_min();

  EXPECT_EQ(1.0, absl::Uniform(gen, 1.0, e));
  EXPECT_EQ(1.0, absl::Uniform(gen, 1.0, f));
  EXPECT_EQ(0.0, absl::Uniform(gen, 0.0, g));

  EXPECT_EQ(e, absl::Uniform(absl::IntervalOpenOpen, gen, 1.0, e));
  EXPECT_EQ(1.0, absl::Uniform(absl::IntervalOpenOpen, gen, 1.0, f));
  EXPECT_EQ(g, absl::Uniform(absl::IntervalOpenOpen, gen, 0.0, g));

  constexpr double inf = std::numeric_limits<double>::infinity();
  EXPECT_EQ(0.0, absl::Uniform(absl::IntervalClosedClosed, gen, 0.0, inf));
  EXPECT_EQ(0.0, absl::Uniform(absl::IntervalOpenClosed, gen, 0.0, inf));
}

template <typename T>
struct DrawResult {
  T value;
  size_t invocations;
};

template <typename T>
void PrintTo(const DrawResult<T>& r, std::ostream* os) {
  *os << "{value=" << r.value << ", invocations=" << r.invocations << "}";
}

MATCHER_P(ReturnsWithoutDrawing, expected, "") {
  return arg.value == expected && arg.invocations == 0;
}

MATCHER(ReturnsNanWithoutDrawing, "") {
  return std::isnan(arg.value) && arg.invocations == 0;
}

template <typename Tag, typename T>
DrawResult<T> DrawTagged(Tag tag, T lo, T hi) {
  absl::random_internal::sequence_urbg urbg({0x5555555555555555ull});
  const T val = absl::Uniform<T>(tag, urbg, lo, hi);
  return {val, urbg.invocations()};
}

template <typename T>
DrawResult<T> DrawDefault(T lo, T hi) {
  absl::random_internal::sequence_urbg urbg({0x5555555555555555ull});
  const T val = absl::Uniform<T>(urbg, lo, hi);
  return {val, urbg.invocations()};
}

TEST_F(RandomDistributionsTest, UniformInvalidRangesReturnLoWithoutDrawing) {
  using ::testing::Eq;

  // Empty and inverted floating-point ranges.
  const double d_eps = std::nextafter(1.0, 2.0);
  const double d_two_ulp = std::nextafter(d_eps, 2.0);
  const double d_prev = std::nextafter(1.0, 0.0);
  constexpr double d_max = (std::numeric_limits<double>::max)();
  constexpr double d_low = std::numeric_limits<double>::lowest();
  constexpr double d_inf = std::numeric_limits<double>::infinity();
  constexpr double d_nan = std::numeric_limits<double>::quiet_NaN();
  const double d_next_low = std::nextafter(d_low, 0.0);

  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, 1.0, 1.0),
              ReturnsWithoutDrawing(1.0));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, 0.0, 0.0),
              ReturnsWithoutDrawing(0.0));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, -0.0, 0.0),
              ReturnsWithoutDrawing(-0.0));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, d_max, d_max),
              ReturnsWithoutDrawing(d_max));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenOpen, 1.0, 1.0),
              ReturnsWithoutDrawing(1.0));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenOpen, d_max, d_max),
              ReturnsWithoutDrawing(d_max));

  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, d_two_ulp, 1.0),
              ReturnsWithoutDrawing(d_two_ulp));
  EXPECT_THAT(DrawTagged(absl::IntervalClosedClosed, d_eps, 1.0),
              ReturnsWithoutDrawing(d_eps));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenOpen, 1.0, d_prev),
              ReturnsWithoutDrawing(1.0));
  EXPECT_THAT(DrawDefault(1.0, 0.0), ReturnsWithoutDrawing(1.0));

  // Non-finite and overflowing floating-point ranges.
  EXPECT_THAT(DrawTagged(absl::IntervalOpenOpen, -d_inf, 0.0),
              ReturnsWithoutDrawing(-d_inf));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, -d_inf, 0.0),
              ReturnsWithoutDrawing(-d_inf));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenOpen, -d_inf, 1.0),
              ReturnsWithoutDrawing(-d_inf));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, -d_inf, 1.0),
              ReturnsWithoutDrawing(-d_inf));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenOpen, d_inf, d_max),
              ReturnsWithoutDrawing(d_inf));
  EXPECT_THAT(DrawTagged(absl::IntervalClosedClosed, 0.0, d_inf),
              ReturnsWithoutDrawing(0.0));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, 0.0, d_inf),
              ReturnsWithoutDrawing(0.0));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, d_next_low, -d_inf),
              ReturnsWithoutDrawing(d_next_low));
  EXPECT_THAT(DrawTagged(absl::IntervalClosedClosed, -d_inf, -d_inf),
              ReturnsWithoutDrawing(-d_inf));
  EXPECT_THAT(DrawTagged(absl::IntervalClosedClosed, 0.0, d_nan),
              ReturnsWithoutDrawing(0.0));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, d_nan, 1.0),
              ReturnsNanWithoutDrawing());
  EXPECT_THAT(DrawTagged(absl::IntervalClosedClosed, d_low, d_max),
              ReturnsWithoutDrawing(d_low));
  EXPECT_THAT(DrawDefault(d_low, d_max), ReturnsWithoutDrawing(d_low));

  // Float non-finite and empty ranges.
  constexpr float f_max = (std::numeric_limits<float>::max)();
  constexpr float f_inf = std::numeric_limits<float>::infinity();
  constexpr float f_nan = std::numeric_limits<float>::quiet_NaN();
  const float f_next_low =
      std::nextafter(std::numeric_limits<float>::lowest(), 0.0f);
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, f_max, f_max),
              ReturnsWithoutDrawing(f_max));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenOpen, -f_inf, 1.0f),
              ReturnsWithoutDrawing(-f_inf));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, f_next_low, -f_inf),
              ReturnsWithoutDrawing(f_next_low));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, f_nan, 1.0f),
              ReturnsNanWithoutDrawing());

  // Empty integer boundary ranges that previously saturated into singletons.
  constexpr int32_t i32_min = (std::numeric_limits<int32_t>::min)();
  constexpr int32_t i32_max = (std::numeric_limits<int32_t>::max)();
  constexpr int64_t i64_min = (std::numeric_limits<int64_t>::min)();
  constexpr int64_t i64_max = (std::numeric_limits<int64_t>::max)();
  constexpr uint64_t u64_max = (std::numeric_limits<uint64_t>::max)();

  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, i32_max, i32_max),
              ReturnsWithoutDrawing(i32_max));
  EXPECT_THAT(DrawDefault(i32_min, i32_min), ReturnsWithoutDrawing(i32_min));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, i64_max, i64_max),
              ReturnsWithoutDrawing(i64_max));
  EXPECT_THAT(DrawDefault(i64_min, i64_min), ReturnsWithoutDrawing(i64_min));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, u64_max, u64_max),
              ReturnsWithoutDrawing(u64_max));
  EXPECT_THAT(DrawDefault<uint64_t>(0u, 0u), ReturnsWithoutDrawing(0u));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenOpen, i64_min, i64_min + 1),
              ReturnsWithoutDrawing(i64_min));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenOpen, i64_max - 1, i64_max),
              ReturnsWithoutDrawing(i64_max - 1));
  EXPECT_THAT(DrawTagged(absl::IntervalOpenClosed, absl::Int128Max(),
                         absl::Int128Max()),
              ReturnsWithoutDrawing(absl::Int128Max()));
  EXPECT_THAT(DrawDefault(absl::Int128Min(), absl::Int128Min()),
              ReturnsWithoutDrawing(absl::Int128Min()));

  // Inferred-return-type overloads also reject empty ranges cleanly.
  absl::random_internal::sequence_urbg inferred_urbg({0x1234ull});
  EXPECT_THAT(absl::Uniform(absl::IntervalOpenClosed, inferred_urbg, 1, 1.0),
              Eq(1.0));
  EXPECT_THAT(inferred_urbg.invocations(), Eq(0));
  EXPECT_THAT(absl::Uniform(inferred_urbg, int16_t{5}, int32_t{5}), Eq(5));
  EXPECT_THAT(inferred_urbg.invocations(), Eq(0));
}

TEST_F(RandomDistributionsTest, UniformFloatClosedOpenEqualReturnsLo) {
  using ::testing::Eq;

  absl::InsecureBitGen gen;
  constexpr double d_max = (std::numeric_limits<double>::max)();
  EXPECT_THAT(absl::Uniform(gen, 0.0, 0.0), Eq(0.0));
  EXPECT_THAT(absl::Uniform(gen, 1.0, 1.0), Eq(1.0));
  EXPECT_THAT(absl::Uniform(gen, d_max, d_max), Eq(d_max));
  EXPECT_THAT(absl::Uniform(absl::IntervalClosedOpen, gen, 1.0f, 1.0f),
              Eq(1.0f));
}

TEST_F(RandomDistributionsTest, UniformClosedUpperRealMaxReachability) {
  using ::testing::AllOf;
  using ::testing::AnyOf;
  using ::testing::Eq;
  using ::testing::Gt;
  using ::testing::Lt;

  constexpr double d_max = (std::numeric_limits<double>::max)();
  const double d_prev_max = std::nextafter(d_max, 0.0);
  constexpr float f_max = (std::numeric_limits<float>::max)();
  const float f_prev_max = std::nextafter(f_max, 0.0f);
  constexpr uint64_t kMaxBits = (std::numeric_limits<uint64_t>::max)();

  // Singleton [max, max] returns max.
  absl::InsecureBitGen gen;
  EXPECT_THAT(absl::Uniform(absl::IntervalClosed, gen, d_max, d_max),
              Eq(d_max));
  EXPECT_THAT(absl::Uniform(absl::IntervalClosed, gen, f_max, f_max),
              Eq(f_max));

  // Singleton (max - ulp, max] returns max 100% of the time.
  for (int i = 0; i < 1000; ++i) {
    EXPECT_THAT(absl::Uniform(absl::IntervalOpenClosed, gen, d_prev_max, d_max),
                Eq(d_max));
    EXPECT_THAT(absl::Uniform(absl::IntervalOpenClosed, gen, f_prev_max, f_max),
                Eq(f_max));
  }

  // [max - ulp, max] hits both endpoints ~50/50.
  int d_max_hits = 0;
  int f_max_hits = 0;
  constexpr int kTrials = 10000;
  for (int i = 0; i < kTrials; ++i) {
    const double d =
        absl::Uniform(absl::IntervalClosed, gen, d_prev_max, d_max);
    ASSERT_THAT(d, AnyOf(Eq(d_prev_max), Eq(d_max)));
    if (d == d_max) ++d_max_hits;

    const float f = absl::Uniform(absl::IntervalClosed, gen, f_prev_max, f_max);
    ASSERT_THAT(f, AnyOf(Eq(f_prev_max), Eq(f_max)));
    if (f == f_max) ++f_max_hits;
  }
  EXPECT_THAT(d_max_hits, AllOf(Gt(4500), Lt(5500)));
  EXPECT_THAT(f_max_hits, AllOf(Gt(4500), Lt(5500)));

  // [0, max] and (0, max] deterministically reach max on all-1 bits.
  {
    absl::random_internal::sequence_urbg hi_urbg({kMaxBits});
    EXPECT_THAT(absl::Uniform(absl::IntervalClosed, hi_urbg, 0.0, d_max),
                Eq(d_max));
    hi_urbg.reset();
    EXPECT_THAT(absl::Uniform(absl::IntervalOpenClosed, hi_urbg, 0.0, d_max),
                Eq(d_max));
    hi_urbg.reset();
    EXPECT_THAT(absl::Uniform(absl::IntervalClosed, hi_urbg, 0.0f, f_max),
                Eq(f_max));
    hi_urbg.reset();
    EXPECT_THAT(absl::Uniform(absl::IntervalOpenClosed, hi_urbg, 0.0f, f_max),
                Eq(f_max));
  }
  {
    absl::random_internal::sequence_urbg lo_urbg({0ull});
    EXPECT_THAT(absl::Uniform(absl::IntervalClosed, lo_urbg, 0.0, d_max),
                Eq(0.0));
    lo_urbg.reset();
    EXPECT_THAT(absl::Uniform(absl::IntervalOpenClosed, lo_urbg, 0.0, d_max),
                Eq(std::numeric_limits<double>::denorm_min()));
  }
}

TEST_F(RandomDistributionsTest, UniformClosedNegativeReachesTopUlps) {
  using ::testing::AllOf;
  using ::testing::Eq;
  using ::testing::Ge;
  using ::testing::Gt;
  using ::testing::Le;
  using ::testing::Lt;

  constexpr uint64_t kMaxBits = (std::numeric_limits<uint64_t>::max)();
  const double d_top_8_lo = -1.0 - 8.0 * (1.0 - std::nextafter(1.0, 0.0));
  const float f_top_8_lo = -1.0f - 8.0f * (1.0f - std::nextafter(1.0f, 0.0f));

  // Previously nextafter(-1.0, max) - (-10.0) rounded down to 9.0, making -1.0
  // and the top 8 ULPs unreachable.
  {
    absl::random_internal::sequence_urbg hi_urbg({kMaxBits});
    EXPECT_THAT(absl::Uniform(absl::IntervalClosed, hi_urbg, -10.0, -1.0),
                Eq(-1.0));
    hi_urbg.reset();
    EXPECT_THAT(absl::Uniform(absl::IntervalClosed, hi_urbg, -10.0f, -1.0f),
                Eq(-1.0f));
  }
  {
    absl::random_internal::sequence_urbg lo_urbg({0ull});
    EXPECT_THAT(absl::Uniform(absl::IntervalClosed, lo_urbg, -10.0, -1.0),
                Eq(-10.0));
    lo_urbg.reset();
    EXPECT_THAT(absl::Uniform(absl::IntervalClosed, lo_urbg, -10.0f, -1.0f),
                Eq(-10.0f));
  }
  {
    // Small offsets below kMaxBits sample inside the top 8 ULPs below -1.0.
    // rem = (~bits) << 1 = 2^8 (clz = 55) -> sample = 2^-56,
    // sample * 9 = 9 * 2^-56 = 1.125 ulp(-1.0).
    absl::random_internal::sequence_urbg near_hi_d({~uint64_t{1u << 7}});
    EXPECT_THAT(absl::Uniform(absl::IntervalClosed, near_hi_d, -10.0, -1.0),
                AllOf(Gt(d_top_8_lo), Lt(-1.0)));

    // For float, ulp(-1.0f) = 2^-24; rem = (~bits) << 1 = 2^37 (clz = 26) ->
    // sample = 2^-27, sample * 9 = 1.125 ulp(-1.0f).
    absl::random_internal::sequence_urbg near_hi_f({~(uint64_t{1} << 36)});
    EXPECT_THAT(absl::Uniform(absl::IntervalClosed, near_hi_f, -10.0f, -1.0f),
                AllOf(Gt(f_top_8_lo), Lt(-1.0f)));
  }

  // Statistical uniformity of closed negative and sign-straddling intervals.
  absl::InsecureBitGen gen;
  std::vector<double> neg_values(kSize);
  std::vector<double> straddle_values(kSize);
  for (size_t i = 0; i < neg_values.size(); ++i) {
    neg_values[i] = absl::Uniform(absl::IntervalClosed, gen, -10.0, -1.0);
    ASSERT_THAT(neg_values[i], AllOf(Ge(-10.0), Le(-1.0)));

    straddle_values[i] = absl::Uniform(absl::IntervalClosed, gen, -1.0, 1.0);
    ASSERT_THAT(straddle_values[i], AllOf(Ge(-1.0), Le(1.0)));
  }

  const auto neg_moments =
      absl::random_internal::ComputeDistributionMoments(neg_values);
  EXPECT_NEAR(-5.5, neg_moments.mean, 0.05);
  EXPECT_NEAR(81.0 / 12.0, neg_moments.variance, 0.1);
  EXPECT_NEAR(0.0, neg_moments.skewness, 0.02);
  EXPECT_NEAR(9.0 / 5.0, neg_moments.kurtosis, 0.02);

  const auto straddle_moments =
      absl::random_internal::ComputeDistributionMoments(straddle_values);
  EXPECT_NEAR(0.0, straddle_moments.mean, 0.02);
  EXPECT_NEAR(1.0 / 3.0, straddle_moments.variance, 0.02);
  EXPECT_NEAR(0.0, straddle_moments.skewness, 0.02);
  EXPECT_NEAR(9.0 / 5.0, straddle_moments.kurtosis, 0.02);

  // Asymmetric sign-straddling closed interval [-1.0f, 2^-25f] reaches 2^-25f.
  absl::random_internal::sequence_urbg asymmetric_hi({0ull, kMaxBits});
  EXPECT_THAT(
      absl::Uniform(absl::IntervalClosed, asymmetric_hi, -1.0f, 0x1p-25f),
      Eq(0x1p-25f));
}

TEST_F(RandomDistributionsTest, UniformDistributionWrapperClosedAccessors) {
  using ::testing::Eq;
  using ::testing::Gt;

  using Wrapper = absl::random_internal::UniformDistributionWrapper<double>;
  constexpr double kMax = (std::numeric_limits<double>::max)();
  constexpr uint64_t kMaxBits = (std::numeric_limits<uint64_t>::max)();

  Wrapper closed_unit(absl::IntervalClosed, 0.0, 1.0);
  Wrapper open_unit(absl::IntervalClosedOpen, 0.0, 1.0);
  EXPECT_NE(closed_unit, open_unit);
  EXPECT_THAT(closed_unit.a(), Eq(0.0));
  EXPECT_THAT(closed_unit.b(), Gt(1.0));
  EXPECT_THAT((closed_unit.max)(), Eq(closed_unit.b()));
  EXPECT_THAT(open_unit.b(), Eq(1.0));

  Wrapper closed_max(absl::IntervalClosed, 0.0, kMax);
  EXPECT_THAT(closed_max.b(), Eq(kMax));
  absl::random_internal::sequence_urbg hi_urbg({kMaxBits});
  EXPECT_THAT(closed_max(hi_urbg, closed_max.param()), Eq(kMax));
}

// TODO(lar): Validate properties of non-default interval-semantics.
TEST_F(RandomDistributionsTest, UniformReal) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Uniform(gen, 0, 1.0);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(0.5, moments.mean, 0.02);
  EXPECT_NEAR(1 / 12.0, moments.variance, 0.02);
  EXPECT_NEAR(0.0, moments.skewness, 0.02);
  EXPECT_NEAR(9 / 5.0, moments.kurtosis, 0.02);
}

TEST_F(RandomDistributionsTest, UniformInt) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    const int64_t kMax = 1000000000000ll;
    int64_t j = absl::Uniform(absl::IntervalClosedClosed, gen, 0, kMax);
    // convert to double.
    values[i] = static_cast<double>(j) / static_cast<double>(kMax);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(0.5, moments.mean, 0.02);
  EXPECT_NEAR(1 / 12.0, moments.variance, 0.02);
  EXPECT_NEAR(0.0, moments.skewness, 0.02);
  EXPECT_NEAR(9 / 5.0, moments.kurtosis, 0.02);

  /*
  // NOTE: These are not supported by absl::Uniform, which is specialized
  // on integer and real valued types.

  enum E { E0, E1 };    // enum
  enum S : int { S0, S1 };    // signed enum
  enum U : unsigned int { U0, U1 };  // unsigned enum

  absl::Uniform(gen, E0, E1);
  absl::Uniform(gen, S0, S1);
  absl::Uniform(gen, U0, U1);
  */
}

TEST_F(RandomDistributionsTest, Exponential) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Exponential<double>(gen);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(1.0, moments.mean, 0.02);
  EXPECT_NEAR(1.0, moments.variance, 0.025);
  EXPECT_NEAR(2.0, moments.skewness, 0.1);
  EXPECT_LT(5.0, moments.kurtosis);
}

TEST_F(RandomDistributionsTest, PoissonDefault) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Poisson<int64_t>(gen);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(1.0, moments.mean, 0.02);
  EXPECT_NEAR(1.0, moments.variance, 0.02);
  EXPECT_NEAR(1.0, moments.skewness, 0.025);
  EXPECT_LT(2.0, moments.kurtosis);
}

TEST_F(RandomDistributionsTest, PoissonLarge) {
  constexpr double kMean = 100000000.0;
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Poisson<int64_t>(gen, kMean);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(kMean, moments.mean, kMean * 0.015);
  EXPECT_NEAR(kMean, moments.variance, kMean * 0.015);
  EXPECT_NEAR(std::sqrt(kMean), moments.skewness, kMean * 0.02);
  EXPECT_LT(2.0, moments.kurtosis);
}

TEST_F(RandomDistributionsTest, Bernoulli) {
  constexpr double kP = 0.5151515151;
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Bernoulli(gen, kP);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(kP, moments.mean, 0.01);
}

TEST_F(RandomDistributionsTest, Beta) {
  constexpr double kAlpha = 2.0;
  constexpr double kBeta = 3.0;
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Beta(gen, kAlpha, kBeta);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(0.4, moments.mean, 0.01);
}

TEST_F(RandomDistributionsTest, Zipf) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Zipf<int64_t>(gen, 100);
  }

  // The mean of a zipf distribution is: H(N, s-1) / H(N,s).
  // Given the parameter v = 1, this gives the following function:
  // (Hn(100, 1) - Hn(1,1)) / (Hn(100,2) - Hn(1,2)) = 6.5944
  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(6.5944, moments.mean, 2000) << moments;
}

TEST_F(RandomDistributionsTest, ZipfWithZeroMax) {
  absl::InsecureBitGen gen;
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(0, absl::Zipf(gen, 0));
  }
}

TEST_F(RandomDistributionsTest, Gaussian) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::Gaussian<double>(gen);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(0.0, moments.mean, 0.02);
  EXPECT_NEAR(1.0, moments.variance, 0.04);
  EXPECT_NEAR(0, moments.skewness, 0.2);
  EXPECT_NEAR(3.0, moments.kurtosis, 0.5);
}

TEST_F(RandomDistributionsTest, LogUniform) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = absl::LogUniform<int64_t>(gen, 0, (1 << 10) - 1);
  }

  // The mean is the sum of the fractional means of the uniform distributions:
  // [0..0][1..1][2..3][4..7][8..15][16..31][32..63]
  // [64..127][128..255][256..511][512..1023]
  const double mean = (0 + 1 + 1 + 2 + 3 + 4 + 7 + 8 + 15 + 16 + 31 + 32 + 63 +
                       64 + 127 + 128 + 255 + 256 + 511 + 512 + 1023) /
                      (2.0 * 11.0);

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(mean, moments.mean, 2) << moments;
}

}  // namespace
