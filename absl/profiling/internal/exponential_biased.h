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

#include "absl/base/config.h"
#include "absl/base/macros.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {

// ExponentialBiased provides a small and fast random number generator for a
// memoryless geometric distribution (the discrete analog of an exponential
// distribution). This generator manages very little state, and imposes no
// synchronization overhead. This makes it useful in specialized scenarios
// requiring minimum overhead, such as stride-based periodic sampling.
//
// ExponentialBiased provides two closely related functions, GetSkipCount() and
// GetStride(), returning the number of events to skip (`>= 0`) or the number
// of events required (`>= 1`) before an event with a given mean occurs.
//
// The distribution is useful to generate a random wait time or some periodic
// event with a given mean probability. For example, if an action is supposed to
// happen on average once every 'N' events, then we can get a random 'stride'
// counting down how long before the event to happen. For example, if we'd want
// to sample one in every 1000 'Frobber' calls, our code could look like this:
//
//   Frobber::Frobber() {
//     stride_ = exponential_biased_.GetStride(1000);
//   }
//
//   void Frobber::Frob(int arg) {
//     if (--stride_ == 0) {
//       SampleFrob(arg);
//       stride_ = exponential_biased_.GetStride(1000);
//     }
//     ...
//   }
//
// `GetSkipCount(mean)` evaluates the exact geometric inverse CDF
// `floor(ln(u) / ln(1 - 1 / (mean + 1)))` (`~= floor(-ln(u) * (mean + 0.5))`):
//   * Truncating `-ln(u) * mean` directly (`floor`) would underestimate the
//     mean by ~0.5, which is significant for small means.
//   * Rounding `-ln(u) * mean` to the nearest integer (`std::rint`) with a
//     cumulative rounding-error term gives `0` (`stride == 1`) a half-width
//     bin `[0, 0.5)` while every `k >= 1` has width `1.0`, halving the
//     probability of `skip == 0` (`stride == 1`) to `~0.5 / (mean + 1)` on a
//     fresh generator and immediately after each sampled event.
// Using `floor` with the exact geometric scale `1 / -ln(1 - 1 / (mean + 1))`
// gives every integer `k >= 0` a full-width geometric bin, exact expectation
// `mean`, exact `P(GetSkipCount(mean) == 0) = 1 / (mean + 1)` (`P(GetStride(N)
// == 1) = 1 / N`), and independent memoryless draws without extra state.
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

  // GetStride() returns the number of events required for a specific event to
  // happen. See the class comments for a usage example. `GetStride()` is
  // equivalent to `GetSkipCount(mean - 1) + 1`. When to use `GetStride()` or
  // `GetSkipCount()` depends mostly on what best fits the use case.
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

}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_PROFILING_INTERNAL_EXPONENTIAL_BIASED_H_
