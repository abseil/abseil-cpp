// Copyright 2019 The Abseil Authors.
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

#include "absl/profiling/internal/exponential_biased.h"

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"

using ::testing::Ge;

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {
namespace {

// Non-positive arguments must return the minimum valid skip (0) or stride (1),
// and extreme arguments must clamp without signed integer overflow.
TEST(ExponentialBiasedTest, NonPositiveParametersAndClamp) {
  ExponentialBiased eb;
  EXPECT_EQ(eb.GetSkipCount(0), 0);
  EXPECT_EQ(eb.GetSkipCount(-5), 0);
  EXPECT_EQ(eb.GetStride(1), 1);
  EXPECT_EQ(eb.GetStride(0), 1);
  EXPECT_EQ(eb.GetStride(-5), 1);
  EXPECT_GE(eb.GetStride(int64_t{1} << 62), 1);
}

// Empirical mean skip count must match the requested mean across scales.
TEST(ExponentialBiasedTest, MeanMatchesRequestedValue) {
  ExponentialBiased eb;
  constexpr int kDraws = 100000;
  for (int64_t mean :
       {int64_t{10}, int64_t{100}, int64_t{1000}, int64_t{1} << 20}) {
    double sum = 0;
    for (int i = 0; i < kDraws; ++i) {
      sum += static_cast<double>(eb.GetSkipCount(mean));
    }
    EXPECT_NEAR(sum / kDraws, static_cast<double>(mean), 0.05 * mean);
  }
}

// Stride frequencies must match the geometric PMF
// `P(S = s) = p * (1 - p)^(s - 1)` across individual buckets, including
// `s = 1`.
TEST(ExponentialBiasedTest, StrideFollowsGeometricDistribution) {
  ExponentialBiased eb;
  constexpr int64_t kPeriod = 10;
  constexpr double kP = 1.0 / kPeriod;
  constexpr int kDraws = 200000;

  int counts[11] = {};
  for (int i = 0; i < kDraws; ++i) {
    int64_t s = eb.GetStride(kPeriod);
    if (s <= 10) ++counts[s];
  }

  double expected_prob = kP;
  for (int s = 1; s <= 10; ++s) {
    EXPECT_NEAR(static_cast<double>(counts[s]) / kDraws, expected_prob, 0.006)
        << "stride = " << s;
    expected_prob *= (1.0 - kP);
  }
}

TEST(ExponentialBiasedTest, FirstAndConsecutiveDrawsAreUnbiased) {
  constexpr int64_t kPeriod = 10;
  constexpr double kExpectedProb = 1.0 / kPeriod;
  constexpr int kTrials = 200000;

  // The very first draw on a new thread/generator must sample immediately
  // (`stride == 1`) with probability `1 / kPeriod`.
  int first_draw_ones = 0;
  for (int i = 0; i < kTrials; ++i) {
    ExponentialBiased fresh;
    if (fresh.GetStride(kPeriod) == 1) ++first_draw_ones;
  }
  EXPECT_NEAR(static_cast<double>(first_draw_ones) / kTrials, kExpectedProb,
              0.01);

  // Because a geometric process is memoryless, the draw immediately following
  // `stride == 1` must still hit `stride == 1` with probability `1 / kPeriod`
  // (no post-sample refractory dip from stateful rounding).
  ExponentialBiased eb;
  int ones = 0;
  int ones_after_one = 0;
  bool prev_was_one = false;
  for (int i = 0; i < kTrials; ++i) {
    bool is_one = (eb.GetStride(kPeriod) == 1);
    if (prev_was_one && is_one) ++ones_after_one;
    if (prev_was_one) ++ones;
    prev_was_one = is_one;
  }
  EXPECT_NEAR(static_cast<double>(ones_after_one) / ones, kExpectedProb, 0.02);
}

// `NextRandom` outputs must stay within 48 bits and distribute uniformly across
// high-bit buckets, which drive the geometric inverse-CDF transform.
TEST(ExponentialBiasedTest, NextRandomIsUniform) {
  uint64_t x = 1;
  for (int i = 0; i < 20; ++i) {
    x = ExponentialBiased::NextRandom(x);
  }

  constexpr int kBuckets = 16;
  constexpr int kDraws = 160000;
  int counts[kBuckets] = {};
  for (int i = 0; i < kDraws; ++i) {
    x = ExponentialBiased::NextRandom(x);
    ASSERT_EQ(x >> ExponentialBiased::kPrngNumBits, 0u);
    ++counts[x >> (ExponentialBiased::kPrngNumBits - 4)];
  }
  for (int count : counts) {
    EXPECT_NEAR(static_cast<double>(count) / kDraws, 1.0 / kBuckets, 0.005);
  }
}

// The generator needs to be available as a thread_local and as a static
// variable.
TEST(ExponentialBiasedTest, InitializationModes) {
  ABSL_CONST_INIT static ExponentialBiased eb_static;
  EXPECT_THAT(eb_static.GetSkipCount(2), Ge(0));

#ifdef ABSL_HAVE_THREAD_LOCAL
  thread_local ExponentialBiased eb_thread;
  EXPECT_THAT(eb_thread.GetSkipCount(2), Ge(0));
#endif

  ExponentialBiased eb_stack;
  EXPECT_THAT(eb_stack.GetSkipCount(2), Ge(0));
}

TEST(ExponentialSamplerTest, InitializationModes) {
  ABSL_CONST_INIT static ExponentialSampler static_sampler;
  EXPECT_EQ(static_sampler.next_sample(), 0);
  EXPECT_EQ(static_sampler.sample_period(), 0);

#ifdef ABSL_HAVE_THREAD_LOCAL
  ABSL_CONST_INIT thread_local ExponentialSampler thread_sampler;
  EXPECT_EQ(thread_sampler.next_sample(), 0);
  EXPECT_EQ(thread_sampler.sample_period(), 0);
#endif
}

TEST(ExponentialSamplerTest, DisabledAndAlwaysOn) {
  ExponentialSampler sampler;

  ASSERT_TRUE(sampler.ShouldSampleNext(/*step=*/1));
  EXPECT_EQ(sampler.SampleSlow(/*period=*/0, /*step=*/1,
                               /*disabled_stride=*/1234),
            0);
  EXPECT_EQ(sampler.next_sample(), 1234);
  EXPECT_EQ(sampler.sample_period(), 0);

  // Period 1 samples every event with weight == step, including event #1.
  sampler.SetStateForTesting(0, 0);
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(sampler.ShouldSampleNext(/*step=*/1));
    EXPECT_EQ(sampler.SampleSlow(/*period=*/1, /*step=*/1,
                                 /*disabled_stride=*/1 << 16),
              1);
  }

  sampler.SetStateForTesting(0, 0);
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(sampler.ShouldSampleNext(/*step=*/65));
    EXPECT_EQ(sampler.SampleSlow(/*period=*/1, /*step=*/65,
                                 /*disabled_stride=*/1 << 16),
              65);
  }

  sampler.DisablePermanently();
  EXPECT_FALSE(sampler.ShouldSampleNext(/*step=*/1));
  EXPECT_EQ(sampler.sample_period(), 0);
}

