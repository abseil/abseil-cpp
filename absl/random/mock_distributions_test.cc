// Copyright 2018 The Abseil Authors.
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

#include "absl/random/mock_distributions.h"

#include <cmath>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "absl/numeric/int128.h"
#include "absl/random/distributions.h"
#include "absl/random/internal/mock_validators.h"
#include "absl/random/mocking_bit_gen.h"
#include "absl/random/random.h"

namespace {
using ::testing::Return;

TEST(MockDistributions, Examples) {
  absl::MockingBitGen gen;

  EXPECT_NE(absl::Uniform<int>(gen, 1, 1000000), 20);
  EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 1000000))
      .WillOnce(Return(20));
  EXPECT_EQ(absl::Uniform<int>(gen, 1, 1000000), 20);

  EXPECT_NE(absl::Uniform<double>(gen, 0.0, 100.0), 5.0);
  EXPECT_CALL(absl::MockUniform<double>(), Call(gen, 0.0, 100.0))
      .WillOnce(Return(5.0));
  EXPECT_EQ(absl::Uniform<double>(gen, 0.0, 100.0), 5.0);

  EXPECT_NE(absl::Exponential<double>(gen, 1.0), 42);
  EXPECT_CALL(absl::MockExponential<double>(), Call(gen, 1.0))
      .WillOnce(Return(42));
  EXPECT_EQ(absl::Exponential<double>(gen, 1.0), 42);

  EXPECT_NE(absl::Poisson<int>(gen, 1.0), 500);
  EXPECT_CALL(absl::MockPoisson<int>(), Call(gen, 1.0)).WillOnce(Return(500));
  EXPECT_EQ(absl::Poisson<int>(gen, 1.0), 500);

  EXPECT_NE(absl::Bernoulli(gen, 0.000001), true);
  EXPECT_CALL(absl::MockBernoulli(), Call(gen, 0.000001))
      .WillOnce(Return(true));
  EXPECT_EQ(absl::Bernoulli(gen, 0.000001), true);

  EXPECT_NE(absl::Beta<double>(gen, 3.0, 2.0), 0.567);
  EXPECT_CALL(absl::MockBeta<double>(), Call(gen, 3.0, 2.0))
      .WillOnce(Return(0.567));
  EXPECT_EQ(absl::Beta<double>(gen, 3.0, 2.0), 0.567);

  EXPECT_NE(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);
  EXPECT_CALL(absl::MockZipf<int>(), Call(gen, 1000000, 2.0, 1.0))
      .WillOnce(Return(1221));
  EXPECT_EQ(absl::Zipf<int>(gen, 1000000, 2.0, 1.0), 1221);

  EXPECT_NE(absl::Gaussian<double>(gen, 0.0, 1.0), 0.001);
  EXPECT_CALL(absl::MockGaussian<double>(), Call(gen, 0.0, 1.0))
      .WillOnce(Return(0.001));
  EXPECT_EQ(absl::Gaussian<double>(gen, 0.0, 1.0), 0.001);

  const int kHigh = (1 << 30) - 1;
  EXPECT_NE(absl::LogUniform<int>(gen, 0, kHigh, 2), kHigh);
  EXPECT_CALL(absl::MockLogUniform<int>(), Call(gen, 0, kHigh, 2))
      .WillOnce(Return(kHigh));
  EXPECT_EQ(absl::LogUniform<int>(gen, 0, kHigh, 2), kHigh);
}

TEST(MockDistributions, UniformUInt128BoundariesAreAllowed) {
  absl::MockingBitGen gen;

  EXPECT_CALL(absl::MockUniform<absl::uint128>(), Call(gen))
      .WillOnce(Return(absl::Uint128Max()));
  EXPECT_EQ(absl::Uniform<absl::uint128>(gen), absl::Uint128Max());
}

