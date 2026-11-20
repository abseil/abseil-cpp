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

#include "absl/random/internal/entropy_pool.h"

#include <atomic>
#include <bitset>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if (defined(__unix__) || (defined(__APPLE__) && defined(__MACH__) &&          \
                           defined(TARGET_OS_OSX) && TARGET_OS_OSX)) &&        \
    !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__wasi__) &&      \
    !defined(__Fuchsia__) && !defined(__VXWORKS__) && !defined(__hexagon__) && \
    !defined(__XTENSA__) && !defined(__native_client__) && !defined(__rtems__)
#define ABSL_RANDOM_TEST_HAVE_FORK 1
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "gtest/gtest.h"
#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/container/flat_hash_set.h"
#include "absl/random/seed_gen_exception.h"  // IWYU pragma: keep
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"

namespace {

using ::absl::random_internal::GetEntropyFromRandenPool;
using ::absl::random_internal::GetRandenPoolOSEntropyReadCountForTesting;
using ::absl::random_internal::ResetRandenPoolForTesting;
#ifdef ABSL_HAVE_EXCEPTIONS
using ::absl::random_internal::SetRandenPoolOSEntropyFailureCountForTesting;
#endif

TEST(EntropyPoolTest, DistinctSequencesPerThread) {
  using result_type = uint32_t;
  constexpr int kNumThreads = 12;
  constexpr size_t kValuesPerThread = 32;

  // Acquire entropy from multiple threads.
  std::vector<std::vector<result_type>> data;
  {
    absl::Mutex mu;
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; i++) {
      threads.emplace_back([&]() {
        std::vector<result_type> v(kValuesPerThread);
        GetEntropyFromRandenPool(v.data(), sizeof(result_type) * v.size());
        absl::MutexLock l(mu);
        data.push_back(std::move(v));
      });
    }
    for (auto& t : threads) t.join();
  }

  EXPECT_EQ(data.size(), kNumThreads);

  // There should be essentially no duplicates in the sequences.
  size_t expected_size = 0;
  absl::flat_hash_set<result_type> seen;
  for (const auto& v : data) {
    expected_size += v.size();
    for (result_type x : v) seen.insert(x);
  }
  EXPECT_GE(seen.size(), expected_size - 1);
}

// This validates that sequences are independent.
TEST(EntropyPoolTest, ValidateDistribution) {
  using result_type = uint32_t;
  constexpr int kNumOutputs = 16;
  std::vector<result_type> a(kNumOutputs);
  std::vector<result_type> b(kNumOutputs);

  GetEntropyFromRandenPool(a.data(), sizeof(a[0]) * a.size());
  GetEntropyFromRandenPool(b.data(), sizeof(b[0]) * b.size());

  // Compare the two sequences, counting the number of bits that are different,
  // then verify using a normal-approximation of the binomial distribution.
  size_t changed_bits = 0;
  size_t total_set = 0;
  size_t equal_count = 0;
  size_t zero_count = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    std::bitset<sizeof(result_type) * 8> changed_set(a[i] ^ b[i]);
    changed_bits += changed_set.count();

    std::bitset<sizeof(result_type) * 8> a_set(a[i]);
    std::bitset<sizeof(result_type) * 8> b_set(b[i]);
    total_set += a_set.count() + b_set.count();

    equal_count += (a[i] == b[i]) ? 1 : 0;

    zero_count += (a[i] == 0) ? 1 : 0;
    zero_count += (b[i] == 0) ? 1 : 0;
  }

  constexpr size_t kNBits = kNumOutputs * sizeof(result_type) * 8;

  // This should be a binomial distribution with:
  //    p = 0.5
  //    n = kNBits
  //    sigma =~ 11.3 (sqrt(n * 0.5 * 0.5))
  // So we expect the number of changed bits to be within 5 standard deviations
  // of the mean; this should fail less than one in 3 million times.
  EXPECT_NEAR(changed_bits, kNBits * 0.5, 5 * std::sqrt(kNBits))
      << "@" << changed_bits / static_cast<double>(kNBits);

  // Verify that the number of set bits is also within the expected range;
  // Note that this is summed over the two sequences, so the number of trials
  // is twice the number of bits.
  EXPECT_NEAR(total_set, kNBits, 5 * std::sqrt(2 * kNBits))
      << "@" << total_set / static_cast<double>(2 * kNBits);

  // A[i] == B[i] with probability ~= 16 * 1/2^32; certainly less than 1.
  EXPECT_LE(equal_count, 1);

  // Zeros values must be rare; 32 / 2^32 is certainly less than 1.
  EXPECT_LE(zero_count, 1);
}

