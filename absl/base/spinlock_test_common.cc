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

// A bunch of threads repeatedly hash an array of ints protected by a
// spinlock.  If the spinlock is working properly, all elements of the
// array should be equal at the end of the test.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>  // NOLINT(build/c++11)
#include <random>
#include <thread>  // NOLINT(build/c++11)
#include <type_traits>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/low_level_scheduling.h"
#include "absl/base/internal/scheduling_mode.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/base/macros.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"

constexpr uint32_t kNumThreads = 10;
constexpr int32_t kIters = 1000;

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// This is defined outside of anonymous namespace so that it can be
// a friend of SpinLock to access protected methods for testing.
struct SpinLockTest {
  static uint32_t EncodeWaitStart(int64_t now) {
    return SpinLock::EncodeWaitStart(now);
  }
  static int64_t DecodeWaitCycles(uint32_t lock_value, int64_t now) {
    return SpinLock::DecodeWaitCycles(lock_value, now);
  }

  static bool IsCooperative(const SpinLock& l) { return l.IsCooperative(); }
};

namespace {

static constexpr size_t kArrayLength = 10;
static uint32_t values[kArrayLength];

ABSL_CONST_INIT static SpinLock static_cooperative_spinlock;
ABSL_CONST_INIT static SpinLock static_noncooperative_spinlock(
    base_internal::SCHEDULE_KERNEL_ONLY);

// Simple integer hash function based on the public domain lookup2 hash.
// http://burtleburtle.net/bob/c/lookup2.c
static uint32_t Hash32(uint32_t a, uint32_t c) {
  uint32_t b = 0x9e3779b9UL;  // The golden ratio; an arbitrary value.
  a -= b;
  a -= c;
  a ^= (c >> 13);
  b -= c;
  b -= a;
  b ^= (a << 8);
  c -= a;
  c -= b;
  c ^= (b >> 13);
  a -= b;
  a -= c;
  a ^= (c >> 12);
  b -= c;
  b -= a;
  b ^= (a << 16);
  c -= a;
  c -= b;
  c ^= (b >> 5);
  a -= b;
  a -= c;
  a ^= (c >> 3);
  b -= c;
  b -= a;
  b ^= (a << 10);
  c -= a;
  c -= b;
  c ^= (b >> 15);
  return c;
}

static void TestFunction(uint32_t thread_salt, SpinLock* spinlock) {
  for (int i = 0; i < kIters; i++) {
    SpinLockHolder h(*spinlock);
    for (size_t j = 0; j < kArrayLength; j++) {
      const size_t index = (j + thread_salt) % kArrayLength;
      values[index] = Hash32(values[index], thread_salt);
      std::this_thread::yield();
    }
  }
}

static void ThreadedTest(SpinLock* spinlock) {
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (uint32_t i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread(TestFunction, i, spinlock));
  }
  for (auto& thread : threads) {
    thread.join();
  }

  SpinLockHolder h(*spinlock);
  for (size_t i = 1; i < kArrayLength; i++) {
    EXPECT_EQ(values[0], values[i]);
  }
}

#ifndef ABSL_HAVE_THREAD_SANITIZER
static_assert(std::is_trivially_destructible<SpinLock>());
#endif

TEST(SpinLock, StackNonCooperativeDisablesScheduling) {
  SpinLock spinlock(base_internal::SCHEDULE_KERNEL_ONLY);
  SpinLockHolder l(spinlock);
  EXPECT_FALSE(base_internal::SchedulingGuard::ReschedulingIsAllowed());
}

TEST(SpinLock, StaticNonCooperativeDisablesScheduling) {
  SpinLockHolder l(static_noncooperative_spinlock);
  EXPECT_FALSE(base_internal::SchedulingGuard::ReschedulingIsAllowed());
}