TEST(MockDistributions, UniformDoubleBoundaryCasesAreAllowed) {
  absl::MockingBitGen gen;

  EXPECT_CALL(absl::MockUniform<double>(), Call(gen, 1.0, 10.0))
      .WillOnce(Return(
          std::nextafter(10.0, -std::numeric_limits<double>::infinity())));
  EXPECT_EQ(absl::Uniform<double>(gen, 1.0, 10.0),
            std::nextafter(10.0, -std::numeric_limits<double>::infinity()));

  EXPECT_CALL(absl::MockUniform<double>(),
              Call(absl::IntervalOpen, gen, 1.0, 10.0))
      .WillOnce(Return(
          std::nextafter(10.0, -std::numeric_limits<double>::infinity())));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 10.0),
            std::nextafter(10.0, -std::numeric_limits<double>::infinity()));

  EXPECT_CALL(absl::MockUniform<double>(),
              Call(absl::IntervalOpen, gen, 1.0, 10.0))
      .WillOnce(
          Return(std::nextafter(1.0, std::numeric_limits<double>::infinity())));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 10.0),
            std::nextafter(1.0, std::numeric_limits<double>::infinity()));

  constexpr double kMax = (std::numeric_limits<double>::max)();
  EXPECT_CALL(absl::MockUniform<double>(),
              Call(absl::IntervalClosed, gen, 0.0, kMax))
      .WillOnce(Return(kMax));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalClosed, gen, 0.0, kMax), kMax);

  EXPECT_CALL(absl::MockUniform<double>(),
              Call(absl::IntervalOpenClosed, gen, 0.0, kMax))
      .WillOnce(Return(kMax));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpenClosed, gen, 0.0, kMax),
            kMax);

  const double prev_max = std::nextafter(kMax, 0.0);
  EXPECT_CALL(absl::MockUniform<double>(),
              Call(absl::IntervalOpenClosed, gen, prev_max, kMax))
      .WillOnce(Return(kMax));
  EXPECT_EQ(
      absl::Uniform<double>(absl::IntervalOpenClosed, gen, prev_max, kMax),
      kMax);

  const double next = std::nextafter(1.0, 2.0);
  EXPECT_CALL(absl::MockUniform<double>(),
              Call(absl::IntervalOpen, gen, 1.0, next))
      .WillOnce(Return(1.0))
      .WillOnce(Return(next));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, next), 1.0);
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, next), next);
}

TEST(MockDistributions, UniformValidatorHandlesDegenerateRangesDirectly) {
  using IntValidator = absl::random_internal::UniformDistributionValidator<int>;
  using DoubleValidator =
      absl::random_internal::UniformDistributionValidator<double>;

  // Inverted, boundary-overflowing, or degenerate ranges must return cleanly
  // without hitting distribution debug assertions or crashing.
  EXPECT_NO_FATAL_FAILURE({
    IntValidator::Validate(1, absl::IntervalOpen, 1, 2);
    IntValidator::Validate(2, absl::IntervalOpen, 1, 2);
    IntValidator::Validate(10, absl::IntervalClosedOpen, 10, 1);
    IntValidator::Validate(1, absl::IntervalClosedOpen, 10, 1);

    DoubleValidator::Validate(10.0, absl::IntervalClosedOpen, 10.0, 1.0);
    DoubleValidator::Validate(1.0, absl::IntervalClosedOpen, 10.0, 1.0);
    constexpr double kMax = (std::numeric_limits<double>::max)();
    DoubleValidator::Validate(0.0, absl::IntervalClosed, -kMax, kMax);
    DoubleValidator::Validate(kMax, absl::IntervalClosed, -kMax, kMax);
  });
}

TEST(MockDistributions, UniformDoubleEmptyRangesAllowTheBoundary) {
  absl::MockingBitGen gen;

  ON_CALL(absl::MockUniform<double>(), Call(absl::IntervalOpen, gen, 1.0, 1.0))
      .WillByDefault(Return(1.0));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 1.0), 1.0);

  ON_CALL(absl::MockUniform<double>(),
          Call(absl::IntervalOpenClosed, gen, 1.0, 1.0))
      .WillByDefault(Return(1.0));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpenClosed, gen, 1.0, 1.0),
            1.0);

  ON_CALL(absl::MockUniform<double>(),
          Call(absl::IntervalClosedOpen, gen, 1.0, 1.0))
      .WillByDefault(Return(1.0));
  EXPECT_EQ(absl::Uniform<double>(absl::IntervalClosedOpen, gen, 1.0, 1.0),
            1.0);
}

TEST(MockDistributions, UniformIntEmptyRangeCasesAllowTheBoundary) {
  absl::MockingBitGen gen;

  ON_CALL(absl::MockUniform<int>(), Call(absl::IntervalOpen, gen, 1, 1))
      .WillByDefault(Return(1));
  EXPECT_EQ(absl::Uniform<int>(absl::IntervalOpen, gen, 1, 1), 1);

  ON_CALL(absl::MockUniform<int>(), Call(absl::IntervalOpenClosed, gen, 1, 1))
      .WillByDefault(Return(1));
  EXPECT_EQ(absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 1), 1);

  ON_CALL(absl::MockUniform<int>(), Call(absl::IntervalClosedOpen, gen, 1, 1))
      .WillByDefault(Return(1));
  EXPECT_EQ(absl::Uniform<int>(absl::IntervalClosedOpen, gen, 1, 1), 1);
}

TEST(MockUniformDeathTest, OutOfBoundsValuesAreRejected) {
  absl::MockingBitGen gen;

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 100))
            .WillOnce(Return(0));
        absl::Uniform<int>(gen, 1, 100);
      },
      " 0 is not in \\[1, 100\\)");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(gen, 1, 100);
      },
      " 101 is not in \\[1, 100\\)");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 1, 100))
            .WillOnce(Return(100));
        absl::Uniform<int>(gen, 1, 100);
      },
      " 100 is not in \\[1, 100\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpen, gen, 1, 100))
            .WillOnce(Return(1));
        absl::Uniform<int>(absl::IntervalOpen, gen, 1, 100);
      },
      " 1 is not in \\(1, 100\\)");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpen, gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(absl::IntervalOpen, gen, 1, 100);
      },
      " 101 is not in \\(1, 100\\)");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpen, gen, 1, 100))
            .WillOnce(Return(100));
        absl::Uniform<int>(absl::IntervalOpen, gen, 1, 100);
      },
      " 100 is not in \\(1, 100\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpenClosed, gen, 1, 100))
            .WillOnce(Return(1));
        absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 100);
      },
      " 1 is not in \\(1, 100\\]");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpenClosed, gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 100);
      },
      " 101 is not in \\(1, 100\\]");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpenClosed, gen, 1, 100))
            .WillOnce(Return(0));
        absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 100);
      },
      " 0 is not in \\(1, 100\\]");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalOpenClosed, gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(absl::IntervalOpenClosed, gen, 1, 100);
      },
      " 101 is not in \\(1, 100\\]");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalClosed, gen, 1, 100))
            .WillOnce(Return(0));
        absl::Uniform<int>(absl::IntervalClosed, gen, 1, 100);
      },
      " 0 is not in \\[1, 100\\]");
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<int>(),
                    Call(absl::IntervalClosed, gen, 1, 100))
            .WillOnce(Return(101));
        absl::Uniform<int>(absl::IntervalClosed, gen, 1, 100);
      },
      " 101 is not in \\[1, 100\\]");
}

TEST(MockUniformDeathTest, OutOfBoundsDoublesAreRejected) {
  absl::MockingBitGen gen;

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(), Call(gen, 1.0, 10.0))
            .WillOnce(Return(10.0));
        EXPECT_EQ(absl::Uniform<double>(gen, 1.0, 10.0), 10.0);
      },
      " 10 is not in \\[1, 10\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalOpen, gen, 1.0, 10.0))
            .WillOnce(Return(10.0));
        EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 10.0),
                  10.0);
      },
      " 10 is not in \\(1, 10\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalOpen, gen, 1.0, 10.0))
            .WillOnce(Return(1.0));
        EXPECT_EQ(absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, 10.0),
                  1.0);
      },
      " 1 is not in \\(1, 10\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalClosed, gen, 1.0, 10.0))
            .WillOnce(Return(0.5));
        absl::Uniform<double>(absl::IntervalClosed, gen, 1.0, 10.0);
      },
      " 0.5 is not in \\[1, 10\\]");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalClosed, gen, 1.0, 10.0))
            .WillOnce(Return(10.5));
        absl::Uniform<double>(absl::IntervalClosed, gen, 1.0, 10.0);
      },
      " 10.5 is not in \\[1, 10\\]");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalOpenClosed, gen, 1.0, 10.0))
            .WillOnce(Return(1.0));
        absl::Uniform<double>(absl::IntervalOpenClosed, gen, 1.0, 10.0);
      },
      " 1 is not in \\(1, 10\\]");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalOpenClosed, gen, 1.0, 10.0))
            .WillOnce(Return(10.5));
        absl::Uniform<double>(absl::IntervalOpenClosed, gen, 1.0, 10.0);
      },
      " 10.5 is not in \\(1, 10\\]");

  constexpr double kMax = (std::numeric_limits<double>::max)();
  const double prev_max = std::nextafter(kMax, 0.0);
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalOpenClosed, gen, prev_max, kMax))
            .WillOnce(Return(prev_max));
        absl::Uniform<double>(absl::IntervalOpenClosed, gen, prev_max, kMax);
      },
      "1\\.7976931348623155e\\+308 is not in "
      "\\(1\\.7976931348623155e\\+308, 1\\.7976931348623157e\\+308\\]");

  const double next = std::nextafter(1.0, 2.0);
  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalOpen, gen, 1.0, next))
            .WillOnce(Return(0.5));
        absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, next);
      },
      " 0.5 is not in \\(1, 1\\.0000000000000002\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        EXPECT_CALL(absl::MockUniform<double>(),
                    Call(absl::IntervalOpen, gen, 1.0, next))
            .WillOnce(Return(2.0));
        absl::Uniform<double>(absl::IntervalOpen, gen, 1.0, next);
      },
      " 2 is not in \\(1, 1\\.0000000000000002\\)");
}

