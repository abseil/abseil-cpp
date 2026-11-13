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

#include <stdint.h>

#include <atomic>
#include <cmath>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {

// Generates a memoryless geometric skip count on {0, 1, 2, ...} by applying
// `floor(-ln(u) * (mean + 0.5))` to a uniform variate `u` in `(0, 1)`. The
// `+0.5` scale shift cancels the `-0.5` average fractional part discarded by
// `floor` (`E[skip] = mean + 1 / (12 * (mean + 0.5)) + O(mean^-3)`).
int64_t ExponentialBiased::GetSkipCount(int64_t mean) {
  if (ABSL_PREDICT_FALSE(mean <= 0)) {
    return 0;
  }
  if (ABSL_PREDICT_FALSE(!initialized_)) {
    Initialize();
  }

  rng_ = NextRandom(rng_);

  // Map the 48-bit PRNG output to the midpoint of its uniform bin in (0, 1).
  // Every midpoint `(rng_ + 0.5) * 2^-48` has at most 49 significand bits and
  // is therefore represented without rounding in IEEE-754 `double`.
  constexpr double kInvPrngMod =
      1.0 / static_cast<double>(uint64_t{1} << kPrngNumBits);
  const double u = (static_cast<double>(rng_) + 0.5) * kInvPrngMod;
  const double interval = -std::log(u) * (static_cast<double>(mean) + 0.5);

  // Clamp extreme values so casting `interval` to `int64_t` (and adding 1 in
  // `GetStride()`) cannot overflow. `1 << 62` is an exact power of two in
  // IEEE-754 `double`.
  constexpr int64_t kMaxSkip = int64_t{1} << 62;
  if (ABSL_PREDICT_FALSE(interval >= static_cast<double>(kMaxSkip))) {
    return kMaxSkip;
  }
  return static_cast<int64_t>(interval);
}

int64_t ExponentialBiased::GetStride(int64_t mean) {
  if (ABSL_PREDICT_FALSE(mean <= 1)) {
    return 1;
  }
  return GetSkipCount(mean - 1) + 1;
}

void ExponentialBiased::Initialize() {
  // We don't get well distributed numbers from `this` so we call NextRandom() a
  // bunch to mush the bits around. We use a global_rand to handle the case
  // where the same thread (by memory address) gets created and destroyed
  // repeatedly.
  ABSL_CONST_INIT static std::atomic<uint32_t> global_rand(0);
  uint64_t r = reinterpret_cast<uint64_t>(this) +
               global_rand.fetch_add(1, std::memory_order_relaxed);
  for (int i = 0; i < 20; ++i) {
    r = NextRandom(r);
  }
  rng_ = r;
  initialized_ = true;
}

}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl
