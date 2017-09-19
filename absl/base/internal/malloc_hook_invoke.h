//
// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
///

// This has the implementation details of malloc_hook that are needed
// to use malloc-hook inside the tcmalloc system.  It does not hold
// any of the client-facing calls that are used to add new hooks.
//
// IWYU pragma: private, include "base/malloc_hook-inl.h"

#ifndef ABSL_BASE_INTERNAL_MALLOC_HOOK_INVOKE_H_
#define ABSL_BASE_INTERNAL_MALLOC_HOOK_INVOKE_H_

#include <sys/types.h>
#include <atomic>
#include <cstddef>

#include "absl/base/internal/malloc_hook.h"

namespace absl {
namespace base_internal {

// Maximum of 7 hooks means that HookList is 8 words.
static constexpr int kHookListMaxValues = 7;

// HookList: a class that provides synchronized insertions and removals and
// lockless traversal.  Most of the implementation is in malloc_hook.cc.
template <typename T>
struct HookList {
  static_assert(sizeof(T) <= sizeof(intptr_t), "T_should_fit_in_intptr_t");

  // Adds value to the list.  Note that duplicates are allowed.  Thread-safe and
  // blocking (acquires hooklist_spinlock).  Returns true on success; false
  // otherwise (failures include invalid value and no space left).
  bool Add(T value);

  // Removes the first entry matching value from the list.  Thread-safe and
  // blocking (acquires hooklist_spinlock).  Returns true on success; false
  // otherwise (failures include invalid value and no value found).
  bool Remove(T value);

  // Store up to n values of the list in output_array, and return the number of
  // elements stored.  Thread-safe and non-blocking.  This is fast (one memory
  // access) if the list is empty.
  int Traverse(T* output_array, int n) const;

  // Fast inline implementation for fast path of Invoke*Hook.
  bool empty() const {
    // empty() is only used as an optimization to determine if we should call
    // Traverse which has proper acquire loads.  Memory reordering around a
    // call to empty will either lead to an unnecessary Traverse call, or will
    // miss invoking hooks, neither of which is a problem.
    return priv_end.load(std::memory_order_relaxed) == 0;
  }

  // This internal data is not private so that the class is an aggregate and can
  // be initialized by the linker.  Don't access this directly.  Use the
  // INIT_HOOK_LIST macro in malloc_hook.cc.

  // One more than the index of the last valid element in priv_data.  During
  // 'Remove' this may be past the last valid element in priv_data, but
  // subsequent values will be 0.
  std::atomic<int> priv_end;
  std::atomic<intptr_t> priv_data[kHookListMaxValues];
};

extern template struct HookList<MallocHook::NewHook>;

extern HookList<MallocHook::NewHook> new_hooks_;
extern HookList<MallocHook::DeleteHook> delete_hooks_;
extern HookList<MallocHook::SampledNewHook> sampled_new_hooks_;
extern HookList<MallocHook::SampledDeleteHook> sampled_delete_hooks_;
extern HookList<MallocHook::PreMmapHook> premmap_hooks_;
extern HookList<MallocHook::MmapHook> mmap_hooks_;
extern HookList<MallocHook::MmapReplacement> mmap_replacement_;
extern HookList<MallocHook::MunmapHook> munmap_hooks_;
extern HookList<MallocHook::MunmapReplacement> munmap_replacement_;
extern HookList<MallocHook::MremapHook> mremap_hooks_;
extern HookList<MallocHook::PreSbrkHook> presbrk_hooks_;
extern HookList<MallocHook::SbrkHook> sbrk_hooks_;

inline void MallocHook::InvokeNewHook(const void* ptr, size_t size) {
  if (!absl::base_internal::new_hooks_.empty()) {
    InvokeNewHookSlow(ptr, size);
  }
}

inline void MallocHook::InvokeDeleteHook(const void* ptr) {
  if (!absl::base_internal::delete_hooks_.empty()) {
    InvokeDeleteHookSlow(ptr);
  }
}

inline void MallocHook::InvokeSampledNewHook(
    const SampledAlloc* sampled_alloc) {
  if (!absl::base_internal::sampled_new_hooks_.empty()) {
    InvokeSampledNewHookSlow(sampled_alloc);
  }
}

inline void MallocHook::InvokeSampledDeleteHook(AllocHandle handle) {
  if (!absl::base_internal::sampled_delete_hooks_.empty()) {
    InvokeSampledDeleteHookSlow(handle);
  }
}

inline void MallocHook::InvokePreMmapHook(const void* start,
                                          size_t size,
                                          int protection,
                                          int flags,
                                          int fd,
                                          off_t offset) {
  if (!absl::base_internal::premmap_hooks_.empty()) {
    InvokePreMmapHookSlow(start, size, protection, flags, fd, offset);
  }
}

inline void MallocHook::InvokeMmapHook(const void* result,
                                       const void* start,
                                       size_t size,
                                       int protection,
                                       int flags,
                                       int fd,
                                       off_t offset) {
  if (!absl::base_internal::mmap_hooks_.empty()) {
    InvokeMmapHookSlow(result, start, size, protection, flags, fd, offset);
  }
}

inline bool MallocHook::InvokeMmapReplacement(const void* start,
                                              size_t size,
                                              int protection,
                                              int flags,
                                              int fd,
                                              off_t offset,
                                              void** result) {
  if (!absl::base_internal::mmap_replacement_.empty()) {
    return InvokeMmapReplacementSlow(start, size,
                                     protection, flags,
                                     fd, offset,
                                     result);
  }
  return false;
}

inline void MallocHook::InvokeMunmapHook(const void* start, size_t size) {
  if (!absl::base_internal::munmap_hooks_.empty()) {
    InvokeMunmapHookSlow(start, size);
  }
}

inline bool MallocHook::InvokeMunmapReplacement(
    const void* start, size_t size, int* result) {
  if (!absl::base_internal::mmap_replacement_.empty()) {
    return InvokeMunmapReplacementSlow(start, size, result);
  }
  return false;
}

inline void MallocHook::InvokeMremapHook(const void* result,
                                         const void* old_addr,
                                         size_t old_size,
                                         size_t new_size,
                                         int flags,
                                         const void* new_addr) {
  if (!absl::base_internal::mremap_hooks_.empty()) {
    InvokeMremapHookSlow(result, old_addr, old_size, new_size, flags, new_addr);
  }
}

inline void MallocHook::InvokePreSbrkHook(ptrdiff_t increment) {
  if (!absl::base_internal::presbrk_hooks_.empty() && increment != 0) {
    InvokePreSbrkHookSlow(increment);
  }
}

inline void MallocHook::InvokeSbrkHook(const void* result,
                                       ptrdiff_t increment) {
  if (!absl::base_internal::sbrk_hooks_.empty() && increment != 0) {
    InvokeSbrkHookSlow(result, increment);
  }
}

}  // namespace base_internal
}  // namespace absl
#endif  // ABSL_BASE_INTERNAL_MALLOC_HOOK_INVOKE_H_
