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
// We define mmap() and mmap64(), which somewhat reimplements libc's mmap
// syscall stubs.  Unfortunately libc only exports the stubs via weak symbols
// (which we're overriding with our mmap64() and mmap() wrappers) so we can't
// just call through to them.

#ifndef __linux__
# error Should only be including malloc_hook_mmap_linux.h on linux systems.
#endif

#include <sys/mman.h>
#include <sys/types.h>
#ifdef __BIONIC__
#include <sys/syscall.h>
#else
#include <syscall.h>
#endif

#include <linux/unistd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>

#include "absl/base/internal/direct_mmap.h"

// SYS_mremap is not defined in Android.
#ifdef __BIONIC__
#ifndef SYS_mremap
#define SYS_mremap __NR_mremap
#endif
#endif  // __BIONIC__

// We put MallocHook::InvokeMmapHook calls right into mmap and mmap64, so that
// the stack frames in the caller's stack are at the same offsets for all the
// calls of memory allocating functions.

// Put all callers of MallocHook::Invoke* in this module into
// malloc_hook section,
// so that MallocHook::GetCallerStackTrace can function accurately:

// Make sure mmap doesn't get #define'd away by <sys/mman.h>
# undef mmap

extern "C" {
ABSL_ATTRIBUTE_SECTION(malloc_hook)
void* mmap64(void* start, size_t length, int prot, int flags, int fd,
             off64_t offset) __THROW;
ABSL_ATTRIBUTE_SECTION(malloc_hook)
void* mmap(void* start, size_t length, int prot, int flags, int fd,
           off_t offset) __THROW;
ABSL_ATTRIBUTE_SECTION(malloc_hook)
int munmap(void* start, size_t length) __THROW;
ABSL_ATTRIBUTE_SECTION(malloc_hook)
void* mremap(void* old_addr, size_t old_size, size_t new_size, int flags,
             ...) __THROW;
ABSL_ATTRIBUTE_SECTION(malloc_hook) void* sbrk(ptrdiff_t increment) __THROW;
}

extern "C" void* mmap64(void *start, size_t length, int prot, int flags,
                        int fd, off64_t offset) __THROW {
  absl::base_internal::MallocHook::InvokePreMmapHook(start, length, prot, flags,
                                                     fd, offset);
  void *result;
  if (!absl::base_internal::MallocHook::InvokeMmapReplacement(
          start, length, prot, flags, fd, offset, &result)) {
    result = absl::base_internal::DirectMmap(start, length, prot, flags, fd,
                                             offset);
  }
  absl::base_internal::MallocHook::InvokeMmapHook(result, start, length, prot,
                                                  flags, fd, offset);
  return result;
}

# if !defined(__USE_FILE_OFFSET64) || !defined(__REDIRECT_NTH)

extern "C" void* mmap(void *start, size_t length, int prot, int flags,
                      int fd, off_t offset) __THROW {
  absl::base_internal::MallocHook::InvokePreMmapHook(start, length, prot, flags,
                                                     fd, offset);
  void *result;
  if (!absl::base_internal::MallocHook::InvokeMmapReplacement(
          start, length, prot, flags, fd, offset, &result)) {
    result = absl::base_internal::DirectMmap(
        start, length, prot, flags, fd,
        static_cast<size_t>(offset)); // avoid sign extension
  }
  absl::base_internal::MallocHook::InvokeMmapHook(result, start, length, prot,
                                                  flags, fd, offset);
  return result;
}

# endif  // !defined(__USE_FILE_OFFSET64) || !defined(__REDIRECT_NTH)

extern "C" int munmap(void* start, size_t length) __THROW {
  absl::base_internal::MallocHook::InvokeMunmapHook(start, length);
  int result;
  if (!absl::base_internal::MallocHook::InvokeMunmapReplacement(start, length,
                                                                &result)) {
    result = absl::base_internal::DirectMunmap(start, length);
  }
  return result;
}

extern "C" void* mremap(void* old_addr, size_t old_size, size_t new_size,
                        int flags, ...) __THROW {
  va_list ap;
  va_start(ap, flags);
  void *new_address = va_arg(ap, void *);
  va_end(ap);
  void* result = reinterpret_cast<void*>(
      syscall(SYS_mremap, old_addr, old_size, new_size, flags, new_address));
  absl::base_internal::MallocHook::InvokeMremapHook(
      result, old_addr, old_size, new_size, flags, new_address);
  return result;
}

// sbrk cannot be intercepted on Android as there is no mechanism to
// invoke the original sbrk (since there is no __sbrk as with glibc).
#if !defined(__BIONIC__)
// libc's version:
extern "C" void* __sbrk(ptrdiff_t increment);

extern "C" void* sbrk(ptrdiff_t increment) __THROW {
  absl::base_internal::MallocHook::InvokePreSbrkHook(increment);
  void *result = __sbrk(increment);
  absl::base_internal::MallocHook::InvokeSbrkHook(result, increment);
  return result;
}
#endif  // !defined(__BIONIC__)

namespace absl {
namespace base_internal {

/*static*/void* MallocHook::UnhookedMMap(void *start, size_t length, int prot,
                                         int flags, int fd, off_t offset) {
  void* result;
  if (!MallocHook::InvokeMmapReplacement(
          start, length, prot, flags, fd, offset, &result)) {
    result = absl::base_internal::DirectMmap(start, length, prot, flags, fd,
                                             offset);
  }
  return result;
}

/*static*/int MallocHook::UnhookedMUnmap(void *start, size_t length) {
  int result;
  if (!MallocHook::InvokeMunmapReplacement(start, length, &result)) {
    result = absl::base_internal::DirectMunmap(start, length);
  }
  return result;
}

}  // namespace base_internal
}  // namespace absl
