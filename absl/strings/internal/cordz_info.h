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

#ifndef ABSL_STRINGS_CORDZ_INFO_H_
#define ABSL_STRINGS_CORDZ_INFO_H_

#include <atomic>
#include <cstdint>
#include <functional>

#include "absl/base/config.h"
#include "absl/base/thread_annotations.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cordz_handle.h"
#include "absl/strings/internal/cordz_statistics.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// CordzInfo tracks a profiled Cord. Each of these objects can be in two places.
// If a Cord is alive, the CordzInfo will be in the global_cordz_infos map, and
// can also be retrieved via the linked list starting with
// global_cordz_infos_head and continued via the cordz_info_next() method. When
// a Cord has reached the end of its lifespan, the CordzInfo object will be
// migrated out of the global_cordz_infos list and the global_cordz_infos_map,
// and will either be deleted or appended to the global_delete_queue. If it is
// placed on the global_delete_queue, the CordzInfo object will be cleaned in
// the destructor of a CordzSampleToken object.
class CordzInfo : public CordzHandle {
 public:
  // All profiled Cords should be accompanied by a call to TrackCord.
  // TrackCord creates a CordzInfo instance which tracks important metrics of
  // the sampled cord. CordzInfo instances are placed in a global list which is
  // used to discover and snapshot all actively tracked cords.
  // Callers are responsible for calling UntrackCord() before the tracked Cord
  // instance is deleted, or to stop tracking the sampled Cord.
  static CordzInfo* TrackCord(CordRep* rep);

  // Stops tracking changes for a sampled cord, and deletes the provided info.
  // This function must be called before the sampled cord instance is deleted,
  // and before the root cordrep of the sampled cord is unreffed.
  // This function may extend the lifetime of the cordrep in cases where the
  // CordInfo instance is being held by a concurrent collection thread.
  static void UntrackCord(CordzInfo* cordz_info);

  // Identical to TrackCord(), except that this function fills the
  // 'parent_stack' property of the returned CordzInfo instance from the
  // provided `src` instance if `src` is not null.
  // This function should be used for sampling 'copy constructed' cords.
  static CordzInfo* TrackCord(CordRep* rep, const CordzInfo* src);

  CordzInfo() = delete;
  CordzInfo(const CordzInfo&) = delete;
  CordzInfo& operator=(const CordzInfo&) = delete;

  // Retrieves the oldest existing CordzInfo.
  static CordzInfo* Head(const CordzSnapshot& snapshot);

  // Retrieves the next oldest existing CordzInfo older than 'this' instance.
  CordzInfo* Next(const CordzSnapshot& snapshot) const;

  // Returns a reference to the mutex guarding the `rep` property of this
  // instance. CordzInfo instances hold a weak reference to the rep pointer of
  // sampled cords, and rely on Cord logic to update the rep pointer when the
  // underlying root tree or ring of the cord changes.
  absl::Mutex& mutex() const { return mutex_; }

  // Updates the `rep' property of this instance. This methods is invoked by
  // Cord logic each time the root node of a sampled Cord changes, and before
  // the old root reference count is deleted. This guarantees that collection
  // code can always safely take a reference on the tracked cord.
  // Requires `mutex()` to be held.
  // TODO(b/117940323): annotate with ABSL_EXCLUSIVE_LOCKS_REQUIRED once all
  // Cord code is in a state where this can be proven true by the compiler.
  void SetCordRep(CordRep* rep);

  // Returns the current value of `rep_` for testing purposes only.
  CordRep* GetCordRepForTesting() const ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return rep_;
  }

  // Returns the stack trace for where the cord was first sampled. Cords are
  // potentially sampled when they promote from an inlined cord to a tree or
  // ring representation, which is not necessarily the location where the cord
  // was first created. Some cords are created as inlined cords, and only as
  // data is added do they become a non-inlined cord. However, typically the
  // location represents reasonably well where the cord is 'created'.
  absl::Span<void* const> GetStack() const;

  // Returns the stack trace for a sampled cord's 'parent stack trace'. This
  // value may be set if the cord is sampled (promoted) after being created
  // from, or being assigned the value of an existing (sampled) cord.
  absl::Span<void* const> GetParentStack() const;

  // Retrieve the CordzStatistics associated with this Cord. The statistics are
  // only updated when a Cord goes through a mutation, such as an Append or
  // RemovePrefix. The refcounts can change due to external events, so the
  // reported refcount stats might be incorrect.
  CordzStatistics GetCordzStatistics() const {
    CordzStatistics stats;
    stats.size = size_.load(std::memory_order_relaxed);
    return stats;
  }

  // Records size metric for this CordzInfo instance.
  void RecordMetrics(int64_t size) {
    size_.store(size, std::memory_order_relaxed);
  }

 private:
  static constexpr int kMaxStackDepth = 64;

  explicit CordzInfo(CordRep* tree);
  ~CordzInfo() override;

  void Track();
  void Untrack();

  // 'Unsafe' head/next/prev accessors not requiring the lock being held.
  // These are used exclusively for iterations (Head / Next) where we enforce
  // a token being held, so reading an 'old' / deleted pointer is fine.
  static CordzInfo* ci_head_unsafe() ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return ci_head_.load(std::memory_order_acquire);
  }
  CordzInfo* ci_next_unsafe() const ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return ci_next_.load(std::memory_order_acquire);
  }
  CordzInfo* ci_prev_unsafe() const ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return ci_prev_.load(std::memory_order_acquire);
  }

  static absl::Mutex ci_mutex_;
  static std::atomic<CordzInfo*> ci_head_ ABSL_GUARDED_BY(ci_mutex_);
  std::atomic<CordzInfo*> ci_prev_ ABSL_GUARDED_BY(ci_mutex_){nullptr};
  std::atomic<CordzInfo*> ci_next_ ABSL_GUARDED_BY(ci_mutex_){nullptr};

  mutable absl::Mutex mutex_;
  CordRep* rep_ ABSL_GUARDED_BY(mutex());

  void* stack_[kMaxStackDepth];
  void* parent_stack_[kMaxStackDepth];
  const int stack_depth_;
  int parent_stack_depth_;
  const absl::Time create_time_;

  // Last recorded size for the cord.
  std::atomic<int64_t> size_{0};
};

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_CORDZ_INFO_H_