TEST(EntropyPoolTest, ConcurrentInitSerializesEntropyReads) {
  constexpr int kNumThreads = 12;
  ResetRandenPoolForTesting();
  const size_t reads_before = GetRandenPoolOSEntropyReadCountForTesting();

  absl::BlockingCounter ready(kNumThreads);
  absl::Notification go;
  std::atomic<int> non_zero_threads{0};
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&]() {
      ready.DecrementCount();
      go.WaitForNotification();
      uint64_t buf[16] = {};
      GetEntropyFromRandenPool(buf, sizeof(buf));
      bool any_non_zero = false;
      for (uint64_t v : buf) {
        any_non_zero |= (v != 0);
      }
      if (any_non_zero) {
        non_zero_threads.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  ready.Wait();
  go.Notify();
  for (auto& t : threads) t.join();

  EXPECT_EQ(non_zero_threads.load(std::memory_order_relaxed), kNumThreads);
  EXPECT_EQ(GetRandenPoolOSEntropyReadCountForTesting() - reads_before, 1u);
}

#ifdef ABSL_HAVE_EXCEPTIONS

class ScopedOSEntropyFailureCount {
 public:
  explicit ScopedOSEntropyFailureCount(size_t count) {
    SetRandenPoolOSEntropyFailureCountForTesting(count);
  }
  ~ScopedOSEntropyFailureCount() {
    SetRandenPoolOSEntropyFailureCountForTesting(0);
  }
  ScopedOSEntropyFailureCount(const ScopedOSEntropyFailureCount&) = delete;
  ScopedOSEntropyFailureCount& operator=(const ScopedOSEntropyFailureCount&) =
      delete;
};

TEST(EntropyPoolTest, SeedGenExceptionDoesNotDeadlock) {
  ResetRandenPoolForTesting();
  ScopedOSEntropyFailureCount inject_failures(2);

  uint64_t value = 0;
  EXPECT_THROW(GetEntropyFromRandenPool(&value, sizeof(value)),
               absl::SeedGenException);
  EXPECT_THROW(GetEntropyFromRandenPool(&value, sizeof(value)),
               absl::SeedGenException);

  // Subsequent initialization must succeed cleanly without deadlocking.
  uint64_t buf[16] = {};
  EXPECT_NO_THROW(GetEntropyFromRandenPool(buf, sizeof(buf)));
  bool any_non_zero = false;
  for (uint64_t v : buf) {
    any_non_zero |= (v != 0);
  }
  EXPECT_TRUE(any_non_zero);
}

TEST(EntropyPoolTest, ConcurrentSeedGenExceptionRecovery) {
  constexpr int kNumThreads = 8;
  ResetRandenPoolForTesting();
  ScopedOSEntropyFailureCount inject_failures(1);

  absl::BlockingCounter ready(kNumThreads);
  absl::Notification go;
  std::atomic<int> caught_exceptions{0};
  std::atomic<int> succeeded{0};
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&]() {
      ready.DecrementCount();
      go.WaitForNotification();
      uint64_t buf[16] = {};
      try {
        GetEntropyFromRandenPool(buf, sizeof(buf));
      } catch (const absl::SeedGenException&) {
        caught_exceptions.fetch_add(1, std::memory_order_relaxed);
        GetEntropyFromRandenPool(buf, sizeof(buf));
      }
      bool any_non_zero = false;
      for (uint64_t v : buf) {
        any_non_zero |= (v != 0);
      }
      if (any_non_zero) {
        succeeded.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  ready.Wait();
  go.Notify();
  for (auto& t : threads) t.join();

  EXPECT_EQ(caught_exceptions.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(succeeded.load(std::memory_order_relaxed), kNumThreads);
}

#endif  // ABSL_HAVE_EXCEPTIONS

#ifdef ABSL_RANDOM_TEST_HAVE_FORK

bool WriteAll(int fd, const void* data, size_t size) {
  const auto* ptr = static_cast<const uint8_t*>(data);
  while (size > 0) {
    const ssize_t n = write(fd, ptr, size);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    ptr += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

bool ReadAll(int fd, void* data, size_t size) {
  auto* ptr = static_cast<uint8_t*>(data);
  while (size > 0) {
    const ssize_t n = read(fd, ptr, size);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    ptr += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

int WaitForChildExit(pid_t pid) {
  int status = 0;
  pid_t ret;
  do {
    ret = waitpid(pid, &status, 0);
  } while (ret < 0 && errno == EINTR);
  if (ret != pid || !WIFEXITED(status)) return -1;
  return WEXITSTATUS(status);
}

TEST(EntropyPoolTest, DistinctSequencesAcrossFork) {
  // Warm up the pool in the parent before fork() so buffered Randen state is
  // already populated and ready to be cloned into child address spaces.
  uint64_t warmup = 0;
  GetEntropyFromRandenPool(&warmup, sizeof(warmup));

  constexpr size_t kNumValues = 16;
  int pipe1[2];
  ASSERT_EQ(pipe(pipe1), 0);

  std::fflush(nullptr);
  const pid_t pid1 = fork();
  if (pid1 < 0) {
    const int fork1_errno = errno;
    close(pipe1[0]);
    close(pipe1[1]);
    if (fork1_errno == ENOSYS || fork1_errno == EPERM) {
      GTEST_SKIP() << "fork() is not permitted in this environment.";
    }
    FAIL() << "First fork() failed: " << std::strerror(fork1_errno);
  }

  if (pid1 == 0) {
    close(pipe1[0]);
    std::vector<uint64_t> seq(kNumValues);
    GetEntropyFromRandenPool(seq.data(), sizeof(uint64_t) * seq.size());

    // Also verify nested fork() reseeds a grandchild relative to this child.
    int gc_pipe[2];
    if (pipe(gc_pipe) != 0) _exit(2);
    const pid_t gc_pid = fork();
    if (gc_pid < 0) _exit(3);
    if (gc_pid == 0) {
      close(gc_pipe[0]);
      close(pipe1[1]);
      std::vector<uint64_t> gc_seq(kNumValues);
      GetEntropyFromRandenPool(gc_seq.data(), sizeof(uint64_t) * gc_seq.size());
      const bool ok =
          WriteAll(gc_pipe[1], gc_seq.data(), sizeof(uint64_t) * gc_seq.size());
      close(gc_pipe[1]);
      _exit(ok ? 0 : 4);
    }
    close(gc_pipe[1]);
    std::vector<uint64_t> child_after_gc(kNumValues);
    GetEntropyFromRandenPool(child_after_gc.data(),
                             sizeof(uint64_t) * child_after_gc.size());
    std::vector<uint64_t> gc_seq(kNumValues);
    const bool gc_read_ok =
        ReadAll(gc_pipe[0], gc_seq.data(), sizeof(uint64_t) * gc_seq.size());
    close(gc_pipe[0]);
    const int gc_exit = WaitForChildExit(gc_pid);

    const bool write_ok =
        WriteAll(pipe1[1], seq.data(), sizeof(uint64_t) * seq.size());
    close(pipe1[1]);
    if (!gc_read_ok || gc_exit != 0 || !write_ok || gc_seq == child_after_gc ||
        gc_seq == seq) {
      _exit(5);
    }
    _exit(0);
  }
  close(pipe1[1]);

  int pipe2[2];
  if (pipe(pipe2) != 0) {
    const int pipe_errno = errno;
    close(pipe1[0]);
    WaitForChildExit(pid1);
    FAIL() << "Second pipe() failed: " << std::strerror(pipe_errno);
  }
  std::fflush(nullptr);
  const pid_t pid2 = fork();
  if (pid2 < 0) {
    const int fork2_errno = errno;
    close(pipe1[0]);
    close(pipe2[0]);
    close(pipe2[1]);
    WaitForChildExit(pid1);
    FAIL() << "Second fork() failed: " << std::strerror(fork2_errno);
  }
  if (pid2 == 0) {
    close(pipe1[0]);
    close(pipe2[0]);
    std::vector<uint64_t> seq(kNumValues);
    GetEntropyFromRandenPool(seq.data(), sizeof(uint64_t) * seq.size());
    const bool ok =
        WriteAll(pipe2[1], seq.data(), sizeof(uint64_t) * seq.size());
    close(pipe2[1]);
    _exit(ok ? 0 : 1);
  }
  close(pipe2[1]);

  std::vector<uint64_t> parent_seq(kNumValues);
  GetEntropyFromRandenPool(parent_seq.data(),
                           sizeof(uint64_t) * parent_seq.size());

  std::vector<uint64_t> child1_seq(kNumValues);
  std::vector<uint64_t> child2_seq(kNumValues);
  const bool read1_ok = ReadAll(pipe1[0], child1_seq.data(),
                                sizeof(uint64_t) * child1_seq.size());
  const bool read2_ok = ReadAll(pipe2[0], child2_seq.data(),
                                sizeof(uint64_t) * child2_seq.size());
  close(pipe1[0]);
  close(pipe2[0]);

  EXPECT_EQ(WaitForChildExit(pid1), 0);
  EXPECT_EQ(WaitForChildExit(pid2), 0);
  ASSERT_TRUE(read1_ok);
  ASSERT_TRUE(read2_ok);

  EXPECT_NE(parent_seq, child1_seq);
  EXPECT_NE(parent_seq, child2_seq);
  EXPECT_NE(child1_seq, child2_seq);
}

TEST(EntropyPoolTest, ForkDuringConcurrentPoolAccess) {
  uint64_t warmup = 0;
  GetEntropyFromRandenPool(&warmup, sizeof(warmup));

  // Verify fork() works in this environment before spawning background threads.
  std::fflush(nullptr);
  const pid_t probe = fork();
  const int probe_errno = errno;
  if (probe < 0 && (probe_errno == ENOSYS || probe_errno == EPERM)) {
    GTEST_SKIP() << "fork() is not permitted in this environment.";
  }
  ASSERT_GE(probe, 0) << std::strerror(probe_errno);
  if (probe == 0) _exit(0);
  ASSERT_EQ(WaitForChildExit(probe), 0);

  // Spawn 8 threads (matching kPoolSize) so every RandenPoolEntry spinlock,
  // including the slot assigned to this thread, is contended during fork().
  constexpr int kNumThreads = 8;
  std::atomic<bool> stop{false};
  std::atomic<int> ready{0};
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);

  struct ThreadJoiner {
    std::atomic<bool>& stop_flag;
    std::vector<std::thread>& pool;
    ~ThreadJoiner() {
      stop_flag.store(true, std::memory_order_relaxed);
      for (auto& t : pool) {
        if (t.joinable()) t.join();
      }
    }
  } joiner{stop, threads};

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&]() {
      uint64_t buf[32];
      GetEntropyFromRandenPool(buf, sizeof(buf));
      ready.fetch_add(1, std::memory_order_release);
      while (!stop.load(std::memory_order_relaxed)) {
        GetEntropyFromRandenPool(buf, sizeof(buf));
      }
    });
  }
  while (ready.load(std::memory_order_acquire) < kNumThreads) {
    std::this_thread::yield();
  }

  constexpr int kNumForks = 16;
  for (int i = 0; i < kNumForks; ++i) {
    const pid_t pid = fork();
    ASSERT_GE(pid, 0) << std::strerror(errno);
    if (pid == 0) {
      alarm(10);
      uint64_t child_buf[32] = {};
      GetEntropyFromRandenPool(child_buf, sizeof(child_buf));
      bool any_non_zero = false;
      for (uint64_t v : child_buf) {
        any_non_zero |= (v != 0);
      }
      _exit(any_non_zero ? 0 : 2);
    }
    EXPECT_EQ(WaitForChildExit(pid), 0) << "iteration " << i;
  }
}

#ifdef ABSL_HAVE_EXCEPTIONS
TEST(EntropyPoolTest, SeedGenExceptionRecoveryAfterFork) {
  uint64_t warmup = 0;
  GetEntropyFromRandenPool(&warmup, sizeof(warmup));

  std::fflush(nullptr);
  const pid_t pid = fork();
  const int fork_errno = errno;
  if (pid < 0 && (fork_errno == ENOSYS || fork_errno == EPERM)) {
    GTEST_SKIP() << "fork() is not permitted in this environment.";
  }
  ASSERT_GE(pid, 0) << std::strerror(fork_errno);

  if (pid == 0) {
    alarm(10);
    // Inject an OS entropy failure in the child without resetting the pool
    // explicitly, verifying that OnForkChild() triggers re-initialization,
    // SeedGenException is thrown cleanly, and the retry succeeds.
    SetRandenPoolOSEntropyFailureCountForTesting(1);
    uint64_t buf[16] = {};
    bool caught = false;
    try {
      GetEntropyFromRandenPool(buf, sizeof(buf));
    } catch (const absl::SeedGenException&) {
      caught = true;
    }
    if (!caught) _exit(2);

    try {
      GetEntropyFromRandenPool(buf, sizeof(buf));
    } catch (...) {
      _exit(3);
    }
    bool any_non_zero = false;
    for (uint64_t v : buf) {
      any_non_zero |= (v != 0);
    }
    _exit(any_non_zero ? 0 : 4);
  }

  EXPECT_EQ(WaitForChildExit(pid), 0);
  uint64_t parent_buf[16] = {};
  EXPECT_NO_THROW(GetEntropyFromRandenPool(parent_buf, sizeof(parent_buf)));
}
#endif  // ABSL_HAVE_EXCEPTIONS

#endif  // ABSL_RANDOM_TEST_HAVE_FORK

}  // namespace
