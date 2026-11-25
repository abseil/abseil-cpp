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

#include "absl/random/discrete_distribution.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"

namespace {

using ::testing::DoubleEq;
using ::testing::ElementsAre;
using ::testing::Gt;
using ::testing::Lt;
using ::testing::Pointwise;

// Fixed nothing-up-my-sleeve seed for deterministic sampling tests (fractional
// hex digits of Euler's number e - 2, used across Abseil random tests).
constexpr uint64_t kTestSeed = 0x2B7E151628AED2A6ULL;
constexpr double kMaxDouble = (std::numeric_limits<double>::max)();
constexpr double kLargePowerOfTwo = 0x1p1023;

template <typename IntType>
class DiscreteDistributionTypeTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int8_t, uint8_t, int16_t, uint16_t, int32_t,
                                  uint32_t, int64_t, uint64_t>;
TYPED_TEST_SUITE(DiscreteDistributionTypeTest, IntTypes);

TYPED_TEST(DiscreteDistributionTypeTest, ParamSerializeTest) {
  using param_type =
      typename absl::discrete_distribution<TypeParam>::param_type;

  absl::discrete_distribution<TypeParam> empty;
  EXPECT_THAT(empty.probabilities(), testing::ElementsAre(1.0));

  absl::discrete_distribution<TypeParam> before({1.0, 2.0, 1.0});

  // Validate that the probabilities sum to 1.0. We picked values which
  // can be represented exactly to avoid floating-point roundoff error.
  double s = 0;
  for (const auto& x : before.probabilities()) {
    s += x;
  }
  EXPECT_EQ(s, 1.0);
  EXPECT_THAT(before.probabilities(), testing::ElementsAre(0.25, 0.5, 0.25));

  // Validate the same data via an initializer list.
  {
    std::vector<double> data({1.0, 2.0, 1.0});

    absl::discrete_distribution<TypeParam> via_param{
        param_type(std::begin(data), std::end(data))};

    EXPECT_EQ(via_param, before);
  }

  std::stringstream ss;
  ss << before;
  absl::discrete_distribution<TypeParam> after;

  EXPECT_NE(before, after);

  ss >> after;

  EXPECT_EQ(before, after);
}

TYPED_TEST(DiscreteDistributionTypeTest, Constructor) {
  auto fn = [](double x) { return x; };
  {
    absl::discrete_distribution<int> unary(0, 1.0, 9.0, fn);
    EXPECT_THAT(unary.probabilities(), testing::ElementsAre(1.0));
  }

  {
    absl::discrete_distribution<int> unary(2, 1.0, 9.0, fn);
    // => fn(1.0 + 0 * 4 + 2) => 3
    // => fn(1.0 + 1 * 4 + 2) => 7
    EXPECT_THAT(unary.probabilities(), testing::ElementsAre(0.3, 0.7));
  }
}

TEST(DiscreteDistributionTest, InitDiscreteDistribution) {
  using testing::_;
  using testing::Pair;

  {
    std::vector<double> p({1.0, 2.0, 3.0});
    constexpr double sum = 6.0;
    std::vector<std::pair<double, size_t>> q =
        absl::random_internal::InitDiscreteDistribution(p);

    // After initialization, probabilities are normalized to 1.0.
    EXPECT_THAT(p, testing::ElementsAre(1.0 / sum, 2.0 / sum, 3.0 / sum));

    // Each bucket is p=1/3, so bucket 0 will send half it's traffic
    // to bucket 2, while the rest will retain all of their traffic.
    EXPECT_THAT(q, testing::ElementsAre(Pair(0.5, 2),  //
                                        Pair(1.0, _),  //
                                        Pair(1.0, _)));
  }

  {
    std::vector<double> p({1.0, 2.0, 3.0, 5.0, 2.0});  // sum = 13.0
    constexpr double sum = 13.0;
    std::vector<std::pair<double, size_t>> q =
        absl::random_internal::InitDiscreteDistribution(p);

    // After initialization, probabilities are normalized to 1.0.
    EXPECT_THAT(p, testing::ElementsAre(1.0 / sum, 2.0 / sum, 3.0 / sum,
                                        5.0 / sum, 2.0 / sum));

    // A more complex bucketing solution: Each bucket has p=0.2
    // So buckets 0, 1, 4 will send their alternate traffic elsewhere, which
    // happens to be bucket 3.
    // However, summing up that alternate traffic gives bucket 3 too much
    // traffic, so it will send some traffic to bucket 2.
    constexpr double b0 = 1.0 / 13.0 / 0.2;
    constexpr double b1 = 2.0 / 13.0 / 0.2;
    constexpr double b3 = (5.0 / 13.0 / 0.2) - ((1 - b0) + (1 - b1) + (1 - b1));

    EXPECT_THAT(q, testing::ElementsAre(Pair(b0, 3),   //
                                        Pair(b1, 3),   //
                                        Pair(1.0, _),  //
                                        Pair(b3, 2),   //
                                        Pair(b1, 3)));
  }
}

TEST(DiscreteDistributionTest, ChiSquaredTest50) {
  using absl::random_internal::kChiSquared;

  constexpr size_t kTrials = 10000;
  constexpr int kBuckets = 50;  // inclusive, so actually +1

  // 1-in-100000 threshold, but remember, there are about 8 tests
  // in this file. And the test could fail for other reasons.
  // Empirically validated with --runs_per_test=10000.
  const int kThreshold =
      absl::random_internal::ChiSquareValue(kBuckets, 0.99999);

  std::vector<double> weights(kBuckets, 0);
  std::iota(std::begin(weights), std::end(weights), 1);
  absl::discrete_distribution<int> dist(std::begin(weights), std::end(weights));

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng(kTestSeed);

  std::vector<int32_t> counts(kBuckets, 0);
  for (size_t i = 0; i < kTrials; i++) {
    auto x = dist(rng);
    counts[x]++;
  }

  // Scale weights.
  double sum = 0;
  for (double x : weights) {
    sum += x;
  }
  for (double& x : weights) {
    x = kTrials * (x / sum);
  }

  double chi_square =
      absl::random_internal::ChiSquare(std::begin(counts), std::end(counts),
                                       std::begin(weights), std::end(weights));

  if (chi_square > kThreshold) {
    double p_value =
        absl::random_internal::ChiSquarePValue(chi_square, kBuckets);

    // Chi-squared test failed. Output does not appear to be uniform.
    std::string msg;
    for (size_t i = 0; i < counts.size(); i++) {
      absl::StrAppend(&msg, i, ": ", counts[i], " vs ", weights[i], "\n");
    }
    absl::StrAppend(&msg, kChiSquared, " p-value ", p_value, "\n");
    absl::StrAppend(&msg, "High ", kChiSquared, " value: ", chi_square, " > ",
                    kThreshold);
    LOG(INFO) << msg;
    FAIL() << msg;
  }
}

TEST(DiscreteDistributionTest, StabilityTest) {
  // absl::discrete_distribution stability relies on
  // absl::uniform_int_distribution and absl::bernoulli_distribution.
  absl::random_internal::sequence_urbg urbg(
      {0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
       0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
       0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
       0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull});

  std::vector<int> output(6);

  {
    absl::discrete_distribution<int32_t> dist({1.0, 2.0, 3.0, 5.0, 2.0});
    EXPECT_EQ(0, dist.min());
    EXPECT_EQ(4, dist.max());
    for (auto& v : output) {
      v = dist(urbg);
    }
    EXPECT_EQ(12, urbg.invocations());
  }

  // With 12 calls to urbg, each call into discrete_distribution consumes
  // precisely 2 values: one for the uniform call, and a second for the
  // bernoulli.
  //
  // Given the alt mapping: 0=>3, 1=>3, 2=>2, 3=>2, 4=>3, we can
  //
  // uniform:      443210143131
  // bernoulli: b0 000011100101
  // bernoulli: b1 001111101101
  // bernoulli: b2 111111111111
  // bernoulli: b3 001111101111
  // bernoulli: b4 001111101101
  // ...
  EXPECT_THAT(output, testing::ElementsAre(3, 3, 1, 3, 3, 3));

  // Test that the type of the distribution does not affect the results.
  {
    urbg.reset();
    absl::discrete_distribution<int64_t> dist({1.0, 2.0, 3.0, 5.0, 2.0});
    EXPECT_EQ(0, dist.min());
    EXPECT_EQ(4, dist.max());
    for (auto& v : output) {
      v = dist(urbg);
    }
    EXPECT_EQ(12, urbg.invocations());
  }
  EXPECT_THAT(output, testing::ElementsAre(3, 3, 0, 3, 0, 4));
}

MATCHER(FailsLeavingUnchanged,
        "fails stream extraction and leaves target and stream flags "
        "unchanged") {
  return arg.failed && arg.flags_preserved && arg.value == arg.initial;
}

MATCHER_P(ParsesEqualTo, expected,
          "succeeds, updates target, and preserves stream flags") {
  return !arg.failed && arg.flags_preserved && arg.value == expected;
}

TEST(DiscreteDistributionTest, MaxNarrowTypeSerializationRoundTrips) {
  // 256 weights for uint8_t (max == 255) previously wrapped uint8_t loop index
  // at 255 -> 0; 128 weights for int8_t (max == 127) overflowed signed int8_t.
  {
    std::vector<double> weights(256, 1.0);
    const absl::discrete_distribution<uint8_t> expected(weights.begin(),
                                                        weights.end());
    std::ostringstream oss;
    oss << expected;
    EXPECT_THAT(absl::random_internal::ParseFromStream(
                    absl::discrete_distribution<uint8_t>(), oss.str()),
                ParsesEqualTo(expected));
  }
  {
    std::vector<double> weights(128, 1.0);
    const absl::discrete_distribution<int8_t> expected(weights.begin(),
                                                       weights.end());
    std::ostringstream oss;
    oss << expected;
    EXPECT_THAT(absl::random_internal::ParseFromStream(
                    absl::discrete_distribution<int8_t>(), oss.str()),
                ParsesEqualTo(expected));
  }
}

TEST(DiscreteDistributionTest, StreamRejectsInvalidParameters) {
  using absl::random_internal::ParseFromStream;
  const absl::discrete_distribution<int> kInitial({1.0, 2.0});
  const absl::discrete_distribution<int8_t> kI8Initial({1.0, 2.0});
  const absl::discrete_distribution<uint8_t> kU8Initial({1.0, 2.0});

  // Valid streams including subnormal positive weight.
  EXPECT_THAT(ParseFromStream(kInitial, "2 0.25 0.75"),
              ParsesEqualTo(absl::discrete_distribution<int>({0.25, 0.75})));
  EXPECT_THAT(ParseFromStream(kInitial, "2 1e-320 1.0"),
              ParsesEqualTo(absl::discrete_distribution<int>({1e-320, 1.0})));

  // Empty, malformed, zero, and negative counts (including unsigned wraparound
  // to n == 2 on 64-bit platforms).
  EXPECT_THAT(ParseFromStream(kInitial, ""), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "abc"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-1 1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-2 0.5 0.5"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-18446744073709551614 0.25 0.75"),
              FailsLeavingUnchanged());

