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

#include "absl/base/config.h"

#if ABSL_HAVE_MMAP
// Disable the glibc prototype of mremap(), as older versions of the
// system headers define this function with only four arguments,
// whereas newer versions allow an optional fifth argument:
#define mremap glibc_mremap
#include <sys/mman.h>
#undef mremap
#endif

#include "absl/base/internal/malloc_hook.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "absl/base/call_once.h"
#include "absl/base/internal/malloc_hook_invoke.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/macros.h"

// __THROW is defined in glibc systems.  It means, counter-intuitively,
// "This function will never throw an exception."  It's an optional
// optimization tool, but we may need to use it to match glibc prototypes.
#ifndef __THROW    // I guess we're not on a glibc system
# define __THROW   // __THROW is just an optimization, so ok to make it ""
#endif

namespace absl {
namespace base_internal {
namespace {

void RemoveInitialHooksAndCallInitializers();  // below.

absl::once_flag once;

// These hooks are installed in MallocHook as the only initial hooks.  The first
// hook that is called will run RemoveInitialHooksAndCallInitializers (see the
// definition below) and then redispatch to any malloc hooks installed by
// RemoveInitialHooksAndCallInitializers.
//
// Note(llib): there is a possibility of a race in the event that there are
// multiple threads running before the first allocation.  This is pretty
// difficult to achieve, but if it is then multiple threads may concurrently do
// allocations.  The first caller will call
// RemoveInitialHooksAndCallInitializers via one of the initial hooks.  A
// concurrent allocation may, depending on timing either:
// * still have its initial malloc hook installed, run that and block on waiting
//   for the first caller to finish its call to
//   RemoveInitialHooksAndCallInitializers, and proceed normally.
// * occur some time during the RemoveInitialHooksAndCallInitializers call, at
//   which point there could be no initial hooks and the subsequent hooks that
//   are about to be set up by RemoveInitialHooksAndCallInitializers haven't
//   been installed yet.  I think the worst we can get is that some allocations
//   will not get reported to some hooks set by the initializers called from
//   RemoveInitialHooksAndCallInitializers.

void InitialNewHook(const void* ptr, size_t size) {
  absl::call_once(once, RemoveInitialHooksAndCallInitializers);
  MallocHook::InvokeNewHook(ptr, size);
}

void InitialPreMMapHook(const void* start,
                               size_t size,
                               int protection,
                               int flags,
                               int fd,
                               off_t offset) {
  absl::call_once(once, RemoveInitialHooksAndCallInitializers);
  MallocHook::InvokePreMmapHook(start, size, protection, flags, fd, offset);
}

void InitialPreSbrkHook(ptrdiff_t increment) {
  absl::call_once(once, RemoveInitialHooksAndCallInitializers);
  MallocHook::InvokePreSbrkHook(increment);
}

// This function is called at most once by one of the above initial malloc
// hooks.  It removes all initial hooks and initializes all other clients that
// want to get control at the very first memory allocation.  The initializers
// may assume that the initial malloc hooks have been removed.  The initializers
// may set up malloc hooks and allocate memory.
void RemoveInitialHooksAndCallInitializers() {
  ABSL_RAW_CHECK(MallocHook::RemoveNewHook(&InitialNewHook), "");
  ABSL_RAW_CHECK(MallocHook::RemovePreMmapHook(&InitialPreMMapHook), "");
  ABSL_RAW_CHECK(MallocHook::RemovePreSbrkHook(&InitialPreSbrkHook), "");
}

}  // namespace
}  // namespace base_internal
}  // namespace absl

