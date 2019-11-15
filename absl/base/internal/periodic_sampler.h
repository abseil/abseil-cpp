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

#ifndef ABSL_BASE_INTERNAL_PERIODIC_SAMPLER_H_
#define ABSL_BASE_INTERNAL_PERIODIC_SAMPLER_H_

#include <stdint.h>

#include <atomic>

#include "absl/base/internal/exponential_biased.h"
#include "absl/base/optimization.h"

namespace absl {
namespace base_internal {

// PeriodicSamplerBase provides the basic period sampler implementation.
//
// This is the base class for the templated PeriodicSampler class, which holds
// a global std::atomic value identified by a user defined tag, such that
// each specific PeriodSampler implementation holds its own global period.
//
// PeriodicSamplerBase is thread-compatible except where stated otherwise.
class PeriodicSamplerBase {
 public:
  // PeriodicSamplerBase is trivial / copyable / movable / destructible.
  PeriodicSamplerBase() = default;
  PeriodicSamplerBase(PeriodicSamplerBase&&) = default;
  PeriodicSamplerBase(const PeriodicSamplerBase&) = default;

  // Returns true roughly once every `period` calls. This is established by a
  // randomly picked `stride` that is counted down on each call to `Sample`.
  // This stride is picked such that the probability of `Sample()` returning
  // true is 1 in `period`.
  inline bool Sample() noexcept;

  // The below methods are intended for optimized use cases where the
  // size of the inlined fast path code is highly important. Applications
  // should use the `Sample()` method unless they have proof that their
  // specific use case requires the optimizations offered by these methods.
  //
  // An example of such a use case is SwissTable sampling. All sampling checks
  // are in inlined SwissTable methods, and the number of call sites is huge.
  // In this case, the inlined code size added to each translation unit calling
  // SwissTable methods is non-trivial.
  //
  // The `SubtleMaybeSample()` function spuriously returns true even if the
  // function should not be sampled, applications MUST match each call to
  // 'SubtleMaybeSample()' returning true with a `SubtleConfirmSample()` call,
  // and use the result of the latter as the sampling decision.
  // In other words: the code should logically be equivalent to:
  //
  //    if (SubtleMaybeSample() && SubtleConfirmSample()) {
  //      // Sample this call
  //    }
  //
  // In the 'inline-size' optimized case, the `SubtleConfirmSample()` call can
  // be placed out of line, for example, the typical use case looks as follows:
  //
  //   // --- frobber.h -----------
  //   void FrobberSampled();
  //
  //   inline void FrobberImpl() {
  //     // ...
  //   }
  //
  //   inline void Frobber() {
  //     if (ABSL_PREDICT_FALSE(sampler.SubtleMaybeSample())) {
  //       FrobberSampled();
  //     } else {
  //       FrobberImpl();
  //     }
  //   }
  //
  //   // --- frobber.cc -----------
  //   void FrobberSampled() {
  //     if (!sampler.SubtleConfirmSample())) {
  //       // Spurious false positive
  //       FrobberImpl();
  //       return;
  //     }
  //
  //     // Sampled execution
  //     // ...
  //   }
  inline bool SubtleMaybeSample() noexcept;
  bool SubtleConfirmSample() noexcept;

 protected:
  // We explicitly don't use a virtual destructor as this class is never
  // virtually destroyed, and it keeps the class trivial, which avoids TLS
  // prologue and epilogue code for our TLS instances.
  ~PeriodicSamplerBase() = default;

  // Returns the next stride for our sampler.
  // This function is virtual for testing purposes only.
  virtual int64_t GetExponentialBiased(int period) noexcept;

 private:
  // Returns the current period of this sampler. Thread-safe.
  virtual int period() const noexcept = 0;

  int64_t stride_ = 0;
  ExponentialBiased rng_;
};

inline bool PeriodicSamplerBase::SubtleMaybeSample() noexcept {
  // We explicitly count up and not down, as the compiler does not generate
  // ideal code for counting down. See also https://gcc.godbolt.org/z/FTPDSM
  //
  // With `if (ABSL_PREDICT_FALSE(++stride_ < 0))`
  //    add     QWORD PTR fs:sampler@tpoff+8, 1
  //    jns     .L15
  //    ret
  //
  // With `if (ABSL_PREDICT_FALSE(--stride_ > 0))`
  //    mov     rax, QWORD PTR fs:sampler@tpoff+8
  //    sub     rax, 1
  //    mov     QWORD PTR fs:sampler@tpoff+8, rax
  //    test    rax, rax
  //    jle     .L15
  //    ret
  //    add     QWORD PTR fs:sampler@tpoff+8, 1
  //    jns     .L15
  //    ret
  //
  // --stride >= 0 does work, but makes our logic slightly harder as in that
  // case we have less convenient zero-init and over-run values.
  if (ABSL_PREDICT_FALSE(++stride_ < 0)) {
    return false;
  }
  return true;
}

inline bool PeriodicSamplerBase::Sample() noexcept {
  return ABSL_PREDICT_FALSE(SubtleMaybeSample()) ? SubtleConfirmSample()
                                                 : false;
}

// PeriodicSampler is a concreted periodic sampler implementation.
// The user provided Tag identifies the implementation, and is required to
// isolate the global state of this instance from other instances.
//
// Typical use case:
//
//   struct HashTablezTag {};
//   thread_local PeriodicSampler sampler;
//
//   void HashTableSamplingLogic(...) {
//     if (sampler.Sample()) {
//       HashTableSlowSamplePath(...);
//     }
//   }
//
template <typename Tag, int default_period = 0>
class PeriodicSampler final : public PeriodicSamplerBase {
 public:
  ~PeriodicSampler() = default;

  int period() const noexcept final {
    return period_.load(std::memory_order_relaxed);
  }

  // Sets the global period for this sampler. Thread-safe.
  // Setting a period of 0 disables the sampler, i.e., every call to Sample()
  // will return false. Setting a period of 1 puts the sampler in 'always on'
  // mode, i.e., every call to Sample() returns true.
  static void SetGlobalPeriod(int period) {
    period_.store(period, std::memory_order_relaxed);
  }

 private:
  static std::atomic<int> period_;
};

template <typename Tag, int default_period>
std::atomic<int> PeriodicSampler<Tag, default_period>::period_(default_period);

}  // namespace base_internal
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_PERIODIC_SAMPLER_H_
