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

#ifndef ABSL_PROFILING_INTERNAL_EXPONENTIAL_BIASED_H_
#define ABSL_PROFILING_INTERNAL_EXPONENTIAL_BIASED_H_

#include <stdint.h>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {

// ExponentialBiased provides a small, fast, synchronization-free random number
// generator for a memoryless geometric distribution (the discrete analog of an
// exponential distribution):
//
//   ExponentialBiased rng;
//   int64_t skip = rng.GetSkipCount(mean);  // in {0, 1, ...}, E[skip] ~ mean
//   int64_t stride = rng.GetStride(period); // in {1, 2, ...}, P(1) ~ 1/period
//
// For periodic sampling with sample weights, prefer `ExponentialSampler`.
//
// This class is thread-compatible.
class ExponentialBiased {
 public:
  // The number of bits set by NextRandom.
  static constexpr int kPrngNumBits = 48;

  // `GetSkipCount()` returns the number of events to skip before some chosen
  // event happens. For example, randomly tossing a coin, we will on average
  // throw heads once before we get tails. We can simulate random coin tosses
  // using GetSkipCount() as:
  //
  //   ExponentialBiased eb;
  //   for (...) {
  //     int number_of_heads_before_tail = eb.GetSkipCount(1);
  //     for (int flips = 0; flips < number_of_heads_before_tail; ++flips) {
  //       printf("head...");
  //     }
  //     printf("tail\n");
  //   }
  //
  int64_t GetSkipCount(int64_t mean);

  // GetStride() returns the number of events required (`>= 1`) for a specific
  // event with a given mean period to happen. `GetStride(mean)` is equivalent
  // to `GetSkipCount(mean - 1) + 1`.
  int64_t GetStride(int64_t mean);

  // Computes a random number in the range [0, 1<<(kPrngNumBits+1) - 1]
  //
  // This is public to enable testing.
  static uint64_t NextRandom(uint64_t rnd);

 private:
  void Initialize();

  uint64_t rng_{0};
  bool initialized_{false};
};

// Returns the next prng value.
// pRNG is: aX+b mod c with a = 0x5DEECE66D, b =  0xB, c = 1<<48
// This is the lrand64 generator.
inline uint64_t ExponentialBiased::NextRandom(uint64_t rnd) {
  const uint64_t prng_mult = uint64_t{0x5DEECE66D};
  const uint64_t prng_add = 0xB;
  const uint64_t prng_mod_power = 48;
  const uint64_t prng_mod_mask =
      ~((~static_cast<uint64_t>(0)) << prng_mod_power);
  return (prng_mult * rnd + prng_add) & prng_mod_mask;
}

// Per-thread periodic sampler that pairs an inline fast-path countdown with an
// out-of-line slow path returning unbiased sample weights.
//
// `ExponentialSampler` is constant-initializable and trivially destructible so
// it can be declared `thread_local` with `ABSL_CONST_INIT` (or
// `ABSL_PER_THREAD_TLS_KEYWORD`) without dynamic TLS initialization guards.
//
// Typical usage splits the fast decrement into a header and the slow path into
// a `.cc` file:
//
//   // frobber.h
//   ABSL_CONST_INIT extern thread_local ExponentialSampler frob_sampler;
//   void SampleFrobSlow(int arg);
//
//   inline void Frob(int arg) {
//     if (ABSL_PREDICT_FALSE(frob_sampler.ShouldSampleNext(/*step=*/1))) {
//       SampleFrobSlow(arg);
//     }
//     ...
//   }
//
//   // frobber.cc
//   ABSL_CONST_INIT thread_local ExponentialSampler frob_sampler;
//
//   void SampleFrobSlow(int arg) {
//     int64_t weight = frob_sampler.SampleSlow(
//         GetFrobSamplePeriod(), /*step=*/1, /*disabled_stride=*/1 << 16);
//     if (weight > 0) {
//       RecordFrobSample(arg, weight);
//     }
//   }
//
// This class is thread-compatible (typically one instance per thread in TLS).
class ExponentialSampler {
 public:
  constexpr ExponentialSampler() = default;

  // Subtracts `step` from the pending countdown and returns true when the slow
  // path (`SampleSlow()`) must be called.
  //
  // `step` is the weight of the current event: `1` for unweighted event/object
  // sampling (e.g. one Cord or hash table), or `size + 1` for size-weighted
  // sampling (e.g. an allocation of `size` bytes, where `+ 1` ensures zero-byte
  // events still advance the countdown).
  ABSL_ATTRIBUTE_ALWAYS_INLINE bool ShouldSampleNext(int64_t step) {
    next_sample_ -= step;
    return ABSL_PREDICT_FALSE(next_sample_ <= 0);
  }

  // Slow-path companion to `ShouldSampleNext()`, which also triggers on a
  // thread's first event (since `next_sample_` is zero-initialized in TLS) and
  // when `disabled_stride` expires while `period <= 0`. Pass the same `step`
  // that was passed to `ShouldSampleNext()`.
  //
  // Returns 0 if the event is not sampled, or the positive sample weight
  // (`sample_period` plus any multi-unit overshoot past zero) if it is sampled.
  //
  // When `period <= 0`, sampling is treated as disabled: the countdown is set
  // to `disabled_stride` so the fast path skips that many units in TLS before
  // re-checking `period` on the slow path, and 0 is returned.
  int64_t SampleSlow(int64_t period, int64_t step, int64_t disabled_stride);

  // Permanently disables sampling on this instance by setting the countdown to
  // `INT64_MAX`.
  void DisablePermanently();

  // Overrides the pending countdown and active period without reseeding `rng_`.
  void SetStateForTesting(int64_t next_sample, int64_t sample_period) {
    next_sample_ = next_sample;
    sample_period_ = sample_period;
  }

  int64_t next_sample() const { return next_sample_; }
  int64_t sample_period() const { return sample_period_; }

 private:
  int64_t next_sample_ = 0;
  int64_t sample_period_ = 0;
  ExponentialBiased rng_;
};

}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_PROFILING_INTERNAL_EXPONENTIAL_BIASED_H_
