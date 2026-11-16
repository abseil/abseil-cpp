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

#include "absl/random/log_uniform_int_distribution.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"

namespace {

using ::testing::AllOf;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::SizeIs;

// Fixed nothing-up-my-sleeve seed for deterministic sampling tests (hex digits
// of Euler's number e, used across Abseil random tests).
constexpr uint64_t kTestSeed = 0x2B7E151628AED2A6ULL;
// ~5 sigma for binomial(n, p) with expected bucket count 1000.
constexpr int kExpectedBucketHits = 1000;
constexpr int kBucketHitTolerance = 150;

constexpr uint64_t PowU64(uint64_t base, int exp) {
  return exp == 0 ? 1 : base * PowU64(base, exp - 1);
}

template <typename IntType>
class LogUniformIntDistributionTypeTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int8_t, int16_t, int32_t, int64_t,  //
                                  uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(LogUniformIntDistributionTypeTest, IntTypes);

TYPED_TEST(LogUniformIntDistributionTypeTest, SerializeTest) {
  using param_type =
      typename absl::log_uniform_int_distribution<TypeParam>::param_type;
  using Limits = std::numeric_limits<TypeParam>;

  constexpr int kCount = 1000;
  absl::InsecureBitGen gen;
  for (const auto& param : {
           param_type(0, 1),                             //
           param_type(0, 2),                             //
           param_type(0, 2, 10),                         //
           param_type(9, 32, 4),                         //
           param_type(1, 101, 10),                       //
           param_type(1, Limits::max() / 2),             //
           param_type(0, Limits::max() - 1),             //
           param_type(0, Limits::max(), 2),              //
           param_type(0, Limits::max(), 10),             //
           param_type(Limits::min(), 0),                 //
           param_type(Limits::lowest(), Limits::max()),  //
           param_type(Limits::min(), Limits::max()),     //
       }) {
    // Validate parameters.
    const auto min = param.min();
    const auto max = param.max();
    const auto base = param.base();
    absl::log_uniform_int_distribution<TypeParam> before(min, max, base);
    EXPECT_EQ(before.min(), param.min());
    EXPECT_EQ(before.max(), param.max());
    EXPECT_EQ(before.base(), param.base());

    {
      absl::log_uniform_int_distribution<TypeParam> via_param(param);
      EXPECT_EQ(via_param, before);
    }

    // Validate stream serialization.
    std::stringstream ss;
    ss << before;

    absl::log_uniform_int_distribution<TypeParam> after(3, 6, 17);

    EXPECT_NE(before.max(), after.max());
    EXPECT_NE(before.base(), after.base());
    EXPECT_NE(before.param(), after.param());
    EXPECT_NE(before, after);

    ss >> after;

    EXPECT_EQ(before.min(), after.min());
    EXPECT_EQ(before.max(), after.max());
    EXPECT_EQ(before.base(), after.base());
    EXPECT_EQ(before.param(), after.param());
    EXPECT_EQ(before, after);

    // Smoke test.
    auto sample_min = after.max();
    auto sample_max = after.min();
    for (int i = 0; i < kCount; i++) {
      auto sample = after(gen);
      EXPECT_GE(sample, after.min());
      EXPECT_LE(sample, after.max());
      if (sample > sample_max) sample_max = sample;
      if (sample < sample_min) sample_min = sample;
    }
    LOG(INFO) << "Range: " << sample_min << ", " << sample_max;
  }
}

// operator>> must reject input that violates the param_type preconditions
// (max >= min and base > 1) by setting failbit and leaving the distribution
// unchanged, rather than constructing an out-of-contract param_type.
TYPED_TEST(LogUniformIntDistributionTypeTest, DeserializeRejectsInvalidParams) {
  for (const char* input : {
           "0 100 1",  // base == 1
           "0 100 0",  // base == 0
           "100 0 2",  // max < min
       }) {
    absl::log_uniform_int_distribution<TypeParam> dist(3, 6, 17);
    const auto before = dist.param();

    std::istringstream is(input);
    is >> dist;

    EXPECT_TRUE(is.fail()) << input;
    EXPECT_EQ(dist.param(), before) << input;
  }
}

using log_uniform_i32 = absl::log_uniform_int_distribution<int32_t>;

class LogUniformIntChiSquaredTest
    : public testing::TestWithParam<log_uniform_i32::param_type> {
 public:
  // The ChiSquaredTestImpl provides a chi-squared goodness of fit test for
  // data generated by the log-uniform-int distribution.
  double ChiSquaredTestImpl();

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{kTestSeed};
};

double LogUniformIntChiSquaredTest::ChiSquaredTestImpl() {
  using absl::random_internal::kChiSquared;

  const auto& param = GetParam();

  // Check the distribution of L=log(log_uniform_int_distribution, base),
  // expecting that L is roughly uniformly distributed, that is:
  //
  //   P[L=0] ~= P[L=1] ~= ... ~= P[L=log(max)]
  //
  // For a total of X entries, each bucket should contain some number of samples
  // in the interval [X/k - a, X/k + a].
  //
  // Where `a` is approximately sqrt(X/k). This is validated by bucketing
  // according to the log function and using a chi-squared test for uniformity.

  const bool is_2 = (param.base() == 2);
  const double base_log = 1.0 / std::log(param.base());
  const auto bucket_index = [base_log, is_2, &param](int32_t x) {
    uint64_t y = static_cast<uint64_t>(x) - param.min();
    return (y == 0) ? 0
           : is_2   ? static_cast<int>(1 + std::log2(y))
                    : static_cast<int>(1 + std::log(y) * base_log);
  };
  const int max_bucket = bucket_index(param.max());  // inclusive
  const size_t trials = 15 + (max_bucket + 1) * 10;

  log_uniform_i32 dist(param);

  std::vector<int64_t> buckets(max_bucket + 1);
  for (size_t i = 0; i < trials; ++i) {
    const auto sample = dist(rng_);
    // Check the bounds.
    ABSL_ASSERT(sample <= dist.max());
    ABSL_ASSERT(sample >= dist.min());
    // Convert the output of the generator to one of num_bucket buckets.
    int bucket = bucket_index(sample);
    ABSL_ASSERT(bucket <= max_bucket);
    ++buckets[bucket];
  }

  // The null-hypothesis is that the distribution is uniform with respect to
  // log-uniform-int bucketization.
  const int dof = buckets.size() - 1;
  const double expected = trials / static_cast<double>(buckets.size());

  const double threshold = absl::random_internal::ChiSquareValue(dof, 0.98);

  double chi_square = absl::random_internal::ChiSquareWithExpected(
      std::begin(buckets), std::end(buckets), expected);

  const double p = absl::random_internal::ChiSquarePValue(chi_square, dof);

  if (chi_square > threshold) {
    LOG(INFO) << "values";
    for (size_t i = 0; i < buckets.size(); i++) {
      LOG(INFO) << i << ": " << buckets[i];
    }
    LOG(INFO) << "trials=" << trials << "\n"
              << kChiSquared << "(data, " << dof << ") = " << chi_square << " ("
              << p << ")\n"
              << kChiSquared << " @ 0.98 = " << threshold;
  }
  return p;
}

TEST_P(LogUniformIntChiSquaredTest, MultiTest) {
  const int kTrials = 5;
  int failures = 0;
  for (int i = 0; i < kTrials; i++) {
    double p_value = ChiSquaredTestImpl();
    if (p_value < 0.005) {
      failures++;
    }
  }

  // There is a 0.10% chance of producing at least one failure, so raise the
  // failure threshold high enough to allow for a flake rate < 10,000.
  EXPECT_LE(failures, 4);
}

// Generate the parameters for the test.
std::vector<log_uniform_i32::param_type> GenParams() {
  using Param = log_uniform_i32::param_type;
  using Limits = std::numeric_limits<int32_t>;

  return std::vector<Param>{
      Param{0, 1, 2},
      Param{1, 1, 2},
      Param{0, 2, 2},
      Param{0, 3, 2},
      Param{0, 4, 2},
      Param{0, 9, 10},
      Param{0, 10, 10},
      Param{0, 11, 10},
      Param{1, 10, 10},
      Param{0, (1 << 8) - 1, 2},
      Param{0, (1 << 8), 2},
      Param{0, (1 << 30) - 1, 2},
      Param{-1000, 1000, 10},
      Param{0, Limits::max(), 2},
      Param{0, Limits::max(), 3},
      Param{0, Limits::max(), 10},
      Param{Limits::min(), 0},
      Param{Limits::min(), Limits::max(), 2},
  };
}

std::string ParamName(
    const ::testing::TestParamInfo<log_uniform_i32::param_type>& info) {
  const auto& p = info.param;
  std::string name =
      absl::StrCat("min_", p.min(), "__max_", p.max(), "__base_", p.base());
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(All, LogUniformIntChiSquaredTest,
                         ::testing::ValuesIn(GenParams()), ParamName);

// NOTE: absl::log_uniform_int_distribution is not guaranteed to be stable.
TEST(LogUniformIntDistributionTest, StabilityTest) {
  // absl::log_uniform_int_distribution stability relies on
  // absl::uniform_int_distribution, absl::random_internal::BitWidth, and
  // integer exponentiation.
  absl::random_internal::sequence_urbg urbg(
      {0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
       0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
       0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
       0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull});

  std::vector<int> output(6);

  {
    absl::log_uniform_int_distribution<int32_t> dist(0, 256);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
    EXPECT_THAT(output, ElementsAre(256, 66, 4, 6, 57, 103));
  }
  urbg.reset();
  {
    absl::log_uniform_int_distribution<int32_t> dist(0, 256, 10);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
    EXPECT_THAT(output, ElementsAre(8, 4, 0, 0, 0, 69));
  }
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

TEST(LogUniformIntDistributionTest, StreamRejectsInvalidParameters) {
  using absl::random_internal::ParseFromStream;
  const absl::log_uniform_int_distribution<int8_t> kI8Initial(3, 6, 17);
  const absl::log_uniform_int_distribution<uint8_t> kU8Initial(3, 6, 17);

  EXPECT_THAT(
      ParseFromStream(kI8Initial, "-128 127 2"),
      ParsesEqualTo(absl::log_uniform_int_distribution<int8_t>(-128, 127, 2)));
  EXPECT_THAT(
      ParseFromStream(kU8Initial, "0 255 2"),
      ParsesEqualTo(absl::log_uniform_int_distribution<uint8_t>(0, 255, 2)));

  EXPECT_THAT(ParseFromStream(kI8Initial, "-254 10 2"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI8Initial, "-129 10 2"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI8Initial, "0 128 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI8Initial, "0 10 128"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI8Initial, "0 10 258"), FailsLeavingUnchanged());

  EXPECT_THAT(ParseFromStream(kU8Initial, "0 256 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Initial, "0 300 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Initial, "0 10 256"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Initial, "0 10 258"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Initial, "-5 10 2"), FailsLeavingUnchanged());
}

template <typename IntType>
class LogUniformIntDistributionUnsignedTypeTest : public ::testing::Test {};

using UnsignedIntTypes =
    ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(LogUniformIntDistributionUnsignedTypeTest, UnsignedIntTypes);

TYPED_TEST(LogUniformIntDistributionUnsignedTypeTest,
           StreamDeserializationValidation) {
  using absl::random_internal::ParseFromStream;
  using Dist = absl::log_uniform_int_distribution<TypeParam>;
  const Dist kInitial(3, 6, 17);

  EXPECT_THAT(ParseFromStream(kInitial, "0 10 2"),
              ParsesEqualTo(Dist(0, 10, 2)));
  EXPECT_THAT(ParseFromStream(kInitial, "  0  10  2"),
              ParsesEqualTo(Dist(0, 10, 2)));
  EXPECT_THAT(ParseFromStream(kInitial, "+0 +10 +2"),
              ParsesEqualTo(Dist(0, 10, 2)));
  EXPECT_THAT(ParseFromStream(kInitial, "  +5  +20  +3"),
              ParsesEqualTo(Dist(5, 20, 3)));
  EXPECT_THAT(ParseFromStream(kInitial, L"  0 10 2"),
              ParsesEqualTo(Dist(0, 10, 2)));

  EXPECT_THAT(ParseFromStream(kInitial, "0 100 1"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 100 0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "100 0 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-5 10 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-5 -1 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 -5 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 -1 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 10 -2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-0 10 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 -0 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "  -5 10 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "\t\n-5 10 2"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0   -5 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 10   -2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, L"-5 10 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, L"  0 -5 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, L"0 10 -2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, ""), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "   "), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 10"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 10   "), FailsLeavingUnchanged());
}

template <typename IntType>
class LogUniformIntDistributionSignedTypeTest : public ::testing::Test {};

using SignedIntTypes = ::testing::Types<int8_t, int16_t, int32_t, int64_t>;
TYPED_TEST_SUITE(LogUniformIntDistributionSignedTypeTest, SignedIntTypes);

TYPED_TEST(LogUniformIntDistributionSignedTypeTest,
           StreamDeserializationValidation) {
  using absl::random_internal::ParseFromStream;
  using Dist = absl::log_uniform_int_distribution<TypeParam>;
  const Dist kInitial(3, 6, 17);

  EXPECT_THAT(ParseFromStream(kInitial, "-10 -2 2"),
              ParsesEqualTo(Dist(-10, -2, 2)));
  EXPECT_THAT(ParseFromStream(kInitial, "  -10  -2  2"),
              ParsesEqualTo(Dist(-10, -2, 2)));
  EXPECT_THAT(ParseFromStream(kInitial, "-10 10 3"),
              ParsesEqualTo(Dist(-10, 10, 3)));
  EXPECT_THAT(ParseFromStream(kInitial, L"-10 -2 2"),
              ParsesEqualTo(Dist(-10, -2, 2)));

  EXPECT_THAT(ParseFromStream(kInitial, "0 100 1"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 100 0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "100 0 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-2 -10 2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-10 -2 -2"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-10 -2 1"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, ""), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0 10"), FailsLeavingUnchanged());
}

template <typename IntType>
struct SampleSummary {
  bool all_in_bounds = true;
  IntType min_sample = (std::numeric_limits<IntType>::max)();
  IntType max_sample = (std::numeric_limits<IntType>::min)();
  int max_hits = 0;
  std::vector<int> bucket_counts;
};

template <typename IntType>
void PrintTo(const SampleSummary<IntType>& summary, std::ostream* os) {
  // Unary '+' promotes int8_t/uint8_t so they print numerically, not as chars.
  *os << "{all_in_bounds: " << (summary.all_in_bounds ? "true" : "false")
      << ", min_sample: " << +summary.min_sample
      << ", max_sample: " << +summary.max_sample
      << ", max_hits: " << summary.max_hits
      << ", bucket_counts: " << ::testing::PrintToString(summary.bucket_counts)
      << "}";
}

MATCHER(AllInBounds, "") {
  return ::testing::ExplainMatchResult(
      Field("all_in_bounds", &std::decay_t<arg_type>::all_in_bounds, IsTrue()),
      arg, result_listener);
}

MATCHER_P(MinSampleIs, matcher, "") {
  return ::testing::ExplainMatchResult(
      Field("min_sample", &std::decay_t<arg_type>::min_sample, matcher), arg,
      result_listener);
}

MATCHER_P(MaxSampleIs, matcher, "") {
  return ::testing::ExplainMatchResult(
      Field("max_sample", &std::decay_t<arg_type>::max_sample, matcher), arg,
      result_listener);
}

MATCHER_P(MaxHitsIs, matcher, "") {
  return ::testing::ExplainMatchResult(
      Field("max_hits", &std::decay_t<arg_type>::max_hits, matcher), arg,
      result_listener);
}

MATCHER_P(BucketCountsAre, matcher, "") {
  return ::testing::ExplainMatchResult(
      Field("bucket_counts", &std::decay_t<arg_type>::bucket_counts, matcher),
      arg, result_listener);
}

MATCHER_P(HasNonEmptyBuckets, count, "") {
  return ::testing::ExplainMatchResult(
      Field("bucket_counts", &std::decay_t<arg_type>::bucket_counts,
            AllOf(SizeIs(count), Each(Gt(0)))),
      arg, result_listener);
}

// Runs `trials` draws using fixed seed `kTestSeed`, so runtime flake rate is 0.
// For an arbitrary seed, false-failure probability across the tests below is
// < 2e-5 (dominated by the base-3 top-tail check at ~1.8e-5; bucket-count
// bounds are ~1e-6 and HasNonEmptyBuckets union bounds are < 1e-65).
template <typename IntType>
SampleSummary<IntType> SampleLogUniform(IntType min, IntType max, IntType base,
                                        int trials) {
  using UIntType = std::make_unsigned_t<IntType>;
  const UIntType umin = static_cast<UIntType>(min);
  const UIntType umax = static_cast<UIntType>(max);
  const UIntType ubase = static_cast<UIntType>(base);
  // Oracle to determine bucket for each offset.
  const auto exact_bucket = [ubase](UIntType offset) -> size_t {
    if (offset == 0) return 0;
    size_t bucket = 1;
    // Use uint128 so `pow * ubase` cannot overflow for uint64_t offsets up to
    // UINT64_MAX.
    absl::uint128 pow = 1;
    while (pow * ubase <= offset) {
      pow *= ubase;
      ++bucket;
    }
    return bucket;
  };

  SampleSummary<IntType> summary;
  summary.bucket_counts.assign(exact_bucket(umax - umin) + 1, 0);
  absl::random_internal::pcg64_2018_engine rng(kTestSeed);
  absl::log_uniform_int_distribution<IntType> dist(min, max, base);
  for (int i = 0; i < trials; ++i) {
    const IntType v = dist(rng);
    if (v < min || v > max) {
      summary.all_in_bounds = false;
    } else {
      ++summary.bucket_counts[exact_bucket(static_cast<UIntType>(v) - umin)];
    }
    summary.min_sample = (std::min)(summary.min_sample, v);
    summary.max_sample = (std::max)(summary.max_sample, v);
    if (v == max) {
      ++summary.max_hits;
    }
  }
  return summary;
}

TEST(LogUniformIntDistributionTest, NegativeToMaxInt32RangeCoversAllBuckets) {
  // Width (max - min) exceeds INT32_MAX, exercising unsigned offset arithmetic.
  EXPECT_THAT(SampleLogUniform<int32_t>(-10, INT32_MAX, 10, /*trials=*/5000),
              AllOf(AllInBounds(), MinSampleIs(-10),
                    MaxSampleIs(Gt(INT32_MAX / 2)), HasNonEmptyBuckets(11)));
}

TEST(LogUniformIntDistributionTest, FullInt64RangeCoversAllBuckets) {
  EXPECT_THAT(
      SampleLogUniform<int64_t>(INT64_MIN, INT64_MAX, 10, /*trials=*/5000),
      AllOf(AllInBounds(), MinSampleIs(INT64_MIN),
            MaxSampleIs(Gt(INT64_MAX / 2)), HasNonEmptyBuckets(21)));
}

TEST(LogUniformIntDistributionTest, FullUint64RangeBase2Covers65FullBuckets) {
  EXPECT_THAT(
      SampleLogUniform<uint64_t>(0, UINT64_MAX, 2, /*trials=*/10000),
      AllOf(AllInBounds(), MinSampleIs(0u), MaxSampleIs(Ge(uint64_t{1} << 63)),
            MaxHitsIs(0), HasNonEmptyBuckets(65)));
}

TEST(LogUniformIntDistributionTest, FullUint64RangeBase4Covers33FullBuckets) {
  // UINT64_MAX == 4^32 - 1, so the top bucket is full (no singleton at max).
  EXPECT_THAT(
      SampleLogUniform<uint64_t>(0, UINT64_MAX, 4, /*trials=*/5000),
      AllOf(AllInBounds(), MinSampleIs(0u), MaxSampleIs(Ge(uint64_t{1} << 62)),
            MaxHitsIs(0), HasNonEmptyBuckets(33)));
}

TEST(LogUniformIntDistributionTest, FullUint64RangeBase3ReachesTopOfRange) {
  // 3^40 < UINT64_MAX < 3^41, so the 42nd bucket spans [3^40, UINT64_MAX].
  EXPECT_THAT(SampleLogUniform<uint64_t>(0, UINT64_MAX, 3, /*trials=*/10000),
              AllOf(AllInBounds(), MinSampleIs(0u),
                    MaxSampleIs(Gt(UINT64_MAX - (UINT64_MAX >> 6))),
                    HasNonEmptyBuckets(42)));
}

TEST(LogUniformIntDistributionTest, MaxBaseFormsThreeEqualBuckets) {
  // Base == UINT64_MAX on [0, UINT64_MAX] forms 3 buckets ({0},
  // [1, UINT64_MAX - 1], and {UINT64_MAX}), each drawn with probability 1/3.
  const auto approx_expected_hits =
      AllOf(Ge(kExpectedBucketHits - kBucketHitTolerance),
            Le(kExpectedBucketHits + kBucketHitTolerance));
  EXPECT_THAT(
      SampleLogUniform<uint64_t>(0, UINT64_MAX, UINT64_MAX, /*trials=*/3000),
      AllOf(AllInBounds(), MinSampleIs(0u), MaxSampleIs(UINT64_MAX),
            MaxHitsIs(approx_expected_hits),
            BucketCountsAre(ElementsAre(approx_expected_hits,
                                        approx_expected_hits,
                                        approx_expected_hits))));
}

TEST(LogUniformIntDistributionTest, SingleValueRangeReturnsMin) {
  EXPECT_THAT(SampleLogUniform<uint64_t>(5, 5, 3, /*trials=*/100),
              AllOf(AllInBounds(), MinSampleIs(5u), MaxSampleIs(5u),
                    BucketCountsAre(ElementsAre(100))));
}

TEST(LogUniformIntDistributionTest, ExactPowerMaxFormsSingletonTopBucket) {
  // 10^15 is the sole element of the 17th bucket (weight 1/17, ~1000 of 17000).
  constexpr int64_t kPow10_15 = static_cast<int64_t>(PowU64(10, 15));
  EXPECT_THAT(
      SampleLogUniform<int64_t>(0, kPow10_15, 10, /*trials=*/17000),
      AllOf(AllInBounds(),
            MaxHitsIs(AllOf(Ge(kExpectedBucketHits - kBucketHitTolerance),
                            Le(kExpectedBucketHits + kBucketHitTolerance))),
            HasNonEmptyBuckets(17)));
}

TEST(LogUniformIntDistributionTest, PowerMinusOneMaxHasNoSingletonTopBucket) {
  // 5^26 - 1 > 2^53 fills the top bucket [5^25, 5^26 - 1] without a singleton.
  constexpr uint64_t kPow5_26Minus1 = PowU64(5, 26) - 1;
  EXPECT_THAT(
      SampleLogUniform<uint64_t>(0, kPow5_26Minus1, 5, /*trials=*/10000),
      AllOf(AllInBounds(), MaxHitsIs(0), HasNonEmptyBuckets(27)));
}

TEST(LogUniformIntDistributionTest,
     LargeUint16BaseAvoidsSignedPromotionOverflow) {
  // Squaring uint16_t(50000) would promote to signed int and overflow INT_MAX.
  const auto approx_expected_hits =
      AllOf(Ge(kExpectedBucketHits - kBucketHitTolerance),
            Le(kExpectedBucketHits + kBucketHitTolerance));
  EXPECT_THAT(SampleLogUniform<uint16_t>(0, UINT16_MAX, 50000, /*trials=*/3000),
              AllOf(AllInBounds(), MinSampleIs(0u), MaxSampleIs(Ge(50000u)),
                    BucketCountsAre(ElementsAre(approx_expected_hits,
                                                approx_expected_hits,
                                                approx_expected_hits))));
}

}  // namespace