TEST(SpinLock, WaitStartEncoding) {
  // These are implementation details not exported by SpinLock.
  const int kProfileTimestampShift = 7;
  const int kLockwordReservedShift = 3;

  // The encoded timestamp wraps around at 2^kWrapShift cycles, and the lower
  // kProfileTimestampShift bits are dropped.
  const int kWrapShift = 32 - kLockwordReservedShift + kProfileTimestampShift;
  const int64_t step = (1 << kProfileTimestampShift);
  const int64_t kMaxCycles = (int64_t{1} << kWrapShift) - step;

  // These bits should be zero after encoding.
  const uint32_t kLockwordReservedMask = (1 << kLockwordReservedShift) - 1;

  // These bits are dropped when timestamps are encoded.
  const int64_t kProfileTimestampMask = (1 << kProfileTimestampShift) - 1;

  // Test a bunch of random values
  std::default_random_engine generator;
  // Shift to avoid overflow below.
  std::uniform_int_distribution<int64_t> time_distribution(
      0, std::numeric_limits<int64_t>::max() >> 3);
  // Leave room for the rounding of start_time and end_time to differ.
  std::uniform_int_distribution<int64_t> cycle_distribution(0,
                                                            kMaxCycles - step);

  for (int i = 0; i < 100; i++) {
    int64_t start_time = time_distribution(generator);
    int64_t cycles = cycle_distribution(generator);
    int64_t end_time = start_time + cycles;
    uint32_t lock_value = SpinLockTest::EncodeWaitStart(start_time);
    EXPECT_EQ(0u, lock_value & kLockwordReservedMask);
    EXPECT_NE(0u, lock_value);
    if (lock_value == (1u << kLockwordReservedShift)) {
      // A timestamp whose truncation is zero encodes as one, costing one unit
      // of precision; skip the exact comparison below.
      continue;
    }
    int64_t decoded = SpinLockTest::DecodeWaitCycles(lock_value, end_time);
    EXPECT_EQ(0, decoded & kProfileTimestampMask);
    EXPECT_EQ((end_time >> kProfileTimestampShift) -
                  (start_time >> kProfileTimestampShift),
              decoded >> kProfileTimestampShift);
    EXPECT_LT(decoded, cycles + step);
    EXPECT_GT(decoded, cycles - step);
  }

  // Test corner cases
  int64_t start_time = time_distribution(generator);
  start_time &= ~kProfileTimestampMask;
  uint32_t lock_value = SpinLockTest::EncodeWaitStart(start_time);
  EXPECT_EQ(0, SpinLockTest::DecodeWaitCycles(lock_value, start_time));
  EXPECT_EQ(step,
            SpinLockTest::DecodeWaitCycles(lock_value, start_time + step));
  EXPECT_EQ(kMaxCycles, SpinLockTest::DecodeWaitCycles(
                            lock_value, start_time + kMaxCycles));
  // Once the clock wraps all the way around, the elapsed time wraps to zero.
  EXPECT_EQ(0, SpinLockTest::DecodeWaitCycles(lock_value,
                                              start_time + kMaxCycles + step));
  EXPECT_EQ(step, SpinLockTest::DecodeWaitCycles(
                      lock_value, start_time + kMaxCycles + 2 * step));

  // A timestamp whose truncation is zero must not encode as "no waiter", and
  // decoding in the same tick must return zero rather than underflowing.
  EXPECT_NE(0u, SpinLockTest::EncodeWaitStart(0));
  EXPECT_EQ(0, SpinLockTest::DecodeWaitCycles(SpinLockTest::EncodeWaitStart(0),
                                              0));
  EXPECT_NE(0u, SpinLockTest::EncodeWaitStart(int64_t{1} << kWrapShift));
  EXPECT_EQ(0, SpinLockTest::DecodeWaitCycles(
                   SpinLockTest::EncodeWaitStart(int64_t{1} << kWrapShift),
                   int64_t{1} << kWrapShift));
}

TEST(SpinLockWithThreads, StackSpinLock) {
  SpinLock spinlock;
  ThreadedTest(&spinlock);
}

TEST(SpinLockWithThreads, StackCooperativeSpinLock) {
  SpinLock spinlock;
  ThreadedTest(&spinlock);
}

