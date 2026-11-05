// Copyright 2018 The Abseil Authors.
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
//
// -----------------------------------------------------------------------------
// File: sample_recorder.h
// -----------------------------------------------------------------------------
//
// This header file defines a lock-free linked list for recording samples
// collected from a random/stochastic process.
//
// This utility is internal-only. Use at your own risk.

#ifndef ABSL_PROFILING_INTERNAL_SAMPLE_RECORDER_H_
#define ABSL_PROFILING_INTERNAL_SAMPLE_RECORDER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "absl/base/config.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {

// Sample<T> that has members required for linking samples in the linked list of
// samples maintained by the SampleRecorder.  Type T defines the sampled data.
template <typename T>
struct Sample {
  // Guards the ability to restore the sample to a pristine state.  This
  // prevents races with sampling and resurrecting an object.
  absl::Mutex init_mu;
  T* next = nullptr;
  std::atomic<T*> dead{nullptr};
  int64_t weight;  // How many sampling events were required to sample this one.
};

// Holds samples and their associated stack traces with a soft limit of
// `SetHashtablezMaxSamples()`.
//
// Thread safe.
template <typename T>
class SampleRecorder {
 public:
  SampleRecorder();
  ~SampleRecorder();

  // Registers for sampling.  Returns an opaque registration info.
  template <typename... Targs>
  T* Register(Targs&&... args);

  // Unregisters the sample.
  void Unregister(T* sample);

  // The dispose callback will be called on all samples the moment they are
  // being unregistered. Only affects samples that are unregistered after the
  // callback has been set.
  // Returns the previous callback.
  using DisposeCallback = void (*)(const T&);
  DisposeCallback SetDisposeCallback(DisposeCallback f);

  // Iterates over all the registered `StackInfo`s.  Returning the number of
  // samples that have been dropped.
  size_t Iterate(const std::function<void(const T& stack)>& f);

  size_t GetMaxSamples() const;
  void SetMaxSamples(size_t max);

 private:
  static constexpr size_t kNumGraveyardShards = 16;

  struct alignas(64) GraveyardShard {
    absl::Mutex mu;
    std::atomic<T*> head{nullptr};
  };

  static size_t ThreadShardIndex() {
    thread_local size_t shard = []() {
      static std::atomic<size_t> next_shard{0};
      return next_shard.fetch_add(1, std::memory_order_relaxed);
    }();
    return shard & (kNumGraveyardShards - 1);
  }

  void PushNew(T* sample);
  void PushDead(T* sample);
  template <typename... Targs>
  T* PopDead(Targs... args);

  alignas(64) std::atomic<size_t> size_estimate_;
  alignas(64) std::atomic<T*> all_;
  alignas(64) std::atomic<size_t> max_samples_{1 << 20};
  std::atomic<DisposeCallback> dispose_;
  std::atomic<size_t> dropped_samples_;
  alignas(64) std::atomic<uint32_t> non_empty_shards_{0};

  // Intrusive lock free linked lists for tracking samples.
  //
  // `all_` records all samples (they are never removed from this list) and is
  // terminated with a `nullptr`.
  //
  // `graveyard_shards_` partitions dead samples across cache-line-aligned
  // circular linked lists terminated by `&graveyard_`.  Every dead item has a
  // non-null `dead` pointer so `Iterate` can determine if a sample is live or
  // dead using only per-sample state, while per-thread sharding and the
  // `non_empty_shards_` summary mask avoid both global lock contention and
  // empty-shard probing.
  T graveyard_;
  GraveyardShard graveyard_shards_[kNumGraveyardShards];
};

template <typename T>
typename SampleRecorder<T>::DisposeCallback
SampleRecorder<T>::SetDisposeCallback(DisposeCallback f) {
  return dispose_.exchange(f, std::memory_order_relaxed);
}

template <typename T>
SampleRecorder<T>::SampleRecorder()
    : size_estimate_(0),
      all_(nullptr),
      dispose_(nullptr),
      dropped_samples_(0) {
  {
    absl::MutexLock l(graveyard_.init_mu);
    graveyard_.dead.store(&graveyard_, std::memory_order_relaxed);
  }
  for (size_t i = 0; i < kNumGraveyardShards; ++i) {
    graveyard_shards_[i].head.store(&graveyard_, std::memory_order_relaxed);
  }
}

template <typename T>
SampleRecorder<T>::~SampleRecorder() {
  T* s = all_.load(std::memory_order_acquire);
  while (s != nullptr) {
    T* next = s->next;
    delete s;
    s = next;
  }
}

template <typename T>
void SampleRecorder<T>::PushNew(T* sample) {
  sample->next = all_.load(std::memory_order_relaxed);
  while (!all_.compare_exchange_weak(sample->next, sample,
                                     std::memory_order_release,
                                     std::memory_order_relaxed)) {
  }
}