namespace absl {
namespace base_internal {

// This lock is shared between all implementations of HookList::Add & Remove.
// The potential for contention is very small.  This needs to be a SpinLock and
// not a Mutex since it's possible for Mutex locking to allocate memory (e.g.,
// per-thread allocation in debug builds), which could cause infinite recursion.
static absl::base_internal::SpinLock hooklist_spinlock(
    absl::base_internal::kLinkerInitialized);

template <typename T>
bool HookList<T>::Add(T value_as_t) {
  if (value_as_t == T()) {
    return false;
  }
  absl::base_internal::SpinLockHolder l(&hooklist_spinlock);
  // Find the first slot in data that is 0.
  int index = 0;
  while ((index < kHookListMaxValues) &&
         (priv_data[index].load(std::memory_order_relaxed) != 0)) {
    ++index;
  }
  if (index == kHookListMaxValues) {
    return false;
  }
  int prev_num_hooks = priv_end.load(std::memory_order_acquire);
  priv_data[index].store(reinterpret_cast<intptr_t>(value_as_t),
                         std::memory_order_release);
  if (prev_num_hooks <= index) {
    priv_end.store(index + 1, std::memory_order_release);
  }
  return true;
}

template <typename T>
bool HookList<T>::Remove(T value_as_t) {
  if (value_as_t == T()) {
    return false;
  }
  absl::base_internal::SpinLockHolder l(&hooklist_spinlock);
  int hooks_end = priv_end.load(std::memory_order_acquire);
  int index = 0;
  while (index < hooks_end &&
         value_as_t != reinterpret_cast<T>(
                           priv_data[index].load(std::memory_order_acquire))) {
    ++index;
  }
  if (index == hooks_end) {
    return false;
  }
  priv_data[index].store(0, std::memory_order_release);
  if (hooks_end == index + 1) {
    // Adjust hooks_end down to the lowest possible value.
    hooks_end = index;
    while ((hooks_end > 0) &&
           (priv_data[hooks_end - 1].load(std::memory_order_acquire) == 0)) {
      --hooks_end;
    }
    priv_end.store(hooks_end, std::memory_order_release);
  }
  return true;
}

template <typename T>
int HookList<T>::Traverse(T* output_array, int n) const {
  int hooks_end = priv_end.load(std::memory_order_acquire);
  int actual_hooks_end = 0;
  for (int i = 0; i < hooks_end && n > 0; ++i) {
    T data = reinterpret_cast<T>(priv_data[i].load(std::memory_order_acquire));
    if (data != T()) {
      *output_array++ = data;
      ++actual_hooks_end;
      --n;
    }
  }
  return actual_hooks_end;
}

// Initialize a HookList (optionally with the given initial_value in index 0).
#define INIT_HOOK_LIST { {0}, {{}} }
#define INIT_HOOK_LIST_WITH_VALUE(initial_value) \
  { {1}, { {reinterpret_cast<intptr_t>(initial_value)} } }

// Explicit instantiation for malloc_hook_test.cc.  This ensures all the methods
// are instantiated.
template struct HookList<MallocHook::NewHook>;

HookList<MallocHook::NewHook> new_hooks_ =
    INIT_HOOK_LIST_WITH_VALUE(&InitialNewHook);
HookList<MallocHook::DeleteHook> delete_hooks_ = INIT_HOOK_LIST;
HookList<MallocHook::SampledNewHook> sampled_new_hooks_ = INIT_HOOK_LIST;
HookList<MallocHook::SampledDeleteHook> sampled_delete_hooks_ = INIT_HOOK_LIST;
HookList<MallocHook::PreMmapHook> premmap_hooks_ =
    INIT_HOOK_LIST_WITH_VALUE(&InitialPreMMapHook);
HookList<MallocHook::MmapHook> mmap_hooks_ = INIT_HOOK_LIST;
HookList<MallocHook::MunmapHook> munmap_hooks_ = INIT_HOOK_LIST;
HookList<MallocHook::MremapHook> mremap_hooks_ = INIT_HOOK_LIST;
HookList<MallocHook::PreSbrkHook> presbrk_hooks_ =
    INIT_HOOK_LIST_WITH_VALUE(InitialPreSbrkHook);
HookList<MallocHook::SbrkHook> sbrk_hooks_ = INIT_HOOK_LIST;

// These lists contain either 0 or 1 hooks.
HookList<MallocHook::MmapReplacement> mmap_replacement_ = INIT_HOOK_LIST;
HookList<MallocHook::MunmapReplacement> munmap_replacement_ = INIT_HOOK_LIST;

#undef INIT_HOOK_LIST_WITH_VALUE
#undef INIT_HOOK_LIST

}  // namespace base_internal
}  // namespace absl

// These are available as C bindings as well as C++, hence their
// definition outside the MallocHook class.
extern "C"
int MallocHook_AddNewHook(MallocHook_NewHook hook) {
  return absl::base_internal::new_hooks_.Add(hook);
}

extern "C"
int MallocHook_RemoveNewHook(MallocHook_NewHook hook) {
  return absl::base_internal::new_hooks_.Remove(hook);
}

extern "C"
int MallocHook_AddDeleteHook(MallocHook_DeleteHook hook) {
  return absl::base_internal::delete_hooks_.Add(hook);
}

extern "C"
int MallocHook_RemoveDeleteHook(MallocHook_DeleteHook hook) {
  return absl::base_internal::delete_hooks_.Remove(hook);
}

extern "C" int MallocHook_AddSampledNewHook(MallocHook_SampledNewHook hook) {
  return absl::base_internal::sampled_new_hooks_.Add(hook);
}

extern "C" int MallocHook_RemoveSampledNewHook(MallocHook_SampledNewHook hook) {
  return absl::base_internal::sampled_new_hooks_.Remove(hook);
}

extern "C" int MallocHook_AddSampledDeleteHook(
    MallocHook_SampledDeleteHook hook) {
  return absl::base_internal::sampled_delete_hooks_.Add(hook);
}

extern "C" int MallocHook_RemoveSampledDeleteHook(
    MallocHook_SampledDeleteHook hook) {
  return absl::base_internal::sampled_delete_hooks_.Remove(hook);
}

extern "C"
int MallocHook_AddPreMmapHook(MallocHook_PreMmapHook hook) {
  return absl::base_internal::premmap_hooks_.Add(hook);
}

extern "C"
int MallocHook_RemovePreMmapHook(MallocHook_PreMmapHook hook) {
  return absl::base_internal::premmap_hooks_.Remove(hook);
}

extern "C"
int MallocHook_SetMmapReplacement(MallocHook_MmapReplacement hook) {
  // NOTE this is a best effort CHECK. Concurrent sets could succeed since
  // this test is outside of the Add spin lock.
  ABSL_RAW_CHECK(absl::base_internal::mmap_replacement_.empty(),
                 "Only one MMapReplacement is allowed.");
  return absl::base_internal::mmap_replacement_.Add(hook);
}

extern "C"
int MallocHook_RemoveMmapReplacement(MallocHook_MmapReplacement hook) {
  return absl::base_internal::mmap_replacement_.Remove(hook);
}

extern "C"
int MallocHook_AddMmapHook(MallocHook_MmapHook hook) {
  return absl::base_internal::mmap_hooks_.Add(hook);
}

extern "C"
int MallocHook_RemoveMmapHook(MallocHook_MmapHook hook) {
  return absl::base_internal::mmap_hooks_.Remove(hook);
}

extern "C"
int MallocHook_AddMunmapHook(MallocHook_MunmapHook hook) {
  return absl::base_internal::munmap_hooks_.Add(hook);
}

extern "C"
int MallocHook_RemoveMunmapHook(MallocHook_MunmapHook hook) {
  return absl::base_internal::munmap_hooks_.Remove(hook);
}

extern "C"
int MallocHook_SetMunmapReplacement(MallocHook_MunmapReplacement hook) {
  // NOTE this is a best effort CHECK. Concurrent sets could succeed since
  // this test is outside of the Add spin lock.
  ABSL_RAW_CHECK(absl::base_internal::munmap_replacement_.empty(),
                 "Only one MunmapReplacement is allowed.");
  return absl::base_internal::munmap_replacement_.Add(hook);
}

extern "C"
int MallocHook_RemoveMunmapReplacement(MallocHook_MunmapReplacement hook) {
  return absl::base_internal::munmap_replacement_.Remove(hook);
}

extern "C"
int MallocHook_AddMremapHook(MallocHook_MremapHook hook) {
  return absl::base_internal::mremap_hooks_.Add(hook);
}

