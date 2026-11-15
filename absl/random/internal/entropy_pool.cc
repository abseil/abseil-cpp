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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/random/internal/randen.h"
#include "absl/random/internal/randen_traits.h"
#include "absl/random/internal/seed_material.h"
#include "absl/random/seed_gen_exception.h"
#include "absl/types/span.h"

#if (defined(__unix__) || (defined(__APPLE__) && defined(__MACH__)) ||         \
     defined(_AIX)) &&                                                         \
    !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__wasi__) &&      \
    !defined(__Fuchsia__) && !defined(__VXWORKS__) && !defined(__hexagon__) && \
    !defined(__XTENSA__) && !defined(__native_client__) && !defined(__rtems__)
#define ABSL_RANDOM_HAVE_PTHREAD_ATFORK 1
#endif

#if defined(ABSL_RANDOM_HAVE_PTHREAD_ATFORK) || !defined(ABSL_HAVE_THREAD_LOCAL)
#include <pthread.h>
#endif

#ifdef ABSL_RANDOM_HAVE_PTHREAD_ATFORK
#include <sys/types.h>
#include <unistd.h>
#endif

using absl::base_internal::SpinLock;
using absl::base_internal::SpinLockHolder;

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

// RandenPoolEntry is a thread-safe pseudorandom bit generator, implementing a
// single generator within a RandenPool<T>. It is an internal implementation
// detail, and does not aim to conform to [rand.req.urng].
//
// At least 32-byte alignment is required for the state_ array on some ARM
// platforms.  We also want this aligned to a cacheline to eliminate false
// sharing.
class alignas(std::max(size_t{ABSL_CACHELINE_SIZE}, size_t{32}))
    RandenPoolEntry {
 public:
  static constexpr size_t kState = RandenTraits::kStateBytes / sizeof(uint32_t);
  static constexpr size_t kCapacity =
      RandenTraits::kCapacityBytes / sizeof(uint32_t);

  void Init(absl::Span<const uint32_t> data) {
    SpinLockHolder l(mu_);  // Always uncontested.
    absl::c_copy(data, std::begin(state_));
    next_ = kState;
  }

  // Copy bytes into out.
  void Fill(uint8_t* out, size_t bytes) ABSL_LOCKS_EXCLUDED(mu_);

  void MaybeRefill() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (next_ >= kState) {
      next_ = kCapacity;
      impl_.Generate(state_);
    }
  }

  size_t available() const ABSL_SHARED_LOCKS_REQUIRED(mu_) {
    return kState - next_;
  }

 private:
  // Randen URBG state.
  // At least 32-byte alignment is required by ARM platform code.
  alignas(32) uint32_t state_[kState] ABSL_GUARDED_BY(mu_);
  SpinLock mu_;
  const Randen impl_;
  size_t next_ ABSL_GUARDED_BY(mu_);
};

void RandenPoolEntry::Fill(uint8_t* out, size_t bytes) {
  SpinLockHolder l(mu_);
  while (bytes > 0) {
    MaybeRefill();
    size_t remaining = available() * sizeof(state_[0]);
    size_t to_copy = std::min(bytes, remaining);
    std::memcpy(out, &state_[next_], to_copy);
    out += to_copy;
    bytes -= to_copy;
    next_ += (to_copy + sizeof(state_[0]) - 1) / sizeof(state_[0]);
  }
}

// Number of pooled urbg entries.
static constexpr size_t kPoolSize = 8;

// A generation-tagged array of pool entries. Old generations remain linked via
// `prev` so they stay reachable to leak checkers without ever destroying or
// locking spinlocks inherited across fork().
struct PoolArray {
  PoolArray* prev = nullptr;
  uint32_t generation = 0;
  RandenPoolEntry entries[kPoolSize];
};

// Active pool entries for the current process generation.
ABSL_CONST_INIT static std::atomic<PoolArray*> shared_pools_ptr{nullptr};

// Monotonically incremented generation counter; bumped in forked children via
// OnForkChild() (and by ResetRandenPoolForTesting()).
ABSL_CONST_INIT static std::atomic<uint32_t> pool_generation{0};

// Per-generation serialization gate for slow-path pool initialization.
// On fork-capable platforms, gates are additionally tagged with the owning
// process ID so a child forked before pthread_atfork() completes never
// acquires a parent's SpinLock. Prior gates remain linked via `prev` so leak
// checkers continue to see them as reachable without touching inherited locks.
struct InitGate {
  InitGate* prev = nullptr;
  uint32_t generation = 0;
#ifdef ABSL_RANDOM_HAVE_PTHREAD_ATFORK
  pid_t pid = 0;
#endif
  SpinLock mu;
};

ABSL_CONST_INIT static std::atomic<InitGate*> init_gate_ptr{nullptr};

// Test-only fault injection and observation counters (touched only on the
// slow initialization path).
ABSL_CONST_INIT static std::atomic<size_t> seed_failure_count_for_testing{0};
ABSL_CONST_INIT static std::atomic<size_t> os_entropy_read_count_for_testing{0};

bool ReadSeedMaterialForPool(absl::Span<uint32_t> values) {
  os_entropy_read_count_for_testing.fetch_add(1, std::memory_order_acq_rel);
  size_t failures =
      seed_failure_count_for_testing.load(std::memory_order_acquire);
  while (failures > 0) {
    if (seed_failure_count_for_testing.compare_exchange_weak(
            failures, failures - 1, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return false;
    }
  }
  return ReadSeedMaterialFromOSEntropy(values);
}

#ifdef ABSL_RANDOM_HAVE_PTHREAD_ATFORK
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "OnForkChild requires lock-free atomic uint32_t operations");

ABSL_CONST_INIT static std::atomic<uint32_t> atfork_registered{0};

// Async-signal-safe child fork handler: increments the generation counter so
// the child process lazily allocates and OS-seeds a fresh PoolArray on next
// use without touching parent spinlocks or buffered PRNG state.
void OnForkChild() {
  atfork_registered.store(1, std::memory_order_relaxed);
  pool_generation.fetch_add(1, std::memory_order_relaxed);
}

// Registers OnForkChild once. Called while holding the current InitGate lock
// so concurrent cold-start callers do not register duplicate permanent atfork
// handlers. Leaves `atfork_registered` unset if pthread_atfork() fails (e.g.
// ENOMEM) so fork detection is never silently disabled.
bool EnsureAtForkRegistered() {
  if (atfork_registered.load(std::memory_order_acquire) != 0) {
    return true;
  }
  if (pthread_atfork(nullptr, nullptr, OnForkChild) != 0) {
    return false;
  }
  atfork_registered.store(1, std::memory_order_release);
  return true;
}
#endif  // ABSL_RANDOM_HAVE_PTHREAD_ATFORK

InitGate* GetOrCreateInitGate(uint32_t generation
#ifdef ABSL_RANDOM_HAVE_PTHREAD_ATFORK
                              ,
                              pid_t pid
#endif
) {
  auto matches = [=](const InitGate* g) {
    if (g == nullptr || g->generation != generation) return false;
#ifdef ABSL_RANDOM_HAVE_PTHREAD_ATFORK
    if (g->pid != pid) return false;
#endif
    return true;
  };

  InitGate* expected = init_gate_ptr.load(std::memory_order_acquire);
  if (matches(expected)) {
    return expected;
  }

  auto new_gate = std::make_unique<InitGate>();
  new_gate->generation = generation;
#ifdef ABSL_RANDOM_HAVE_PTHREAD_ATFORK
  new_gate->pid = pid;
#endif

  while (true) {
    if (matches(expected)) {
      return expected;
    }
    new_gate->prev = expected;
    if (init_gate_ptr.compare_exchange_weak(expected, new_gate.get(),
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
      return new_gate.release();
    }
  }
}

// Returns an id in the range [0 ... kPoolSize), which indexes into the
// pool of random engines.
//
// Each thread to access the pool is assigned a sequential ID (without reuse)
// from the pool-id space; the id is cached in a thread_local variable.
// This id is assigned based on the arrival-order of the thread to the
// GetPoolID call; this has no binary, CL, or runtime stability because
// on subsequent runs the order within the same program may be significantly
// different. However, as other thread IDs are not assigned sequentially,
// this is not expected to matter.
size_t GetPoolID() {
  static_assert(kPoolSize >= 1,
                "At least one urbg instance is required for PoolURBG");

  ABSL_CONST_INIT static std::atomic<uint64_t> sequence{0};

#ifdef ABSL_HAVE_THREAD_LOCAL
  static thread_local size_t my_pool_id = kPoolSize;
  if (ABSL_PREDICT_FALSE(my_pool_id == kPoolSize)) {
    my_pool_id = (sequence++ % kPoolSize);
  }
  return my_pool_id;
#else
  static pthread_key_t tid_key = [] {
    pthread_key_t tmp_key;
    int err = pthread_key_create(&tmp_key, nullptr);
    if (err) {
      ABSL_RAW_LOG(FATAL, "pthread_key_create failed with %d", err);
    }
    return tmp_key;
  }();

  // Store the value in the pthread_{get/set}specific. However an uninitialized
  // value is 0, so add +1 to distinguish from the null value.
  uintptr_t my_pool_id =
      reinterpret_cast<uintptr_t>(pthread_getspecific(tid_key));
  if (ABSL_PREDICT_FALSE(my_pool_id == 0)) {
    // No allocated ID, allocate the next value, cache it, and return.
    my_pool_id = (sequence++ % kPoolSize) + 1;
    int err = pthread_setspecific(tid_key, reinterpret_cast<void*>(my_pool_id));
    if (err) {
      ABSL_RAW_LOG(FATAL, "pthread_setspecific failed with %d", err);
    }
  }
  return my_pool_id - 1;
#endif
}

// Allocate and initialize a fresh PoolArray for `current_gen` using OS entropy.
// Serialized per generation via a generation-tagged InitGate so concurrent
// cold-start or post-fork callers perform a single OS entropy read and a single
// pthread_atfork() registration without ever inheriting a parent's locked
// SpinLock across fork() or wedging on SeedGenException.
ABSL_ATTRIBUTE_NOINLINE PoolArray* InitPoolsForGeneration(
    uint32_t current_gen) {
#ifdef ABSL_RANDOM_HAVE_PTHREAD_ATFORK
  InitGate* gate = GetOrCreateInitGate(current_gen, getpid());
#else
  InitGate* gate = GetOrCreateInitGate(current_gen);
#endif

  PoolArray* result = nullptr;
  {
    SpinLockHolder l(gate->mu);

    PoolArray* existing = shared_pools_ptr.load(std::memory_order_acquire);
    if (existing != nullptr && existing->generation == current_gen) {
      return existing;
    }

#ifdef ABSL_RANDOM_HAVE_PTHREAD_ATFORK
    const bool atfork_ok = EnsureAtForkRegistered();
#else
    constexpr bool atfork_ok = true;
#endif

    if (atfork_ok) {
      static constexpr size_t kSeedSize =
          RandenTraits::kStateBytes / sizeof(uint32_t);
      uint32_t seed_material[kPoolSize * kSeedSize];
      struct SeedScrubber {
        absl::Span<uint32_t> values;
        ~SeedScrubber() {
          volatile uint32_t* p = values.data();
          for (size_t i = 0; i < values.size(); ++i) {
            p[i] = 0;
          }
        }
      } scrubber{absl::MakeSpan(seed_material)};

      if (ReadSeedMaterialForPool(absl::MakeSpan(seed_material))) {
        auto new_pools = std::make_unique<PoolArray>();
        new_pools->generation = current_gen;
        for (size_t i = 0; i < kPoolSize; ++i) {
          new_pools->entries[i].Init(
              absl::MakeSpan(&seed_material[i * kSeedSize], kSeedSize));
        }

        while (true) {
          if (existing != nullptr && existing->generation == current_gen) {
            result = existing;
            break;
          }
          new_pools->prev = existing;
          if (shared_pools_ptr.compare_exchange_weak(
                  existing, new_pools.get(), std::memory_order_acq_rel,
                  std::memory_order_acquire)) {
            result = new_pools.release();
            break;
          }
        }
      }
    }
  }

  if (result == nullptr) {
    ThrowSeedGenException();
  }
  return result;
}

// Returns the pool entry for the current thread.
RandenPoolEntry* GetPoolForCurrentThread() {
  const uint32_t current_gen = pool_generation.load(std::memory_order_relaxed);
  PoolArray* pools = shared_pools_ptr.load(std::memory_order_acquire);
  if (ABSL_PREDICT_FALSE(pools == nullptr ||
                         pools->generation != current_gen)) {
    pools = InitPoolsForGeneration(current_gen);
  }
  return &pools->entries[GetPoolID()];
}

}  // namespace

void GetEntropyFromRandenPool(void* dest, size_t bytes) {
  auto* pool = GetPoolForCurrentThread();
  pool->Fill(reinterpret_cast<uint8_t*>(dest), bytes);
}

void ResetRandenPoolForTesting() {
  pool_generation.fetch_add(1, std::memory_order_acq_rel);
}

void SetRandenPoolOSEntropyFailureCountForTesting(size_t count) {
  seed_failure_count_for_testing.store(count, std::memory_order_release);
}

size_t GetRandenPoolOSEntropyReadCountForTesting() {
  return os_entropy_read_count_for_testing.load(std::memory_order_acquire);
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