  // Count exceeding IntType::max() + 1 or huge truncated count (must not OOM).
  EXPECT_THAT(ParseFromStream(kI8Initial, "129 1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Initial, "257 1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "1000000000000 1.0"),
              FailsLeavingUnchanged());

  // Truncated, all-zero, negative, overflowing-sum, and non-finite weights.
  EXPECT_THAT(ParseFromStream(kInitial, "2 1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "1 0.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "3 0.0 0.0 0.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2 -1.0 1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2 1.0 -1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2 1e308 1e308"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "3 1e308 1e308 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2 nan 1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2 1.0 nan"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2 inf 1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2 1.0 inf"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2 1.0 -inf"), FailsLeavingUnchanged());

  // Non-finite weights delivered with goodbit by a custom num_get facet.
  EXPECT_THAT(
      ParseFromStream(kInitial, "2 nan 1.0", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kInitial, "2 1.0 nan", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kInitial, "2 inf 1.0", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kInitial, "2 1.0 inf", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kInitial, "2 1.0 -inf", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
}

TEST(DiscreteDistributionTest, AllZeroWeightsDefaultToSingleOutcome) {
  absl::random_internal::pcg64_2018_engine rng(kTestSeed);
  absl::discrete_distribution<int> all_zero({0.0, 0.0, 0.0});

  EXPECT_THAT(all_zero.probabilities(), ElementsAre(1.0));
  EXPECT_EQ(all_zero(rng), 0);
}

TEST(DiscreteDistributionTest, SubnormalWeightsNormalize) {
  // x/3, x should normalize within a few ULPs of 0.25, 0.75 even when x/3 is
  // subnormal.
  constexpr double kMinDouble = (std::numeric_limits<double>::min)();
  absl::discrete_distribution<int> dist({kMinDouble / 3.0, kMinDouble});

  EXPECT_THAT(dist.probabilities(), Pointwise(DoubleEq(), {0.25, 0.75}));
}

TEST(DiscreteDistributionTest, OverflowingWeightsNormalize) {
  // x/3, x should normalize within a few ULPs of 0.25, 0.75 even when direct
  // summation overflows double.
  absl::discrete_distribution<int> dist({kMaxDouble / 3.0, kMaxDouble});

  EXPECT_THAT(dist.probabilities(), Pointwise(DoubleEq(), {0.25, 0.75}));
}

TEST(DiscreteDistributionTest,
     OverflowingDominantWeightPreservesMultipleSecondaryWeights) {
  // kV / kMaxDouble == DBL_EPSILON / 4 == 2^-54 is below 0.5 * ULP(1.0), so
  // naive summation after max-scaling would lose all six secondary weights
  // into 1.0.
  constexpr double kV =
      kMaxDouble * (std::numeric_limits<double>::epsilon() / 4.0);
  constexpr double kHalfV = 0.5 * kV;
  absl::discrete_distribution<int> overflow(
      {kMaxDouble, kV, kV, kV, kV, kV, kV});
  absl::discrete_distribution<int> scaled(
      {0.5 * kMaxDouble, kHalfV, kHalfV, kHalfV, kHalfV, kHalfV, kHalfV});

  EXPECT_THAT(overflow.probabilities(),
              ElementsAre(Lt(1.0), Gt(0.0), Gt(0.0), Gt(0.0), Gt(0.0), Gt(0.0),
                          Gt(0.0)));

  EXPECT_THAT(overflow.probabilities(),
              Pointwise(DoubleEq(), scaled.probabilities()));
}

TEST(DiscreteDistributionTest, SlightlyUnnormalizedWeightsNormalize) {
  constexpr double kDeficit = 1024.0 * std::numeric_limits<double>::epsilon();
  absl::discrete_distribution<int> dist({0.5 - kDeficit, 0.5});

  EXPECT_THAT(dist.probabilities()[1], Gt(0.5));
}

TEST(DiscreteDistributionTest, UnaryOpConstructorOddBucketsOverflowingRange) {
  std::vector<double> args;
  const double weights[] = {1.0, 2.0, 1.0};
  absl::discrete_distribution<int> dist(3, -1.5 * kLargePowerOfTwo,
                                        1.5 * kLargePowerOfTwo, [&](double x) {
                                          const double w = weights[args.size()];
                                          args.push_back(x);
                                          return w;
                                        });

  EXPECT_THAT(args, ElementsAre(DoubleEq(-kLargePowerOfTwo), DoubleEq(0.0),
                                DoubleEq(kLargePowerOfTwo)));
  EXPECT_THAT(dist.probabilities(), ElementsAre(0.25, 0.5, 0.25));
}

TEST(DiscreteDistributionTest, UnaryOpConstructorAsymmetricOverflowingRange) {
  std::vector<double> args;
  absl::discrete_distribution<int> dist(
      4, -1.5 * kLargePowerOfTwo, 0.5 * kLargePowerOfTwo, [&](double x) {
        args.push_back(x);
        return static_cast<double>(args.size());
      });

  EXPECT_THAT(args, ElementsAre(DoubleEq(-1.25 * kLargePowerOfTwo),
                                DoubleEq(-0.75 * kLargePowerOfTwo),
                                DoubleEq(-0.25 * kLargePowerOfTwo),
                                DoubleEq(0.25 * kLargePowerOfTwo)));
  EXPECT_THAT(dist.probabilities(), ElementsAre(0.1, 0.2, 0.3, 0.4));
}

MATCHER(RoundTripsThroughStream, "round-trips through std::stringstream") {
  absl::discrete_distribution<int> before(arg.begin(), arg.end());
  std::stringstream ss;
  ss << before;
  absl::discrete_distribution<int> after;
  ss >> after;
  if (ss.fail()) {
    *result_listener << "stream deserialization failed";
    return false;
  }
  if (before != after) {
    *result_listener << "deserialized distribution differs from original";
    return false;
  }
  return true;
}

TEST(DiscreteDistributionTest, LargeNonPowerOfTwoSerializationRoundTrip) {
  // With kRoundingError = 16 * eps, uncompensated summation drifts
  // by > 400 * eps at n = 10000 and would spuriously re-normalize during
  // operator>>.
  EXPECT_THAT(std::vector<double>(10000, 1.0), RoundTripsThroughStream());

  auto make_varied_weights = [](size_t n) {
    std::vector<double> weights(n);
    for (size_t i = 0; i < n; ++i) {
      weights[i] = 1.0 + static_cast<double>((i * 31) % 997) * 0.13;
    }
    return weights;
  };
  EXPECT_THAT(make_varied_weights(10000), RoundTripsThroughStream());
}

#ifdef NDEBUG
constexpr size_t kNdebugSampleCount = 64;

std::vector<int> DrawNdebugSamples(absl::discrete_distribution<int>& dist) {
  absl::random_internal::pcg64_2018_engine rng(kTestSeed);
  std::vector<int> samples(kNdebugSampleCount);
  std::generate(samples.begin(), samples.end(), [&] { return dist(rng); });
  return samples;
}

TEST(DiscreteDistributionTest,
     AllNegativeWeightsDefaultToSingleOutcomeWhenNdebug) {
  absl::discrete_distribution<int> dist({-1.0, -2.0});

  EXPECT_THAT(dist.probabilities(), ElementsAre(1.0));
  EXPECT_THAT(DrawNdebugSamples(dist), testing::Each(0));
}

TEST(DiscreteDistributionTest,
     AllInvalidWeightsDefaultToSingleOutcomeWhenNdebug) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  constexpr double kSnan = std::numeric_limits<double>::signaling_NaN();
  absl::discrete_distribution<int> dist({-0.0, -1.0, kNan, kSnan, kInf, -kInf});

  EXPECT_THAT(dist.probabilities(), ElementsAre(1.0));
  EXPECT_THAT(DrawNdebugSamples(dist), testing::Each(0));
}

TEST(DiscreteDistributionTest,
     MixedInvalidWeightsClampToPositiveZeroWhenNdebug) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  absl::discrete_distribution<int> dist(
      {-2.0, 1.0, kNan, 3.0, kInf, -kInf, -0.0});

  EXPECT_THAT(dist.probabilities(),
              ElementsAre(0.0, 0.25, 0.0, 0.75, 0.0, 0.0, 0.0));
  EXPECT_FALSE(std::signbit(dist.probabilities()[6]));
  EXPECT_THAT(DrawNdebugSamples(dist), testing::Each(testing::AnyOf(1, 3)));
}

TEST(DiscreteDistributionTest,
     OverflowingMixedInvalidWeightsNormalizeWhenNdebug) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  absl::discrete_distribution<int> dist(
      {kMaxDouble, kMaxDouble, -kMaxDouble, kNan, kInf});

  EXPECT_THAT(dist.probabilities(), ElementsAre(0.5, 0.5, 0.0, 0.0, 0.0));
  EXPECT_THAT(DrawNdebugSamples(dist), testing::Each(testing::AnyOf(0, 1)));
}
#endif

}  // namespace
