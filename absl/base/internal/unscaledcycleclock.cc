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

#include "absl/base/internal/unscaledcycleclock.h"

#if ABSL_USE_UNSCALED_CYCLECLOCK

#if defined(_WIN32)
#include <intrin.h>
#endif

#if (defined(__powerpc__) || defined(__ppc__)) && defined(__GLIBC__)
#include <sys/platform/ppc.h>
#endif

#if (defined(__powerpc__) || defined(__ppc__)) && defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include "absl/base/internal/sysinfo.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

#if defined(__i386__)

int64_t UnscaledCycleClock::Now() {
  int64_t ret;
  __asm__ volatile("rdtsc" : "=A"(ret));
  return ret;
}

double UnscaledCycleClock::Frequency() {
  return base_internal::NominalCPUFrequency();
}

#elif defined(__x86_64__)

int64_t UnscaledCycleClock::Now() {
  uint64_t low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return (high << 32) | low;
}

double UnscaledCycleClock::Frequency() {
  return base_internal::NominalCPUFrequency();
}

#elif defined(__powerpc__) || defined(__ppc__)

int64_t UnscaledCycleClock::Now() {
#ifdef __GLIBC__
  return __ppc_get_timebase();
#elif defined(__FREEBSD__)
  union { long long complete; unsigned int part[2]; } ticks;
  unsigned int tmp;
  asm volatile(
    "0:\n"
    "mftbu %[hi32]\n"
    "mftb %[lo32]\n"
    "mftbu %[tmp]\n"
    "cmpw %[tmp],%[hi32]\n"
    "bne 0b\n"
    : [hi32] "=r"(ticks.part[0]), [lo32] "=r"(ticks.part[1]),
    [tmp] "=r"(tmp)
  );
  return ticks.complete;
#endif
}

double UnscaledCycleClock::Frequency() {
#ifdef __GLIBC__
  return __ppc_get_timebase_freq();
#elif defined(__FreeBSD__)
  double timebaseFrequency = 0;
  size_t length = sizeof(timebaseFrequency);
  sysctlbyname("kern.timecounter.tc.timebase.frequency", &timebaseFrequency, &length, NULL, 0);
  return timebaseFrequency;
#endif
}

#elif defined(__aarch64__)

// System timer of ARMv8 runs at a different frequency than the CPU's.
// The frequency is fixed, typically in the range 1-50MHz.  It can be
// read at CNTFRQ special register.  We assume the OS has set up
// the virtual timer properly.
int64_t UnscaledCycleClock::Now() {
  int64_t virtual_timer_value;
  asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
  return virtual_timer_value;
}

double UnscaledCycleClock::Frequency() {
  uint64_t aarch64_timer_frequency;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(aarch64_timer_frequency));
  return aarch64_timer_frequency;
}

#elif defined(_M_IX86) || defined(_M_X64)

#pragma intrinsic(__rdtsc)

int64_t UnscaledCycleClock::Now() {
  return __rdtsc();
}

double UnscaledCycleClock::Frequency() {
  return base_internal::NominalCPUFrequency();
}

#endif

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_USE_UNSCALED_CYCLECLOCK
