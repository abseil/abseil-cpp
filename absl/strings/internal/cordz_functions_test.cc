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

#include "absl/strings/internal/cordz_functions.h"

#include <cstdint>
#include <thread>  // NOLINT we need real clean new threads

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::testing::Eq;
using ::testing::Ge;
using ::testing::Le;

TEST(CordzFunctionsTest, SampleRate) {
  int32_t orig_sample_rate = get_cordz_mean_interval();
  int32_t expected_sample_rate = 123;
  set_cordz_mean_interval(expected_sample_rate);
  EXPECT_THAT(get_cordz_mean_interval(), Eq(expected_sample_rate));
  set_cordz_mean_interval(orig_sample_rate);
}

// Cordz is disabled when we don't have thread_local. All calls to
// should_profile will return false when cordz is disabled, so we might want to
// avoid those tests.
#ifdef ABSL_INTERNAL_CORDZ_ENABLED

TEST(CordzFunctionsTest, ShouldProfileDisable) {
  int32_t orig_sample_rate = get_cordz_mean_interval();

  set_cordz_mean_interval(0);
  cordz_set_next_sample_for_testing(0);
  EXPECT_EQ(cordz_should_profile(), 0);
  // 1 << 16 is from kIntervalIfDisabled in cordz_functions.cc.
  EXPECT_THAT(cordz_next_sample.next_sample(), Eq(1 << 16));

  set_cordz_mean_interval(orig_sample_rate);
}

TEST(CordzFunctionsTest, ShouldProfileAlways) {
  int32_t orig_sample_rate = get_cordz_mean_interval();

  set_cordz_mean_interval(1);
  cordz_set_next_sample_for_testing(1);
  EXPECT_GT(cordz_should_profile(), 0);
  EXPECT_THAT(cordz_next_sample.next_sample(), Le(1));

  set_cordz_mean_interval(orig_sample_rate);
}

TEST(CordzFunctionsTest, DoesNotAlwaysSampleFirstCord) {
  // Set large enough interval such that the chance of 'tons' of threads
  // randomly sampling the first call is infinitely small.
  set_cordz_mean_interval(10000);
  int tries = 0;
  bool sampled = false;
  do {
    ++tries;
    ASSERT_THAT(tries, Le(1000));
    std::thread thread([&sampled] { sampled = cordz_should_profile() > 0; });
    thread.join();
  } while (sampled);
}

TEST(CordzFunctionsTest, ShouldProfileRate) {
  static constexpr int kDesiredMeanInterval = 1000;
  static constexpr int kSamples = 10000;
  int32_t orig_sample_rate = get_cordz_mean_interval();

  set_cordz_mean_interval(kDesiredMeanInterval);

  int64_t sum_of_intervals = 0;
  for (int i = 0; i < kSamples; i++) {
    // Setting next_sample to 0 will force cordz_should_profile to generate a
    // new value for next_sample each iteration.
    cordz_set_next_sample_for_testing(0);
    cordz_should_profile();
    sum_of_intervals += cordz_next_sample.next_sample();
  }

  // The sum of independent exponential variables is an Erlang distribution,
  // which is a gamma distribution where the shape parameter is equal to the
  // number of summands. The distribution used for cordz_should_profile is
  // actually floor(Exponential(1/mean)) which introduces bias. However, we can
  // apply the squint-really-hard correction factor. That is, when mean is
  // large, then if we squint really hard the shape of the distribution between
  // N and N+1 looks like a uniform distribution. On average, each value for
  // next_sample will be about 0.5 lower than we would expect from an
  // exponential distribution. This squint-really-hard correction approach won't
  // work when mean is smaller than about 10 but works fine when mean is 1000.
  //
  // We can use R to calculate a confidence interval. This
  // shows how to generate a confidence interval with a false positive rate of
  // one in a billion.
  //
  // $ R -q
  // > mean = 1000
  // > kSamples = 10000
  // > errorRate = 1e-9
  // > correction = -kSamples / 2
  // > low = qgamma(errorRate/2, kSamples, 1/mean) + correction
  // > high = qgamma(1 - errorRate/2, kSamples, 1/mean) + correction
  // > low
  // [1] 9396115
  // > high
  // [1] 10618100
  EXPECT_THAT(sum_of_intervals, Ge(9396115));
  EXPECT_THAT(sum_of_intervals, Le(10618100));

  set_cordz_mean_interval(orig_sample_rate);
}

TEST(CordzFunctionsTest, SampleWeightEqualsMeanInterval) {
  const int32_t orig_sample_rate = get_cordz_mean_interval();
  constexpr int32_t kInterval = 16;
  constexpr int kTrials = 100000;
  set_cordz_mean_interval(kInterval);

  // Both the first Cord on a fresh thread and steady-state Cords must return
  // constant weight `kInterval` and sum to `kTrials` in expectation.
  int64_t first_cord_weight_sum = 0;
  for (int i = 0; i < kTrials; ++i) {
    cordz_set_next_sample_for_testing(-1);
    const int64_t w = cordz_should_profile();
    if (w > 0) {
      ASSERT_EQ(w, kInterval);
      first_cord_weight_sum += w;
    }
  }
  EXPECT_NEAR(static_cast<double>(first_cord_weight_sum), kTrials,
              0.10 * kTrials);

  cordz_set_next_sample_for_testing(-1);
  int64_t steady_weight_sum = 0;
  for (int i = 0; i < kTrials; ++i) {
    const int64_t w = cordz_should_profile();
    if (w > 0) {
      ASSERT_EQ(w, kInterval);
      steady_weight_sum += w;
    }
  }
  EXPECT_NEAR(static_cast<double>(steady_weight_sum), kTrials, 0.10 * kTrials);

  set_cordz_mean_interval(orig_sample_rate);
}

#else  // ABSL_INTERNAL_CORDZ_ENABLED

TEST(CordzFunctionsTest, ShouldProfileDisabled) {
  int32_t orig_sample_rate = get_cordz_mean_interval();

  set_cordz_mean_interval(1);
  cordz_set_next_sample_for_testing(0);
  EXPECT_FALSE(cordz_should_profile());

  set_cordz_mean_interval(orig_sample_rate);
}

#endif  // ABSL_INTERNAL_CORDZ_ENABLED

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