TEST(SpinLockWithThreads, StackNonCooperativeSpinLock) {
  SpinLock spinlock(base_internal::SCHEDULE_KERNEL_ONLY);
  ThreadedTest(&spinlock);
}

TEST(SpinLockWithThreads, StaticCooperativeSpinLock) {
  ThreadedTest(&static_cooperative_spinlock);
}

TEST(SpinLockWithThreads, StaticNonCooperativeSpinLock) {
  ThreadedTest(&static_noncooperative_spinlock);
}

TEST(SpinLockWithThreads, DoesNotDeadlock) {
  struct Helper {
    static void NotifyThenLock(Notification* locked, SpinLock* spinlock,
                               BlockingCounter* b) {
      locked->WaitForNotification();  // Wait for LockThenWait() to hold "s".
      b->DecrementCount();
      SpinLockHolder l(*spinlock);
    }

    static void LockThenWait(Notification* locked, SpinLock* spinlock,
                             BlockingCounter* b) {
      SpinLockHolder l(*spinlock);
      locked->Notify();
      b->Wait();
    }

    static void DeadlockTest(SpinLock* spinlock, int num_spinners) {
      Notification locked;
      BlockingCounter counter(num_spinners);
      std::vector<std::thread> threads;

      threads.push_back(
          std::thread(Helper::LockThenWait, &locked, spinlock, &counter));
      for (int i = 0; i < num_spinners; ++i) {
        threads.push_back(
            std::thread(Helper::NotifyThenLock, &locked, spinlock, &counter));
      }

      for (auto& thread : threads) {
        thread.join();
      }
    }
  };

  int num_threads = base_internal::NumCPUs() * 2;
  SpinLock stack_cooperative_spinlock;
  SpinLock stack_noncooperative_spinlock(base_internal::SCHEDULE_KERNEL_ONLY);
  Helper::DeadlockTest(&stack_cooperative_spinlock, num_threads);
  Helper::DeadlockTest(&stack_noncooperative_spinlock, num_threads);
  Helper::DeadlockTest(&static_cooperative_spinlock, num_threads);
  Helper::DeadlockTest(&static_noncooperative_spinlock, num_threads);
}

TEST(SpinLockTest, IsCooperative) {
  SpinLock default_constructor;
  EXPECT_TRUE(SpinLockTest::IsCooperative(default_constructor));

  SpinLock cooperative;
  EXPECT_TRUE(SpinLockTest::IsCooperative(cooperative));

  SpinLock kernel_only(base_internal::SCHEDULE_KERNEL_ONLY);
  EXPECT_FALSE(SpinLockTest::IsCooperative(kernel_only));
}

TEST(SpinLockTest, ScopedLock) {
  SpinLock s;
  std::scoped_lock l(s);
}

