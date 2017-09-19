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
//

// Some of our malloc implementations can invoke the following hooks whenever
// memory is allocated or deallocated.  MallocHook is thread-safe, and things
// you do before calling AddFooHook(MyHook) are visible to any resulting calls
// to MyHook.  Hooks must be thread-safe.  If you write:
//
//   CHECK(MallocHook::AddNewHook(&MyNewHook));
//
// MyNewHook will be invoked in subsequent calls in the current thread, but
// there are no guarantees on when it might be invoked in other threads.
//
// There are a limited number of slots available for each hook type.  Add*Hook
// will return false if there are no slots available.  Remove*Hook will return
// false if the given hook was not already installed.
//
// The order in which individual hooks are called in Invoke*Hook is undefined.
//
// It is safe for a hook to remove itself within Invoke*Hook and add other
// hooks.  Any hooks added inside a hook invocation (for the same hook type)
// will not be invoked for the current invocation.
//
// One important user of these hooks is the heap profiler.
//
// CAVEAT: If you add new MallocHook::Invoke* calls then those calls must be
// directly in the code of the (de)allocation function that is provided to the
// user and that function must have an ABSL_ATTRIBUTE_SECTION(malloc_hook)
// attribute.
//
// Note: the Invoke*Hook() functions are defined in malloc_hook-inl.h.  If you
// need to invoke a hook (which you shouldn't unless you're part of tcmalloc),
// be sure to #include malloc_hook-inl.h in addition to malloc_hook.h.
//
// NOTE FOR C USERS: If you want to use malloc_hook functionality from
// a C program, #include malloc_hook_c.h instead of this file.
//
// IWYU pragma: private, include "base/malloc_hook.h"

#ifndef ABSL_BASE_INTERNAL_MALLOC_HOOK_H_
#define ABSL_BASE_INTERNAL_MALLOC_HOOK_H_

#include <sys/types.h>
#include <cstddef>

#include "absl/base/config.h"
#include "absl/base/internal/malloc_hook_c.h"
#include "absl/base/port.h"

namespace absl {
namespace base_internal {

// Note: malloc_hook_c.h defines MallocHook_*Hook and
// MallocHook_{Add,Remove}*Hook.  The version of these inside the MallocHook
// class are defined in terms of the malloc_hook_c version.  See malloc_hook_c.h
// for details of these types/functions.

class MallocHook {
 public:
  // The NewHook is invoked whenever an object is being allocated.
  // Object pointer and size are passed in.
  // It may be passed null pointer if the allocator returned null.
  typedef MallocHook_NewHook NewHook;
  inline static bool AddNewHook(NewHook hook) {
    return MallocHook_AddNewHook(hook);
  }
  inline static bool RemoveNewHook(NewHook hook) {
    return MallocHook_RemoveNewHook(hook);
  }
  inline static void InvokeNewHook(const void* ptr, size_t size);

  // The DeleteHook is invoked whenever an object is being deallocated.
  // Object pointer is passed in.
  // It may be passed null pointer if the caller is trying to delete null.
  typedef MallocHook_DeleteHook DeleteHook;
  inline static bool AddDeleteHook(DeleteHook hook) {
    return MallocHook_AddDeleteHook(hook);
  }
  inline static bool RemoveDeleteHook(DeleteHook hook) {
    return MallocHook_RemoveDeleteHook(hook);
  }
  inline static void InvokeDeleteHook(const void* ptr);

  // The SampledNewHook is invoked for some subset of object allocations
  // according to the sampling policy of an allocator such as tcmalloc.
  // SampledAlloc has the following fields:
  //  * AllocHandle handle: to be set to an effectively unique value (in this
  //    process) by allocator.
  //  * size_t allocated_size: space actually used by allocator to host
  //    the object.
  //  * int stack_depth and const void* stack: invocation stack for
  //    the allocation.
  // The allocator invoking the hook should record the handle value and later
  // call InvokeSampledDeleteHook() with that value.
  typedef MallocHook_SampledNewHook SampledNewHook;
  typedef MallocHook_SampledAlloc SampledAlloc;
  inline static bool AddSampledNewHook(SampledNewHook hook) {
    return MallocHook_AddSampledNewHook(hook);
  }
  inline static bool RemoveSampledNewHook(SampledNewHook hook) {
    return MallocHook_RemoveSampledNewHook(hook);
  }
  inline static void InvokeSampledNewHook(const SampledAlloc* sampled_alloc);

  // The SampledDeleteHook is invoked whenever an object previously chosen
  // by an allocator for sampling is being deallocated.
  // The handle identifying the object --as previously chosen by
  // InvokeSampledNewHook()-- is passed in.
  typedef MallocHook_SampledDeleteHook SampledDeleteHook;
  typedef MallocHook_AllocHandle AllocHandle;
  inline static bool AddSampledDeleteHook(SampledDeleteHook hook) {
    return MallocHook_AddSampledDeleteHook(hook);
  }
  inline static bool RemoveSampledDeleteHook(SampledDeleteHook hook) {
    return MallocHook_RemoveSampledDeleteHook(hook);
  }
  inline static void InvokeSampledDeleteHook(AllocHandle handle);

