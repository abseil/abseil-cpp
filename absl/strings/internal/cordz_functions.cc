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

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/profiling/internal/exponential_biased.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

// The average interval until the next sample. A value of 0 disables profiling
// while a value of 1 will profile all Cords.
std::atomic<int> g_cordz_mean_interval(50000);

}  // namespace

#ifdef ABSL_INTERNAL_CORDZ_ENABLED

ABSL_CONST_INIT thread_local ExponentialSampler cordz_next_sample;

// kIntervalIfDisabled is the number of profile-eligible events need to occur
// before the code will confirm that cordz is still disabled.
constexpr int64_t kIntervalIfDisabled = 1 << 16;

ABSL_ATTRIBUTE_NOINLINE int64_t
cordz_should_profile_slow(ExponentialSampler& sampler) {

  return sampler.SampleSlow(get_cordz_mean_interval(), /*step=*/1,
                            kIntervalIfDisabled);
}

void cordz_set_next_sample_for_testing(int64_t next_sample) {
  if (next_sample < 0) {
    cordz_next_sample.SetStateForTesting(0, 0);
    return;
  }
  const int32_t interval = get_cordz_mean_interval();
  cordz_next_sample.SetStateForTesting(next_sample > 1 ? next_sample : 1,
                                       interval > 0 ? interval : 1);
}

#endif  // ABSL_INTERNAL_CORDZ_ENABLED

int32_t get_cordz_mean_interval() {
  return g_cordz_mean_interval.load(std::memory_order_acquire);
}

void set_cordz_mean_interval(int32_t mean_interval) {
  g_cordz_mean_interval.store(mean_interval, std::memory_order_release);
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