TEST(SpinLockTest, ComputeSpinIterationsScalesWithCapacityAndLoopSpeed) {
  constexpr int kNumCpus = 80;
  constexpr int64_t kRefLatencyCycles = 4000;
  constexpr int64_t kSlowCoreLatencyCycles = 6000;
  constexpr int64_t kFastCoreLatencyCycles = 3000;
  constexpr int64_t kOneCyclePerSpinIter = kProbeSteps;

  // In the unclamped regime, doubling NumCPUs() halves the target cycle budget.
  const int base_iters =
      ComputeSpinIterations(kNumCpus, kRefLatencyCycles, kOneCyclePerSpinIter);
  const int double_cpu_iters = ComputeSpinIterations(
      2 * kNumCpus, kRefLatencyCycles, kOneCyclePerSpinIter);
  EXPECT_GT(double_cpu_iters, kMinSpinIterations);
  EXPECT_LT(base_iters, kMaxSpinIterations);
  EXPECT_NEAR(base_iters, 2 * double_cpu_iters, 1);

  // Faster cores (fewer latency-probe cycles) get a smaller spin budget at the
  // same CPU count and spin-loop speed.
  const int slow_core = ComputeSpinIterations(kNumCpus, kSlowCoreLatencyCycles,
                                              kOneCyclePerSpinIter);
  const int fast_core = ComputeSpinIterations(kNumCpus, kFastCoreLatencyCycles,
                                              kOneCyclePerSpinIter);
  EXPECT_LT(slow_core, kMaxSpinIterations);
  EXPECT_GT(slow_core, base_iters);
  EXPECT_GT(base_iters, fast_core);
  EXPECT_GT(fast_core, kMinSpinIterations);

  // Faster spin loops (fewer cycles per probe) run proportionally more
  // iterations for the same target cycle budget.
  const int half_cycle_per_iter = ComputeSpinIterations(
      kNumCpus, kRefLatencyCycles, kOneCyclePerSpinIter / 2);
  const int quarter_cycle_per_iter = ComputeSpinIterations(
      kNumCpus, kRefLatencyCycles, kOneCyclePerSpinIter / 4);
  EXPECT_LT(quarter_cycle_per_iter, kMaxSpinIterations);
  EXPECT_NEAR(half_cycle_per_iter, 2 * base_iters, 1);
  EXPECT_NEAR(quarter_cycle_per_iter, 2 * half_cycle_per_iter, 1);

  // Low-CPU-count machines spin as long as a minimum-size machine would, well
  // below kMaxSpinIterations.
  const int low_cpu_8 =
      ComputeSpinIterations(8, kRefLatencyCycles, kOneCyclePerSpinIter);
  const int low_cpu_16 =
      ComputeSpinIterations(16, kRefLatencyCycles, kOneCyclePerSpinIter);
  EXPECT_GT(low_cpu_8, base_iters);
  EXPECT_LT(low_cpu_8, kMaxSpinIterations);
  EXPECT_EQ(low_cpu_8, low_cpu_16);
}

// Checks that CalibrateSpinIterations() measures real probe costs, so that a
// regression (e.g. an elided probe loop) can't silently pin the result to
// kMinSpinIterations or kMaxSpinIterations.
TEST(SpinLockTest, CalibrateSpinIterationsMeasuresProbeCost) {
  if (NumCPUs() <= 1) GTEST_SKIP() << "Uniprocessors don't calibrate";

  // The spin probe runs RunLatencyProbe() for 1/divisor of the steps. Both
  // probes then run the same compiled loop, so their cycle ratio is `divisor`
  // on any machine or build mode, which makes the expected result exact.
  // Static so the captureless probe below can read it.
  static int divisor;
  int expected;
  // Start at 2, since a probe whose cost doesn't scale with `steps` gives a
  // ratio of 1. Grow it so the expected count is far above the lower clamp
  // (which large machines hit in real calibration) and a result stuck there
  // fails clearly.
  constexpr int kMinExpected = 3 * kMinSpinIterations;
  for (divisor = 2;; ++divisor) {
    expected = ComputeSpinIterations(NumCPUs(), int64_t{kProbeSteps} * divisor,
                                     kProbeSteps);
    if (expected >= kMinExpected) break;
  }
  ASSERT_LT(expected, kMaxSpinIterations);  // Also unclamped at the top.

  // Keep the closest of a few calibrations to tolerate preemption.
  int best = 0;
  for (int i = 0; i < 5; ++i) {
    const int iterations = CalibrateSpinIterations(
        [](int steps) { RunLatencyProbe(steps / divisor); });
    if (std::abs(iterations - expected) < std::abs(best - expected)) {
      best = iterations;
    }
  }
  // Bugs move the result far off: to a clamp, or to about expected / divisor.
  // Noise is a few percent, but fixed per-timing overhead (clock reads, calls)
  // biases the ratio low: negligible with hardware cycle counters, ~10% under
  // emulation with a steady_clock-based CycleClock (e.g. Android ARM32). So
  // allow 10% above and 1/3 below, which still excludes expected / 2.
  EXPECT_LE(best, expected + expected / 10);
  EXPECT_GE(best, expected - expected / 3);
}

}  // namespace
}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