TEST(MockUniformDeathTest, SucceedInCallbackDoesNotSkipValidation) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::MockingBitGen gen;
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 10, 100))
            .WillOnce([](int, int) {
              SUCCEED();
              return 0;
            });
        absl::Uniform<int>(gen, 10, 100);
      },
      " 0 is not in \\[10, 100\\)");
}

TEST(MockUniformDeathTest, NestedCallsDoNotSuppressOuterValidation) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::MockingBitGen gen;
        EXPECT_CALL(absl::MockUniform<double>(), Call(gen, 1.0, 2.0))
            .WillOnce(Return(1.5));
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 10, 100))
            .WillOnce([&gen](int, int) {
              // Consumed failure inside an inner scope must not suppress
              // validation on the enclosing call.
              EXPECT_NONFATAL_FAILURE(
                  (void)absl::Uniform<double>(gen, 5.0, 10.0),
                  "Unexpected mock function call");
              return 0;
            });
        absl::Uniform<int>(gen, 10, 100);
      },
      " 0 is not in \\[10, 100\\)");

  EXPECT_DEATH_IF_SUPPORTED(
      {
        ::testing::NiceMock<absl::MockingBitGen> gen;
        ON_CALL(absl::MockUniform<double>(), Call(gen, 1.0, 2.0))
            .WillByDefault(Return(1.5));
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 10, 100))
            .WillOnce([&gen](int, int) {
              // Non-failing default action returning in-range 0.0 must not
              // suppress validation on the enclosing call.
              EXPECT_EQ(absl::Uniform<double>(gen, 0.0, 10.0), 0.0);
              return 0;
            });
        absl::Uniform<int>(gen, 10, 100);
      },
      " 0 is not in \\[10, 100\\)");
}

TEST(MockUniformDeathTest, SilentDefaultActionsStillValidate) {
  // Naggy (default) uninteresting call returns default 0 and must abort.
  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::MockingBitGen gen;
        ON_CALL(absl::MockUniform<int>(), Call(gen, 1, 10))
            .WillByDefault(Return(5));
        absl::Uniform<int>(gen, 20, 30);
      },
      " 0 is not in \\[20, 30\\)");

  // NiceMock uninteresting call returns default 0 and must abort.
  EXPECT_DEATH_IF_SUPPORTED(
      {
        ::testing::NiceMock<absl::MockingBitGen> gen;
        ON_CALL(absl::MockUniform<int>(), Call(gen, 1, 10))
            .WillByDefault(Return(5));
        absl::Uniform<int>(gen, 20, 30);
      },
      " 0 is not in \\[20, 30\\)");

  // EXPECT_CALL with no action returns default 0 and must abort.
  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::MockingBitGen gen;
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 10, 100));
        absl::Uniform<int>(gen, 10, 100);
      },
      " 0 is not in \\[10, 100\\)");

  // EXPECT_CALL with explicit DoDefault() returns default 0 and must abort.
  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::MockingBitGen gen;
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 10, 100))
            .WillOnce(::testing::DoDefault());
        absl::Uniform<int>(gen, 10, 100);
      },
      " 0 is not in \\[10, 100\\)");

  // Exhausted .Times(2).WillOnce(...) below upper bound returns default 0 and
  // must abort.
  EXPECT_DEATH_IF_SUPPORTED(
      {
        absl::MockingBitGen gen;
        EXPECT_CALL(absl::MockUniform<int>(), Call(gen, 10, 100))
            .Times(2)
            .WillOnce(Return(42));
        EXPECT_EQ(absl::Uniform<int>(gen, 10, 100), 42);
        absl::Uniform<int>(gen, 10, 100);
      },
      " 0 is not in \\[10, 100\\)");
}

}  // namespace
