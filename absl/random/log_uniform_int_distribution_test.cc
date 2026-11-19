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
  absl::random_internal::pcg64_2018_engine rng_{0x2B7E151628AED2A6};
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
  *os << "{all_in_bounds: " << (summary.all_in_bounds ? "true" : "false")
      << ", min_sample: " << summary.min_sample
      << ", max_sample: " << summary.max_sample
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

// Fixed seed for deterministic sampling tests (fractional hex digits of Euler's
// number e, matching LogUniformIntChiSquaredTest above).
constexpr uint64_t kTestSeed = 0x2B7E151628AED2A6ULL;

template <typename IntType>
SampleSummary<IntType> SampleLogUniform(IntType min, IntType max, IntType base,
                                        int trials) {
  using Unsigned = std::make_unsigned_t<IntType>;
  const Unsigned umin = static_cast<Unsigned>(min);
  const Unsigned umax = static_cast<Unsigned>(max);
  const Unsigned ubase = static_cast<Unsigned>(base);
  const auto exact_bucket = [ubase](Unsigned offset) -> size_t {
    if (offset == 0) return 0;
    size_t bucket = 1;
    const Unsigned limit = offset / ubase;
    Unsigned pow = 1;
    while (pow <= limit) {
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
      ++summary.bucket_counts[exact_bucket(static_cast<Unsigned>(v) - umin)];
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
  constexpr int32_t kMaxI32 = (std::numeric_limits<int32_t>::max)();
  // [-10, INT32_MAX] spans > INT32_MAX across 11 base-10 buckets (e in 0..10),
  // with the top bucket covering [10^9 - 10, INT32_MAX].
  EXPECT_THAT(SampleLogUniform<int32_t>(-10, kMaxI32, 10, /*trials=*/5000),
              AllOf(AllInBounds(), MinSampleIs(-10),
                    MaxSampleIs(Gt(kMaxI32 / 2)), HasNonEmptyBuckets(11)));
}

TEST(LogUniformIntDistributionTest, FullInt64RangeCoversAllBuckets) {
  constexpr int64_t kMinI64 = (std::numeric_limits<int64_t>::min)();
  constexpr int64_t kMaxI64 = (std::numeric_limits<int64_t>::max)();
  // [INT64_MIN, INT64_MAX] spans 2^64 - 1 across 21 base-10 buckets (e in
  // 0..20), with the top bucket covering [INT64_MIN + 10^19, INT64_MAX].
  EXPECT_THAT(SampleLogUniform<int64_t>(kMinI64, kMaxI64, 10, /*trials=*/5000),
              AllOf(AllInBounds(), MinSampleIs(kMinI64),
                    MaxSampleIs(Gt(kMaxI64 / 2)), HasNonEmptyBuckets(21)));
}

TEST(LogUniformIntDistributionTest, FullUint64RangeBase2Covers65FullBuckets) {
  constexpr uint64_t kMaxU64 = (std::numeric_limits<uint64_t>::max)();
  // UINT64_MAX == 2^64 - 1 forms 65 full base-2 buckets (e in 0..64) with top
  // bucket [2^63, UINT64_MAX] and no singleton spike at UINT64_MAX.
  EXPECT_THAT(
      SampleLogUniform<uint64_t>(0, kMaxU64, 2, /*trials=*/10000),
      AllOf(AllInBounds(), MinSampleIs(0u), MaxSampleIs(Ge(uint64_t{1} << 63)),
            MaxHitsIs(0), HasNonEmptyBuckets(65)));
}

TEST(LogUniformIntDistributionTest, FullUint64RangeBase4Covers33FullBuckets) {
  constexpr uint64_t kMaxU64 = (std::numeric_limits<uint64_t>::max)();
  // UINT64_MAX == 4^32 - 1 forms 33 full base-4 buckets (e in 0..32) with no
  // singleton spike at UINT64_MAX.
  EXPECT_THAT(
      SampleLogUniform<uint64_t>(0, kMaxU64, 4, /*trials=*/5000),
      AllOf(AllInBounds(), MinSampleIs(0u), MaxSampleIs(Ge(uint64_t{1} << 62)),
            MaxHitsIs(0), HasNonEmptyBuckets(33)));
}

TEST(LogUniformIntDistributionTest, FullUint64RangeBase3ReachesTopOfRange) {
  constexpr uint64_t kMaxU64 = (std::numeric_limits<uint64_t>::max)();
  // Base 3 on [0, UINT64_MAX] has 42 buckets (3^40 < UINT64_MAX < 3^41), with
  // the top bucket [3^40, UINT64_MAX] extending all the way to UINT64_MAX.
  EXPECT_THAT(
      SampleLogUniform<uint64_t>(0, kMaxU64, 3, /*trials=*/10000),
      AllOf(AllInBounds(), MinSampleIs(0u),
            MaxSampleIs(Gt(kMaxU64 - (kMaxU64 >> 6))), HasNonEmptyBuckets(42)));
}

TEST(LogUniformIntDistributionTest, MaxBaseFormsThreeEqualBuckets) {
  constexpr uint64_t kMaxU64 = (std::numeric_limits<uint64_t>::max)();
  // Base == UINT64_MAX on [0, UINT64_MAX] forms 3 buckets ({0},
  // [1, UINT64_MAX - 1], and {UINT64_MAX}), each drawn with probability 1/3.
  const auto approx_one_third = AllOf(Ge(850), Le(1150));
  EXPECT_THAT(
      SampleLogUniform<uint64_t>(0, kMaxU64, kMaxU64, /*trials=*/3000),
      AllOf(AllInBounds(), MinSampleIs(0u), MaxSampleIs(kMaxU64),
            MaxHitsIs(approx_one_third),
            BucketCountsAre(ElementsAre(approx_one_third, approx_one_third,
                                        approx_one_third))));
}

TEST(LogUniformIntDistributionTest, SingleValueRangeReturnsMin) {
  // Empty range (min == max) with base > 2 always returns min.
  EXPECT_THAT(SampleLogUniform<uint64_t>(5, 5, 3, /*trials=*/100),
              AllOf(AllInBounds(), MinSampleIs(5u), MaxSampleIs(5u),
                    BucketCountsAre(ElementsAre(100))));
}

TEST(LogUniformIntDistributionTest, ExactPowerMaxFormsSingletonTopBucket) {
  // On [0, 10^15] in base 10, 10^15 starts bucket e = 16 as a singleton
  // {10^15}, receiving 1/17 of draws (~1000 of 17000).
  constexpr int64_t kPow10_15 = 1'000'000'000'000'000LL;
  EXPECT_THAT(SampleLogUniform<int64_t>(0, kPow10_15, 10, /*trials=*/17000),
              AllOf(AllInBounds(), MaxHitsIs(AllOf(Ge(850), Le(1150))),
                    HasNonEmptyBuckets(17)));
}

TEST(LogUniformIntDistributionTest, PowerMinusOneMaxHasNoSingletonTopBucket) {
  // On [0, 5^26 - 1] in base 5 (> 2^53), bucket e = 26 spans [5^25, 5^26 - 1]
  // in full, with no singleton bucket at max.
  constexpr uint64_t kPow5_26Minus1 = 1'490'116'119'384'765'625ULL - 1;
  EXPECT_THAT(
      SampleLogUniform<uint64_t>(0, kPow5_26Minus1, 5, /*trials=*/10000),
      AllOf(AllInBounds(), MaxHitsIs(0), HasNonEmptyBuckets(27)));
}

}  // namespace
