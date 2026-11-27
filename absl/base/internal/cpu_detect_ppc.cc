// Copyright 2026 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/config.h"
#include "absl/base/internal/cpu_detect.h"

#if defined(ABSL_INTERNAL_CPU_DETECT_PPC)

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__UCLIBC__)
#if defined(__GLIBC__)
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 16)
#define ABSL_INTERNAL_CPU_DETECT_PPC_HAVE_GETAUXVAL 1
#endif
#elif defined(__has_include)
#if __has_include(<sys/auxv.h>)
// Other Linux C libraries such as musl (which provides getauxval() since 1.1.0
// and defines no macro to identify itself).
#define ABSL_INTERNAL_CPU_DETECT_PPC_HAVE_GETAUXVAL 1
#endif
#endif
#endif

#if defined(ABSL_INTERNAL_CPU_DETECT_PPC_HAVE_GETAUXVAL)
#include <sys/auxv.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {
namespace {

#if defined(__linux__)
void QueryAuxv(unsigned long* hwcap,   // NOLINT(runtime/int)
               unsigned long* hwcap2)  // NOLINT(runtime/int)
    noexcept {
  constexpr unsigned long kAtNull = 0;     // NOLINT(runtime/int)
  constexpr unsigned long kAtHwcap = 16;   // NOLINT(runtime/int)
  constexpr unsigned long kAtHwcap2 = 26;  // NOLINT(runtime/int)
#if defined(ABSL_INTERNAL_CPU_DETECT_PPC_HAVE_GETAUXVAL)
#ifdef AT_HWCAP
  static_assert(AT_HWCAP == kAtHwcap, "AT_HWCAP value mismatch");
#endif
#ifdef AT_HWCAP2
  static_assert(AT_HWCAP2 == kAtHwcap2, "AT_HWCAP2 value mismatch");
#endif
  (void)kAtNull;
  *hwcap = getauxval(kAtHwcap);
  *hwcap2 = getauxval(kAtHwcap2);
#else
  *hwcap = 0;
  *hwcap2 = 0;
#ifdef O_CLOEXEC
  constexpr int kOpenFlags = O_RDONLY | O_CLOEXEC;
#else
  constexpr int kOpenFlags = O_RDONLY;
#endif
  int fd;
  do {
    fd = open("/proc/self/auxv", kOpenFlags);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) return;
  struct AuxvEntry {
    unsigned long tag;    // NOLINT(runtime/int)
    unsigned long value;  // NOLINT(runtime/int)
  };
  AuxvEntry entries[32];
  bool done = false;
  while (!done) {
    ssize_t n;
    do {
      n = read(fd, entries, sizeof(entries));
    } while (n < 0 && errno == EINTR);
    if (n < static_cast<ssize_t>(sizeof(AuxvEntry))) break;
    const size_t count = static_cast<size_t>(n) / sizeof(AuxvEntry);
    for (size_t i = 0; i < count; ++i) {
      if (entries[i].tag == kAtNull) {
        done = true;
        break;
      }
      if (entries[i].tag == kAtHwcap) *hwcap = entries[i].value;
      if (entries[i].tag == kAtHwcap2) *hwcap2 = entries[i].value;
    }
  }
  close(fd);
#endif
}
#endif

CpuFeatures DetectCpuFeatures() noexcept {
  CpuFeatures f;
#if defined(__VSX__)
  f.has_vsx = true;
#endif
#if defined(__CRYPTO__) && defined(__VSX__) && defined(__POWER8_VECTOR__)
  f.has_vcrypto = true;
#endif
#if defined(__linux__)
  // Linux uapi/asm/cputable.h: AT_HWCAP = 16 (HAS_VSX = 0x80), AT_HWCAP2 = 26
  // (ARCH_2_07 = 0x80000000, VEC_CRYPTO = 0x02000000).
  constexpr unsigned long kPpcHasVsx = 0x00000080UL;     // NOLINT(runtime/int)
  constexpr unsigned long kPpcArch207 = 0x80000000UL;    // NOLINT(runtime/int)
  constexpr unsigned long kPpcVecCrypto = 0x02000000UL;  // NOLINT(runtime/int)
  unsigned long hwcap = 0;                               // NOLINT(runtime/int)
  unsigned long hwcap2 = 0;                              // NOLINT(runtime/int)
  QueryAuxv(&hwcap, &hwcap2);
  if ((hwcap & kPpcHasVsx) != 0) f.has_vsx = true;
  if (f.has_vsx && (hwcap2 & kPpcArch207) != 0 &&
      (hwcap2 & kPpcVecCrypto) != 0) {
    f.has_vcrypto = true;
  }
#endif
  return f;
}

}  // namespace

CpuFeatures GetCpuFeatures() noexcept {
  static const CpuFeatures kFeatures = DetectCpuFeatures();
  return kFeatures;
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // defined(ABSL_INTERNAL_CPU_DETECT_PPC)
