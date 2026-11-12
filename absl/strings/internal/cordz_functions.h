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

#ifndef ABSL_STRINGS_INTERNAL_CORDZ_FUNCTIONS_H_
#define ABSL_STRINGS_INTERNAL_CORDZ_FUNCTIONS_H_

#include <stdint.h>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/profiling/internal/exponential_biased.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// Returns the current sample rate. This represents the average interval
// between samples.
int32_t get_cordz_mean_interval();

// Sets the sample rate with the average interval between samples.
void set_cordz_mean_interval(int32_t mean_interval);

// Cordz is only enabled on Linux with thread_local support.
#if defined(ABSL_INTERNAL_CORDZ_ENABLED)
#error ABSL_INTERNAL_CORDZ_ENABLED cannot be set directly
#elif defined(__linux__) && defined(ABSL_HAVE_THREAD_LOCAL)
#define ABSL_INTERNAL_CORDZ_ENABLED 1
#endif

#ifdef ABSL_INTERNAL_CORDZ_ENABLED

using ExponentialSampler = ::absl::profiling_internal::ExponentialSampler;

// cordz_next_sample holds the per-thread sampler tracking the number of events
// until the next Cord is sampled and the sampling period active when that
// countdown started. cordz is only enabled when we can use thread locals.
ABSL_CONST_INIT extern thread_local ExponentialSampler cordz_next_sample;

// Determines if the next sample should be profiled.
// Returns:
//   0: Do not sample
//  >0: Sample with the sampling period active for the completed stride
int64_t cordz_should_profile_slow(ExponentialSampler& sampler);

// Determines if the next sample should be profiled.
// Returns:
//   0: Do not sample
//  >0: Sample with the sampling period active for the completed stride
inline int64_t cordz_should_profile() {
  if (ABSL_PREDICT_TRUE(!cordz_next_sample.ShouldSampleNext(/*step=*/1))) {
    return 0;
  }
  return cordz_should_profile_slow(cordz_next_sample);
}

// Sets the interval until the next sample (for testing only)
void cordz_set_next_sample_for_testing(int64_t next_sample);

#else  // ABSL_INTERNAL_CORDZ_ENABLED

inline int64_t cordz_should_profile() { return 0; }
inline void cordz_set_next_sample_for_testing(int64_t) {}

#endif  // ABSL_INTERNAL_CORDZ_ENABLED

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORDZ_FUNCTIONS_H_