template <typename T>
void SampleRecorder<T>::PushDead(T* sample) {
  if (auto* dispose = dispose_.load(std::memory_order_relaxed)) {
    dispose(*sample);
  }

  // Mark `sample` dead under `sample->init_mu` before publishing it to the
  // thread's home graveyard shard.
  {
    absl::MutexLock sample_lock(sample->init_mu);
    sample->dead.store(&graveyard_, std::memory_order_relaxed);
  }
  const size_t shard_idx = ThreadShardIndex();
  GraveyardShard& shard = graveyard_shards_[shard_idx];
  {
    absl::MutexLock shard_lock(shard.mu);
    T* prev_head = shard.head.load(std::memory_order_relaxed);
    sample->dead.store(prev_head, std::memory_order_relaxed);
    shard.head.store(sample, std::memory_order_relaxed);
    if (prev_head == &graveyard_) {
      non_empty_shards_.fetch_or(uint32_t{1} << shard_idx,
                                 std::memory_order_relaxed);
    }
  }
}

template <typename T>
template <typename... Targs>
T* SampleRecorder<T>::PopDead(Targs... args) {
  T* sample = nullptr;
  const size_t home = ThreadShardIndex();
  GraveyardShard& home_shard = graveyard_shards_[home];
  if (home_shard.head.load(std::memory_order_relaxed) != &graveyard_) {
    absl::MutexLock shard_lock(home_shard.mu);
    T* head = home_shard.head.load(std::memory_order_relaxed);
    if (head != &graveyard_) {
      T* next_dead = head->dead.load(std::memory_order_relaxed);
      home_shard.head.store(next_dead, std::memory_order_relaxed);
      if (next_dead == &graveyard_) {
        non_empty_shards_.fetch_and(~(uint32_t{1} << home),
                                    std::memory_order_relaxed);
      }
      sample = head;
    }
  }
  if (sample == nullptr) {
    uint32_t mask = non_empty_shards_.load(std::memory_order_relaxed);
    while (mask != 0) {
      const size_t idx = static_cast<size_t>(__builtin_ctz(mask));
      mask &= mask - 1;
      if (idx == home) continue;
      GraveyardShard& shard = graveyard_shards_[idx];
      if (shard.head.load(std::memory_order_relaxed) == &graveyard_) {
        continue;
      }
      absl::MutexLock shard_lock(shard.mu);
      T* head = shard.head.load(std::memory_order_relaxed);
      if (head != &graveyard_) {
        T* next_dead = head->dead.load(std::memory_order_relaxed);
        shard.head.store(next_dead, std::memory_order_relaxed);
        if (next_dead == &graveyard_) {
          non_empty_shards_.fetch_and(~(uint32_t{1} << idx),
                                      std::memory_order_relaxed);
        }
        sample = head;
        break;
      }
    }
  }
  if (sample == nullptr) return nullptr;

  // `sample` is detached from its shard and will not be observed by PopDead.
  // SampleRecorder<T>::Iterate acquires the per-sample lock before inspecting
  // `sample->dead`.
  absl::MutexLock sample_lock(sample->init_mu);
  // Note: consider PrepareForSampling out of the lock.
  // Currently many clients have PrepareForSampling marked as
  // ABSL_EXCLUSIVE_LOCKS_REQUIRED.
  sample->PrepareForSampling(std::forward<Targs>(args)...);
  sample->dead.store(nullptr, std::memory_order_relaxed);
  return sample;
}

template <typename T>
template <typename... Targs>
T* SampleRecorder<T>::Register(Targs&&... args) {
  size_t size = size_estimate_.fetch_add(1, std::memory_order_relaxed);
  if (size > max_samples_.load(std::memory_order_relaxed)) {
    size_estimate_.fetch_sub(1, std::memory_order_relaxed);
    dropped_samples_.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
  }

  T* sample = PopDead(args...);
  if (sample == nullptr) {
    // Resurrection failed.  Hire a new warlock.
    sample = new T();
    {
      absl::MutexLock sample_lock(sample->init_mu);
      // If flag initialization happens to occur (perhaps in another thread)
      // while in this block, it will lock `graveyard_` which is usually always
      // locked before any sample. This will appear as a lock inversion.
      // However, this code is run exactly once per sample, and this sample
      // cannot be accessed until after it is returned from this method.  This
      // means that this lock state can never be recreated, so we can safely
      // inform the deadlock detector to ignore it.
      sample->init_mu.ForgetDeadlockInfo();
      sample->PrepareForSampling(std::forward<Targs>(args)...);
    }
    PushNew(sample);
  }

  return sample;
}

template <typename T>
void SampleRecorder<T>::Unregister(T* sample) {
  PushDead(sample);
  size_estimate_.fetch_sub(1, std::memory_order_relaxed);
}

template <typename T>
size_t SampleRecorder<T>::Iterate(
    const std::function<void(const T& stack)>& f) {
  T* s = all_.load(std::memory_order_acquire);
  while (s != nullptr) {
    absl::MutexLock l(s->init_mu);
    if (s->dead.load(std::memory_order_relaxed) == nullptr) {
      f(*s);
    }
    s = s->next;
  }

  return dropped_samples_.load(std::memory_order_relaxed);
}

template <typename T>
void SampleRecorder<T>::SetMaxSamples(size_t max) {
  max_samples_.store(max, std::memory_order_release);
}

template <typename T>
size_t SampleRecorder<T>::GetMaxSamples() const {
  return max_samples_.load(std::memory_order_acquire);
}

}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_PROFILING_INTERNAL_SAMPLE_RECORDER_H_