extern "C"
int MallocHook_RemoveMremapHook(MallocHook_MremapHook hook) {
  return absl::base_internal::mremap_hooks_.Remove(hook);
}

extern "C"
int MallocHook_AddPreSbrkHook(MallocHook_PreSbrkHook hook) {
  return absl::base_internal::presbrk_hooks_.Add(hook);
}

extern "C"
int MallocHook_RemovePreSbrkHook(MallocHook_PreSbrkHook hook) {
  return absl::base_internal::presbrk_hooks_.Remove(hook);
}

extern "C"
int MallocHook_AddSbrkHook(MallocHook_SbrkHook hook) {
  return absl::base_internal::sbrk_hooks_.Add(hook);
}

extern "C"
int MallocHook_RemoveSbrkHook(MallocHook_SbrkHook hook) {
  return absl::base_internal::sbrk_hooks_.Remove(hook);
}

namespace absl {
namespace base_internal {

// Note: embedding the function calls inside the traversal of HookList would be
// very confusing, as it is legal for a hook to remove itself and add other
// hooks.  Doing traversal first, and then calling the hooks ensures we only
// call the hooks registered at the start.
#define INVOKE_HOOKS(HookType, hook_list, args)                    \
  do {                                                             \
    HookType hooks[kHookListMaxValues];                            \
    int num_hooks = hook_list.Traverse(hooks, kHookListMaxValues); \
    for (int i = 0; i < num_hooks; ++i) {                          \
      (*hooks[i]) args;                                            \
    }                                                              \
  } while (0)

// There should only be one replacement. Return the result of the first
// one, or false if there is none.
#define INVOKE_REPLACEMENT(HookType, hook_list, args)              \
  do {                                                             \
    HookType hooks[kHookListMaxValues];                            \
    int num_hooks = hook_list.Traverse(hooks, kHookListMaxValues); \
    return (num_hooks > 0 && (*hooks[0])args);                     \
  } while (0)

void MallocHook::InvokeNewHookSlow(const void* ptr, size_t size) {
  INVOKE_HOOKS(NewHook, new_hooks_, (ptr, size));
}

void MallocHook::InvokeDeleteHookSlow(const void* ptr) {
  INVOKE_HOOKS(DeleteHook, delete_hooks_, (ptr));
}

void MallocHook::InvokeSampledNewHookSlow(const SampledAlloc* sampled_alloc) {
  INVOKE_HOOKS(SampledNewHook, sampled_new_hooks_, (sampled_alloc));
}

void MallocHook::InvokeSampledDeleteHookSlow(AllocHandle handle) {
  INVOKE_HOOKS(SampledDeleteHook, sampled_delete_hooks_, (handle));
}

void MallocHook::InvokePreMmapHookSlow(const void* start,
                                       size_t size,
                                       int protection,
                                       int flags,
                                       int fd,
                                       off_t offset) {
  INVOKE_HOOKS(PreMmapHook, premmap_hooks_, (start, size, protection, flags, fd,
                                            offset));
}

void MallocHook::InvokeMmapHookSlow(const void* result,
                                    const void* start,
                                    size_t size,
                                    int protection,
                                    int flags,
                                    int fd,
                                    off_t offset) {
  INVOKE_HOOKS(MmapHook, mmap_hooks_, (result, start, size, protection, flags,
                                       fd, offset));
}

bool MallocHook::InvokeMmapReplacementSlow(const void* start,
                                           size_t size,
                                           int protection,
                                           int flags,
                                           int fd,
                                           off_t offset,
                                           void** result) {
  INVOKE_REPLACEMENT(MmapReplacement, mmap_replacement_,
                      (start, size, protection, flags, fd, offset, result));
}

void MallocHook::InvokeMunmapHookSlow(const void* start, size_t size) {
  INVOKE_HOOKS(MunmapHook, munmap_hooks_, (start, size));
}

bool MallocHook::InvokeMunmapReplacementSlow(const void* start,
                                             size_t size,
                                             int* result) {
  INVOKE_REPLACEMENT(MunmapReplacement, munmap_replacement_,
                     (start, size, result));
}

void MallocHook::InvokeMremapHookSlow(const void* result,
                                      const void* old_addr,
                                      size_t old_size,
                                      size_t new_size,
                                      int flags,
                                      const void* new_addr) {
  INVOKE_HOOKS(MremapHook, mremap_hooks_, (result, old_addr, old_size, new_size,
                                           flags, new_addr));
}

void MallocHook::InvokePreSbrkHookSlow(ptrdiff_t increment) {
  INVOKE_HOOKS(PreSbrkHook, presbrk_hooks_, (increment));
}

void MallocHook::InvokeSbrkHookSlow(const void* result, ptrdiff_t increment) {
  INVOKE_HOOKS(SbrkHook, sbrk_hooks_, (result, increment));
}

#undef INVOKE_HOOKS
#undef INVOKE_REPLACEMENT

}  // namespace base_internal
}  // namespace absl

ABSL_DEFINE_ATTRIBUTE_SECTION_VARS(malloc_hook);
ABSL_DECLARE_ATTRIBUTE_SECTION_VARS(malloc_hook);
// actual functions are in this file, malloc_hook.cc, and low_level_alloc.cc
ABSL_DEFINE_ATTRIBUTE_SECTION_VARS(google_malloc);
ABSL_DECLARE_ATTRIBUTE_SECTION_VARS(google_malloc);
ABSL_DEFINE_ATTRIBUTE_SECTION_VARS(blink_malloc);
ABSL_DECLARE_ATTRIBUTE_SECTION_VARS(blink_malloc);

#define ADDR_IN_ATTRIBUTE_SECTION(addr, name)                         \
  (reinterpret_cast<uintptr_t>(ABSL_ATTRIBUTE_SECTION_START(name)) <= \
       reinterpret_cast<uintptr_t>(addr) &&                           \
   reinterpret_cast<uintptr_t>(addr) <                                \
       reinterpret_cast<uintptr_t>(ABSL_ATTRIBUTE_SECTION_STOP(name)))

// Return true iff 'caller' is a return address within a function
// that calls one of our hooks via MallocHook:Invoke*.
// A helper for GetCallerStackTrace.
static inline bool InHookCaller(const void* caller) {
  return ADDR_IN_ATTRIBUTE_SECTION(caller, google_malloc) ||
         ADDR_IN_ATTRIBUTE_SECTION(caller, malloc_hook) ||
         ADDR_IN_ATTRIBUTE_SECTION(caller, blink_malloc);
  // We can use one section for everything except tcmalloc_or_debug
  // due to its special linkage mode, which prevents merging of the sections.
}

#undef ADDR_IN_ATTRIBUTE_SECTION

static absl::once_flag in_hook_caller_once;

static void InitializeInHookCaller() {
  ABSL_INIT_ATTRIBUTE_SECTION_VARS(malloc_hook);
  if (ABSL_ATTRIBUTE_SECTION_START(malloc_hook) ==
      ABSL_ATTRIBUTE_SECTION_STOP(malloc_hook)) {
    ABSL_RAW_LOG(ERROR,
                 "malloc_hook section is missing, "
                 "thus InHookCaller is broken!");
  }
  ABSL_INIT_ATTRIBUTE_SECTION_VARS(google_malloc);
  if (ABSL_ATTRIBUTE_SECTION_START(google_malloc) ==
      ABSL_ATTRIBUTE_SECTION_STOP(google_malloc)) {
    ABSL_RAW_LOG(ERROR,
                 "google_malloc section is missing, "
                 "thus InHookCaller is broken!");
  }
  ABSL_INIT_ATTRIBUTE_SECTION_VARS(blink_malloc);
}

// We can improve behavior/compactness of this function
// if we pass a generic test function (with a generic arg)
// into the implementations for get_stack_trace_fn instead of the skip_count.
extern "C" int MallocHook_GetCallerStackTrace(
    void** result, int max_depth, int skip_count,
    MallocHook_GetStackTraceFn get_stack_trace_fn) {
  if (!ABSL_HAVE_ATTRIBUTE_SECTION) {
    // Fall back to get_stack_trace_fn and good old but fragile frame skip
    // counts.
    // Note: this path is inaccurate when a hook is not called directly by an
    // allocation function but is daisy-chained through another hook,
    // search for MallocHook::(Get|Set|Invoke)* to find such cases.
#ifdef NDEBUG
    return get_stack_trace_fn(result, max_depth, skip_count);
#else
    return get_stack_trace_fn(result, max_depth, skip_count + 1);
#endif
    // due to -foptimize-sibling-calls in opt mode
    // there's no need for extra frame skip here then
  }
  absl::call_once(in_hook_caller_once, InitializeInHookCaller);
  // MallocHook caller determination via InHookCaller works, use it:
  static const int kMaxSkip = 32 + 6 + 3;
    // Constant tuned to do just one get_stack_trace_fn call below in practice
    // and not get many frames that we don't actually need:
    // currently max passed max_depth is 32,
    // max passed/needed skip_count is 6
    // and 3 is to account for some hook daisy chaining.
  static const int kStackSize = kMaxSkip + 1;
  void* stack[kStackSize];
  int depth =
      get_stack_trace_fn(stack, kStackSize, 1);  // skip this function frame
  if (depth == 0)
    // silently propagate cases when get_stack_trace_fn does not work
    return 0;
  for (int i = depth - 1; i >= 0; --i) {  // stack[0] is our immediate caller
    if (InHookCaller(stack[i])) {
      i += 1;  // skip hook caller frame
      depth -= i;  // correct depth
      if (depth > max_depth) depth = max_depth;
      std::copy(stack + i, stack + i + depth, result);
      if (depth < max_depth  &&  depth + i == kStackSize) {
        // get frames for the missing depth
        depth += get_stack_trace_fn(result + depth, max_depth - depth,
                                    1 + kStackSize);
      }
      return depth;
    }
  }
  ABSL_RAW_LOG(WARNING,
               "Hooked allocator frame not found, returning empty trace");
  // If this happens try increasing kMaxSkip
  // or else something must be wrong with InHookCaller,
  // e.g. for every section used in InHookCaller
  // all functions in that section must be inside the same library.
  return 0;
}

// On systems where we know how, we override mmap/munmap/mremap/sbrk
// to provide support for calling the related hooks (in addition,
// of course, to doing what these functions normally do).

// The ABSL_MALLOC_HOOK_MMAP_DISABLE macro disables mmap/munmap interceptors.
// Dynamic tools that intercept mmap/munmap can't be linked together with
// malloc_hook interceptors. We disable the malloc_hook interceptors for the
// widely-used dynamic tools, i.e. ThreadSanitizer and MemorySanitizer, but
// still allow users to disable this in special cases that can't be easily
// detected during compilation, via -DABSL_MALLOC_HOOK_MMAP_DISABLE or #define
// ABSL_MALLOC_HOOK_MMAP_DISABLE.
//
// TODO(absl-team): Remove MALLOC_HOOK_MMAP_DISABLE in CROSSTOOL for tsan and
// msan config; Replace MALLOC_HOOK_MMAP_DISABLE with
// ABSL_MALLOC_HOOK_MMAP_DISABLE for other special cases.
#if !defined(THREAD_SANITIZER) && !defined(MEMORY_SANITIZER) && \
    !defined(ABSL_MALLOC_HOOK_MMAP_DISABLE) && defined(__linux__)
#include "absl/base/internal/malloc_hook_mmap_linux.inc"

#elif ABSL_HAVE_MMAP

namespace absl {
namespace base_internal {

// static
void* MallocHook::UnhookedMMap(void* start, size_t size, int protection,
                               int flags, int fd, off_t offset) {
  void* result;
  if (!MallocHook::InvokeMmapReplacement(
          start, size, protection, flags, fd, offset, &result)) {
    result = mmap(start, size, protection, flags, fd, offset);
  }
  return result;
}

// static
int MallocHook::UnhookedMUnmap(void* start, size_t size) {
  int result;
  if (!MallocHook::InvokeMunmapReplacement(start, size, &result)) {
    result = munmap(start, size);
  }
  return result;
}

}  // namespace base_internal
}  // namespace absl

#endif