  // The PreMmapHook is invoked with mmap's or mmap64's arguments just
  // before the mmap/mmap64 call is actually made.  Such a hook may be useful
  // in memory limited contexts, to catch allocations that will exceed
  // a memory limit, and take outside actions to increase that limit.
  typedef MallocHook_PreMmapHook PreMmapHook;
  inline static bool AddPreMmapHook(PreMmapHook hook) {
    return MallocHook_AddPreMmapHook(hook);
  }
  inline static bool RemovePreMmapHook(PreMmapHook hook) {
    return MallocHook_RemovePreMmapHook(hook);
  }
  inline static void InvokePreMmapHook(const void* start,
                                       size_t size,
                                       int protection,
                                       int flags,
                                       int fd,
                                       off_t offset);

  // The MmapReplacement is invoked with mmap's arguments and place to put the
  // result into after the PreMmapHook but before the mmap/mmap64 call is
  // actually made.
  // The MmapReplacement should return true if it handled the call, or false
  // if it is still necessary to call mmap/mmap64.
  // This should be used only by experts, and users must be be
  // extremely careful to avoid recursive calls to mmap. The replacement
  // should be async signal safe.
  // Only one MmapReplacement is supported. After setting an MmapReplacement
  // you must call RemoveMmapReplacement before calling SetMmapReplacement
  // again.
  typedef MallocHook_MmapReplacement MmapReplacement;
  inline static bool SetMmapReplacement(MmapReplacement hook) {
    return MallocHook_SetMmapReplacement(hook);
  }
  inline static bool RemoveMmapReplacement(MmapReplacement hook) {
    return MallocHook_RemoveMmapReplacement(hook);
  }
  inline static bool InvokeMmapReplacement(const void* start,
                                           size_t size,
                                           int protection,
                                           int flags,
                                           int fd,
                                           off_t offset,
                                           void** result);


  // The MmapHook is invoked with mmap's return value and arguments whenever
  // a region of memory has been just mapped.
  // It may be passed MAP_FAILED if the mmap failed.
  typedef MallocHook_MmapHook MmapHook;
  inline static bool AddMmapHook(MmapHook hook) {
    return MallocHook_AddMmapHook(hook);
  }
  inline static bool RemoveMmapHook(MmapHook hook) {
    return MallocHook_RemoveMmapHook(hook);
  }
  inline static void InvokeMmapHook(const void* result,
                                    const void* start,
                                    size_t size,
                                    int protection,
                                    int flags,
                                    int fd,
                                    off_t offset);

  // The MunmapReplacement is invoked with munmap's arguments and place to put
  // the result into just before the munmap call is actually made.
  // The MunmapReplacement should return true if it handled the call, or false
  // if it is still necessary to call munmap.
  // This should be used only by experts. The replacement should be
  // async signal safe.
  // Only one MunmapReplacement is supported. After setting an
  // MunmapReplacement you must call RemoveMunmapReplacement before
  // calling SetMunmapReplacement again.
  typedef MallocHook_MunmapReplacement MunmapReplacement;
  inline static bool SetMunmapReplacement(MunmapReplacement hook) {
    return MallocHook_SetMunmapReplacement(hook);
  }
  inline static bool RemoveMunmapReplacement(MunmapReplacement hook) {
    return MallocHook_RemoveMunmapReplacement(hook);
  }
  inline static bool InvokeMunmapReplacement(const void* start,
                                             size_t size,
                                             int* result);

  // The MunmapHook is invoked with munmap's arguments just before the munmap
  // call is actually made.
  // TODO(maxim): Rename this to PreMunmapHook for consistency with PreMmapHook
  // and PreSbrkHook.
  typedef MallocHook_MunmapHook MunmapHook;
  inline static bool AddMunmapHook(MunmapHook hook) {
    return MallocHook_AddMunmapHook(hook);
  }
  inline static bool RemoveMunmapHook(MunmapHook hook) {
    return MallocHook_RemoveMunmapHook(hook);
  }
  inline static void InvokeMunmapHook(const void* start, size_t size);

  // The MremapHook is invoked with mremap's return value and arguments
  // whenever a region of memory has been just remapped.
  typedef MallocHook_MremapHook MremapHook;
  inline static bool AddMremapHook(MremapHook hook) {
    return MallocHook_AddMremapHook(hook);
  }
  inline static bool RemoveMremapHook(MremapHook hook) {
    return MallocHook_RemoveMremapHook(hook);
  }
  inline static void InvokeMremapHook(const void* result,
                                      const void* old_addr,
                                      size_t old_size,
                                      size_t new_size,
                                      int flags,
                                      const void* new_addr);

  // The PreSbrkHook is invoked with sbrk's argument just before sbrk is called
  // -- except when the increment is 0.  This is because sbrk(0) is often called
  // to get the top of the memory stack, and is not actually a
  // memory-allocation call.  It may be useful in memory-limited contexts,
  // to catch allocations that will exceed the limit and take outside
  // actions to increase such a limit.
  typedef MallocHook_PreSbrkHook PreSbrkHook;
  inline static bool AddPreSbrkHook(PreSbrkHook hook) {
    return MallocHook_AddPreSbrkHook(hook);
  }
  inline static bool RemovePreSbrkHook(PreSbrkHook hook) {
    return MallocHook_RemovePreSbrkHook(hook);
  }
  inline static void InvokePreSbrkHook(ptrdiff_t increment);

  // The SbrkHook is invoked with sbrk's result and argument whenever sbrk
  // has just executed -- except when the increment is 0.
  // This is because sbrk(0) is often called to get the top of the memory stack,
  // and is not actually a memory-allocation call.
  typedef MallocHook_SbrkHook SbrkHook;
  inline static bool AddSbrkHook(SbrkHook hook) {
    return MallocHook_AddSbrkHook(hook);
  }
  inline static bool RemoveSbrkHook(SbrkHook hook) {
    return MallocHook_RemoveSbrkHook(hook);
  }
  inline static void InvokeSbrkHook(const void* result, ptrdiff_t increment);

  // Pointer to a absl::GetStackTrace implementation, following the API in
  // base/stacktrace.h.
  using GetStackTraceFn = int (*)(void**, int, int);

  // Get the current stack trace.  Try to skip all routines up to and
  // including the caller of MallocHook::Invoke*.
  // Use "skip_count" (similarly to absl::GetStackTrace from stacktrace.h)
  // as a hint about how many routines to skip if better information
  // is not available.
  // Stack trace is filled into *result up to the size of max_depth.
  // The actual number of stack frames filled is returned.
  inline static int GetCallerStackTrace(void** result, int max_depth,
                                        int skip_count,
                                        GetStackTraceFn get_stack_trace_fn) {
    return MallocHook_GetCallerStackTrace(result, max_depth, skip_count,
                                          get_stack_trace_fn);
  }

#if ABSL_HAVE_MMAP
  // Unhooked versions of mmap() and munmap().   These should be used
  // only by experts, since they bypass heapchecking, etc.
  // Note: These do not run hooks, but they still use the MmapReplacement
  // and MunmapReplacement.
  static void* UnhookedMMap(void* start, size_t size, int protection, int flags,
                            int fd, off_t offset);
  static int UnhookedMUnmap(void* start, size_t size);
#endif

 private:
  // Slow path versions of Invoke*Hook.
  static void InvokeNewHookSlow(const void* ptr,
                                size_t size) ABSL_ATTRIBUTE_COLD;
  static void InvokeDeleteHookSlow(const void* ptr) ABSL_ATTRIBUTE_COLD;
  static void InvokeSampledNewHookSlow(const SampledAlloc* sampled_alloc)
      ABSL_ATTRIBUTE_COLD;
  static void InvokeSampledDeleteHookSlow(AllocHandle handle)
      ABSL_ATTRIBUTE_COLD;
  static void InvokePreMmapHookSlow(const void* start, size_t size,
                                    int protection, int flags, int fd,
                                    off_t offset) ABSL_ATTRIBUTE_COLD;
  static void InvokeMmapHookSlow(const void* result, const void* start,
                                 size_t size, int protection, int flags, int fd,
                                 off_t offset) ABSL_ATTRIBUTE_COLD;
  static bool InvokeMmapReplacementSlow(const void* start, size_t size,
                                        int protection, int flags, int fd,
                                        off_t offset,
                                        void** result) ABSL_ATTRIBUTE_COLD;
  static void InvokeMunmapHookSlow(const void* ptr,
                                   size_t size) ABSL_ATTRIBUTE_COLD;
  static bool InvokeMunmapReplacementSlow(const void* ptr, size_t size,
                                          int* result) ABSL_ATTRIBUTE_COLD;
  static void InvokeMremapHookSlow(const void* result, const void* old_addr,
                                   size_t old_size, size_t new_size, int flags,
                                   const void* new_addr) ABSL_ATTRIBUTE_COLD;
  static void InvokePreSbrkHookSlow(ptrdiff_t increment) ABSL_ATTRIBUTE_COLD;
  static void InvokeSbrkHookSlow(const void* result,
                                 ptrdiff_t increment) ABSL_ATTRIBUTE_COLD;
};

}  // namespace base_internal
}  // namespace absl
#endif  // ABSL_BASE_INTERNAL_MALLOC_HOOK_H_
