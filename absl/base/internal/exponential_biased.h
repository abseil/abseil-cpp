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

#ifndef ABSL_BASE_INTERNAL_EXPONENTIAL_BIASED_H_
#define ABSL_BASE_INTERNAL_EXPONENTIAL_BIASED_H_

#include <stdint.h>

namespace absl {
namespace base_internal {

// ExponentialBiased provides a small and fast random number generator for a
// rounded exponential distribution. This generator doesn't requires very little
// state doesn't impose synchronization overhead, which makes it useful in some
// specialized scenarios.
//
// For the generated variable X, X ~ floor(Exponential(1/mean)). The floor
// operation introduces a small amount of bias, but the distribution is useful
// to generate a wait time. That is, if an operation is supposed to happen on
// average to 1/mean events, then the generated variable X will describe how
// many events to skip before performing the operation and computing a new X.
//
// The mathematically precise distribution to use for integer wait times is a
// Geometric distribution, but a Geometric distribution takes slightly more time
// to compute and when the mean is large (say, 100+), the Geometric distribution
// is hard to distinguish from the result of ExponentialBiased.
//
// This class is thread-compatible.
class ExponentialBiased {
 public:
  // The number of bits set by NextRandom.
  static constexpr int kPrngNumBits = 48;

  // Generates the floor of an exponentially distributed random variable by
  // rounding the value down to the nearest integer. The result will be in the
  // range [0, int64_t max / 2].
  int64_t Get(int64_t mean);

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

}  // namespace base_internal
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_EXPONENTIAL_BIASED_H_