TEST(ExponentialSamplerTest, UnitStepConstantWeightAndUnbiasedFirstItem) {
  constexpr int64_t kPeriod = 16;
  constexpr int64_t kDisabledStride = 1 << 16;
  constexpr int kTrials = 100000;

  // Item #1 on a fresh thread must be sampled with probability `1 / kPeriod`
  // and receive weight `kPeriod` (never 1).
  int64_t first_item_weight_sum = 0;
  for (int i = 0; i < kTrials; ++i) {
    ExponentialSampler fresh;
    ASSERT_TRUE(fresh.ShouldSampleNext(/*step=*/1));
    const int64_t w = fresh.SampleSlow(kPeriod, /*step=*/1, kDisabledStride);
    if (w > 0) {
      ASSERT_EQ(w, kPeriod);
      first_item_weight_sum += w;
    }
  }
  EXPECT_NEAR(static_cast<double>(first_item_weight_sum), kTrials,
              0.10 * kTrials);

  // Steady-state sequential items must also carry constant weight
  // `kSteadyPeriod`.
  constexpr int64_t kSteadyPeriod = 32;
  constexpr int kEvents = 200000;
  ExponentialSampler sampler;
  int64_t steady_weight_sum = 0;
  for (int i = 0; i < kEvents; ++i) {
    if (sampler.ShouldSampleNext(/*step=*/1)) {
      const int64_t w =
          sampler.SampleSlow(kSteadyPeriod, /*step=*/1, kDisabledStride);
      if (w > 0) {
        ASSERT_EQ(w, kSteadyPeriod);
        steady_weight_sum += w;
      }
    }
  }
  EXPECT_NEAR(static_cast<double>(steady_weight_sum), kEvents, 0.10 * kEvents);
}

TEST(ExponentialSamplerTest, PreservesInFlightPeriodAcrossRateChange) {
  ExponentialSampler sampler;
  sampler.SetStateForTesting(/*next_sample=*/0, /*sample_period=*/100);

  // Completed stride ran under period 100; new stride runs under period 500.
  EXPECT_EQ(sampler.SampleSlow(/*period=*/500, /*step=*/1,
                               /*disabled_stride=*/1 << 16),
            100);
  EXPECT_EQ(sampler.sample_period(), 500);
  EXPECT_GT(sampler.next_sample(), 0);

  sampler.SetStateForTesting(/*next_sample=*/0, sampler.sample_period());
  EXPECT_EQ(sampler.SampleSlow(/*period=*/500, /*step=*/1,
                               /*disabled_stride=*/1 << 16),
            500);
}

TEST(ExponentialSamplerTest, WeightedStepOvershootUnbiased) {
  constexpr int64_t kPeriod = 4096;
  // 2x period: exercises multi-period overshoot.
  constexpr int64_t kSize = 8192;
  constexpr int64_t kStep = kSize + 1;
  constexpr int64_t kDisabledStride = 1 << 20;
  constexpr int kAllocs = 100000;

  ExponentialSampler sampler;
  double estimated_bytes = 0;
  for (int i = 0; i < kAllocs; ++i) {
    if (sampler.ShouldSampleNext(kStep)) {
      const int64_t w = sampler.SampleSlow(kPeriod, kStep, kDisabledStride);
      if (w > 0) {
        EXPECT_GE(w, kPeriod);
        estimated_bytes += static_cast<double>(w) * kSize / kStep;
      }
    }
  }
  const double exact_bytes = static_cast<double>(kAllocs) * kSize;
  EXPECT_NEAR(estimated_bytes, exact_bytes, 0.05 * exact_bytes);
}

}  // namespace
}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl
