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

#ifdef __mips__
// Include definitions of the ABI currently in use.
#ifdef __BIONIC__
// Android doesn't have sgidefs.h, but does have asm/sgidefs.h, which has the
// definitions we need.
#include <asm/sgidefs.h>
#else
#include <sgidefs.h>
#endif  // __BIONIC__
#endif  // __mips__

// SYS_mmap, SYS_munmap, and SYS_mremap are not defined in Android.
#ifdef __BIONIC__
extern "C" void *__mmap2(void *, size_t, int, int, int, long);
#if defined(__NR_mmap) && !defined(SYS_mmap)
#define SYS_mmap __NR_mmap
#endif
#ifndef SYS_munmap
#define SYS_munmap __NR_munmap
#endif
#ifndef SYS_mremap
#define SYS_mremap __NR_mremap
#endif
#endif  // __BIONIC__

// Platform specific logic extracted from
// https://chromium.googlesource.com/linux-syscall-support/+/master/linux_syscall_support.h
static inline void* do_mmap64(void* start, size_t length, int prot,
                              int flags, int fd, off64_t offset) __THROW {
#if defined(__i386__) ||                                                \
  defined(__ARM_ARCH_3__) || defined(__ARM_EABI__) ||                   \
  (defined(__mips__) && _MIPS_SIM == _MIPS_SIM_ABI32) ||                \
  (defined(__PPC__) && !defined(__PPC64__)) ||                          \
  (defined(__s390__) && !defined(__s390x__))
  // On these architectures, implement mmap with mmap2.
  static int pagesize = 0;
  if (pagesize == 0) {
    pagesize = getpagesize();
  }
  if (offset < 0 || offset % pagesize != 0) {
    errno = EINVAL;
    return MAP_FAILED;
  }
#ifdef __BIONIC__
  // SYS_mmap2 has problems on Android API level <= 16.
  // Workaround by invoking __mmap2() instead.
  return __mmap2(start, length, prot, flags, fd, offset / pagesize);
#else
  return reinterpret_cast<void*>(
      syscall(SYS_mmap2, start, length, prot, flags, fd,
              static_cast<off_t>(offset / pagesize)));
#endif
#elif defined(__s390x__)
  // On s390x, mmap() arguments are passed in memory.
  uint32_t buf[6] = {
      reinterpret_cast<uint32_t>(start), static_cast<uint32_t>(length),
      static_cast<uint32_t>(prot),       static_cast<uint32_t>(flags),
      static_cast<uint32_t>(fd),         static_cast<uint32_t>(offset)};
  return reintrepret_cast<void*>(syscall(SYS_mmap, buf));
#elif defined(__x86_64__)
  // The x32 ABI has 32 bit longs, but the syscall interface is 64 bit.
  // We need to explicitly cast to an unsigned 64 bit type to avoid implicit
  // sign extension.  We can't cast pointers directly because those are
  // 32 bits, and gcc will dump ugly warnings about casting from a pointer
  // to an integer of a different size. We also need to make sure __off64_t
  // isn't truncated to 32-bits under x32.
  #define MMAP_SYSCALL_ARG(x) ((uint64_t)(uintptr_t)(x))
  return reinterpret_cast<void*>(
      syscall(SYS_mmap, MMAP_SYSCALL_ARG(start), MMAP_SYSCALL_ARG(length),
              MMAP_SYSCALL_ARG(prot), MMAP_SYSCALL_ARG(flags),
              MMAP_SYSCALL_ARG(fd), static_cast<uint64_t>(offset)));
  #undef MMAP_SYSCALL_ARG
#else  // Remaining 64-bit aritectures.
  static_assert(sizeof(unsigned long) == 8, "Platform is not 64-bit");
  return reinterpret_cast<void*>(
      syscall(SYS_mmap, start, length, prot, flags, fd, offset));
#endif
}

// We use do_mmap64 abstraction to put MallocHook::InvokeMmapHook
// calls right into mmap and mmap64, so that the stack frames in the caller's
// stack are at the same offsets for all the calls of memory allocating
// functions.

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
    result = do_mmap64(start, length, prot, flags, fd, offset);
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
    result = do_mmap64(start, length, prot, flags, fd,
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
    result = syscall(SYS_munmap, start, length);
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
    result = do_mmap64(start, length, prot, flags, fd, offset);
  }
  return result;
}

/*static*/int MallocHook::UnhookedMUnmap(void *start, size_t length) {
  int result;
  if (!MallocHook::InvokeMunmapReplacement(start, length, &result)) {
    result = syscall(SYS_munmap, start, length);
  }
  return result;
}

}  // namespace base_internal
}  // namespace absl
