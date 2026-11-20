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

#if defined(ABSL_INTERNAL_CPU_DETECT_ARM)

#include <cstdint>  // IWYU pragma: keep

#if defined(__ANDROID__)
// Bionic declares getauxval() starting at API level 18.
#if defined(__ANDROID_API__) && __ANDROID_API__ >= 18
#define ABSL_INTERNAL_CPU_DETECT_ARM_HAVE_GETAUXVAL 1
#endif
#elif defined(__linux__) && !defined(__UCLIBC__)
#if defined(__GLIBC__)
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 16)
#define ABSL_INTERNAL_CPU_DETECT_ARM_HAVE_GETAUXVAL 1
#endif
#elif defined(__has_include)
#if __has_include(<sys/auxv.h>)
// Other Linux C libraries such as musl (which provides getauxval() since 1.1.0
// and defines no macro to identify itself).
#define ABSL_INTERNAL_CPU_DETECT_ARM_HAVE_GETAUXVAL 1
#endif
#endif
#endif

#if defined(ABSL_INTERNAL_CPU_DETECT_ARM_HAVE_GETAUXVAL)
#include <sys/auxv.h>
#elif defined(__linux__) || defined(__ANDROID__)
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#elif defined(__APPLE__)
#ifdef __has_include
#if __has_include(<arm/cpu_capabilities_public.h>)
#include <arm/cpu_capabilities_public.h>
#endif
#endif
#include <sys/sysctl.h>
#include <sys/types.h>

#include <cstddef>
#include <optional>
#elif defined(__Fuchsia__) && (defined(__aarch64__) || defined(__arm64__))
#include <zircon/features.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE
#define PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE 30
#endif
#ifndef PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE
#define PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE 31
#endif
#ifndef PF_ARM_SHA3_INSTRUCTIONS_AVAILABLE
#define PF_ARM_SHA3_INSTRUCTIONS_AVAILABLE 64
#endif
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {
namespace {

#if defined(__linux__) || defined(__ANDROID__)
// Queries Linux/Android ELF auxiliary vector entries AT_HWCAP (16) and
// AT_HWCAP2 (26), using in-memory getauxval() when available (glibc >= 2.16,
// musl, Android API >= 18) and falling back to /proc/self/auxv on older NDK/
// libc configurations (works in static binaries without dlopen/libdl).
void QueryAuxv(unsigned long* hwcap,   // NOLINT(runtime/int)
               unsigned long* hwcap2)  // NOLINT(runtime/int)
    noexcept {
  constexpr unsigned long kAtNull = 0;     // NOLINT(runtime/int)
  constexpr unsigned long kAtHwcap = 16;   // NOLINT(runtime/int)
  constexpr unsigned long kAtHwcap2 = 26;  // NOLINT(runtime/int)
#if defined(ABSL_INTERNAL_CPU_DETECT_ARM_HAVE_GETAUXVAL)
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

#if (defined(__aarch64__) || defined(__arm64__)) && \
    (defined(__GNUC__) || defined(__clang__))
// MIDR_EL1 is trapped at EL0 and emulated by Linux when AT_HWCAP advertises
// HWCAP_CPUID (1 << 11). Note: on heterogeneous (big.LITTLE) SoCs, MIDR_EL1
// reflects the core executing the calling thread; all enumerated CpuType
// entries below are homogeneous datacenter processors.
CpuType DetectArm64CpuType(
    unsigned long hwcap) noexcept {                 // NOLINT(runtime/int)
  constexpr unsigned long kHwcapCpuid = 1UL << 11;  // NOLINT(runtime/int)
  if ((hwcap & kHwcapCpuid) == 0) return CpuType::kUnknown;
  uint64_t midr = 0;
  __asm__("mrs %0, MIDR_EL1" : "=r"(midr));
  const uint32_t implementer = static_cast<uint32_t>((midr >> 24) & 0xffU);
  const uint32_t part_number = static_cast<uint32_t>((midr >> 4) & 0xfffU);
  switch (implementer) {
    case 0x41:  // ARM
      switch (part_number) {
        case 0xd0c:
          return CpuType::kArmNeoverseN1;
        case 0xd40:
          return CpuType::kArmNeoverseV1;
        case 0xd49:
          return CpuType::kArmNeoverseN2;
        case 0xd4f: {
          uint64_t isar0 = 0;
          __asm__("mrs %0, ID_AA64ISAR0_EL1" : "=r"(isar0));
          // NVIDIA Grace uses Neoverse V2 cores configured without FEAT_RNG
          // (ID_AA64ISAR0_EL1.RNDR == 0 in bits [63:60]).
          return ((isar0 >> 60) & 0xfU) == 0 ? CpuType::kNvidiaGrace
                                             : CpuType::kArmNeoverseV2;
        }
        case 0xd84:
          return CpuType::kArmNeoverseV3;
        case 0xd8e:
          return CpuType::kArmNeoverseN3;
        case 0xd94:
          return CpuType::kArmNeoverseN4;
        default:
          return CpuType::kUnknown;
      }
    case 0x4e:  // NVIDIA
      return part_number == 0x010 ? CpuType::kNvidiaVera : CpuType::kUnknown;
    case 0xc0:  // Ampere
      return part_number == 0xac3 ? CpuType::kAmpereSiryn : CpuType::kUnknown;
    default:
      return CpuType::kUnknown;
  }
}
#endif
#endif  // defined(__linux__) || defined(__ANDROID__)

#if defined(__APPLE__)
std::optional<int> ReadSysctlInt(const char* name) noexcept {
  int value = 0;
  size_t size = sizeof(value);
  if (sysctlbyname(name, &value, &size, nullptr, 0) != 0 ||
      size != sizeof(value)) {
    return std::nullopt;
  }
  return value;
}

#if (defined(__aarch64__) || defined(__arm64__)) && defined(CAP_BIT_AdvSIMD)
bool HasCapBit(const uint8_t* caps, size_t caps_size, size_t bit) noexcept {
  const size_t byte_index = bit / 8;
  return byte_index < caps_size &&
         (caps[byte_index] & static_cast<uint8_t>(1U << (bit % 8))) != 0;
}

// XNU's hw.optional.arm.caps is a multi-byte bitvector (CAP_BIT_NB > 64 bits,
// so reading into uint64_t fails with ENOMEM). Read into a byte buffer to
// query all capabilities in one sysctlbyname call on macOS 13+ / iOS 16+.
bool PopulateFromAppleArmCaps(CpuFeatures* f) noexcept {
  uint8_t caps[128] = {};
  size_t caps_size = sizeof(caps);
  if (sysctlbyname("hw.optional.arm.caps", caps, &caps_size, nullptr, 0) != 0 ||
      static_cast<size_t>(CAP_BIT_AdvSIMD) / 8 >= caps_size) {
    return false;
  }
#ifdef CAP_BIT_CRC32
  if (HasCapBit(caps, caps_size, CAP_BIT_CRC32)) f->has_crc32 = true;
#endif
  if (HasCapBit(caps, caps_size, CAP_BIT_AdvSIMD)) {
    f->has_neon = true;
#ifdef CAP_BIT_FEAT_AES
    if (HasCapBit(caps, caps_size, CAP_BIT_FEAT_AES)) f->has_aes = true;
#endif
#ifdef CAP_BIT_FEAT_PMULL
    if (HasCapBit(caps, caps_size, CAP_BIT_FEAT_PMULL)) f->has_pmull = true;
#endif
#ifdef CAP_BIT_FEAT_SHA3
    if (HasCapBit(caps, caps_size, CAP_BIT_FEAT_SHA3)) f->has_sha3 = true;
#endif
  }
  return true;
}
#endif
#endif  // defined(__APPLE__)

CpuFeatures DetectCpuFeatures() noexcept {
  CpuFeatures f;
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#if defined(__ARM_NEON) || defined(_M_ARM64)
  f.has_neon = true;
#endif
#if defined(__ARM_FEATURE_CRC32)
  f.has_crc32 = true;
#endif
#if defined(__ARM_FEATURE_AES) || defined(__ARM_FEATURE_CRYPTO)
  f.has_aes = f.has_neon;
#endif
#if defined(__ARM_FEATURE_CRYPTO)
  f.has_pmull = f.has_neon;
#endif
#if defined(__ARM_FEATURE_SHA3)
  f.has_sha3 = f.has_neon;
#endif
#if defined(__ARM_FEATURE_SVE2)
  f.has_sve2 = f.has_neon;
#endif
#if defined(__linux__) || defined(__ANDROID__)
  // Linux/Android AArch64 uapi/asm/hwcap.h: AT_HWCAP (ASIMD = 1<<1,
  // AES = 1<<3, PMULL = 1<<4, CRC32 = 1<<7, SHA3 = 1<<17),
  // AT_HWCAP2 (SVE2 = 1<<1).
  constexpr unsigned long kHwcapAsimd = 1UL << 1;  // NOLINT(runtime/int)
  constexpr unsigned long kHwcapAes = 1UL << 3;    // NOLINT(runtime/int)
  constexpr unsigned long kHwcapPmull = 1UL << 4;  // NOLINT(runtime/int)
  constexpr unsigned long kHwcapCrc32 = 1UL << 7;  // NOLINT(runtime/int)
  constexpr unsigned long kHwcapSha3 = 1UL << 17;  // NOLINT(runtime/int)
  constexpr unsigned long kHwcap2Sve2 = 1UL << 1;  // NOLINT(runtime/int)
  unsigned long hwcap = 0;                         // NOLINT(runtime/int)
  unsigned long hwcap2 = 0;                        // NOLINT(runtime/int)
  QueryAuxv(&hwcap, &hwcap2);
#if defined(__GNUC__) || defined(__clang__)
  f.type = DetectArm64CpuType(hwcap);
#endif
  if ((hwcap & kHwcapCrc32) != 0) f.has_crc32 = true;
  if ((hwcap & kHwcapAsimd) != 0) f.has_neon = true;
  if (f.has_neon) {
    if ((hwcap & kHwcapAes) != 0) f.has_aes = true;
    if ((hwcap & kHwcapPmull) != 0) f.has_pmull = true;
    if ((hwcap & kHwcapSha3) != 0) f.has_sha3 = true;
    if ((hwcap2 & kHwcap2Sve2) != 0) f.has_sve2 = true;
  }
#elif defined(__APPLE__)
  bool caps_ok = false;
#if defined(CAP_BIT_AdvSIMD)
  caps_ok = PopulateFromAppleArmCaps(&f);
#endif
  if (!caps_ok) {
    if (ReadSysctlInt("hw.optional.armv8_crc32").value_or(0) != 0 ||
        ReadSysctlInt("hw.optional.arm.FEAT_CRC32").value_or(0) != 0) {
      f.has_crc32 = true;
    }
    // Older XNU kernels (such as macOS 11 on Apple M1) predate
    // hw.optional.arm.caps, hw.optional.arm.FEAT_AES, and
    // hw.optional.arm.FEAT_PMULL even though all Darwin arm64 hardware mandates
    // FEAT_AES and FEAT_PMULL; treat missing keys as supported once AdvSIMD is
    // present.
    if (ReadSysctlInt("hw.optional.AdvSIMD").value_or(0) != 0) {
      f.has_neon = true;
      if (ReadSysctlInt("hw.optional.arm.FEAT_AES").value_or(1) != 0) {
        f.has_aes = true;
      }
      if (ReadSysctlInt("hw.optional.arm.FEAT_PMULL").value_or(1) != 0) {
        f.has_pmull = true;
      }
      if (ReadSysctlInt("hw.optional.armv8_2_sha3").value_or(0) != 0 ||
          ReadSysctlInt("hw.optional.arm.FEAT_SHA3").value_or(0) != 0) {
        f.has_sha3 = true;
      }
    }
  }
  // M1-M3 lack SVE/SVE2, and M4 implements SME2 (streaming mode only, not
  // non-streaming FEAT_SVE2); query FEAT_SVE2 for future Apple chips.
  if (f.has_neon &&
      ReadSysctlInt("hw.optional.arm.FEAT_SVE2").value_or(0) != 0) {
    f.has_sve2 = true;
  }
#elif defined(__Fuchsia__)
  uint32_t hwcap = 0;
  if (zx_system_get_features(ZX_FEATURE_KIND_CPU, &hwcap) == ZX_OK) {
    if ((hwcap & ZX_ARM64_FEATURE_ISA_CRC32) != 0) f.has_crc32 = true;
    if ((hwcap & ZX_ARM64_FEATURE_ISA_ASIMD) != 0) {
      f.has_neon = true;
      if ((hwcap & ZX_ARM64_FEATURE_ISA_AES) != 0) f.has_aes = true;
      if ((hwcap & ZX_ARM64_FEATURE_ISA_PMULL) != 0) f.has_pmull = true;
#ifdef ZX_ARM64_FEATURE_ISA_SHA3
      if ((hwcap & ZX_ARM64_FEATURE_ISA_SHA3) != 0) f.has_sha3 = true;
#endif
#if defined(ZX_FEATURE_KIND_CPU_SVE) && defined(ZX_ARM64_FEATURE_ISA_SVE) && \
    defined(ZX_ARM64_FEATURE_ISA_SVE2)
      uint32_t sve_hwcap = 0;
      if ((hwcap & ZX_ARM64_FEATURE_ISA_SVE) != 0 &&
          zx_system_get_features(ZX_FEATURE_KIND_CPU_SVE, &sve_hwcap) ==
              ZX_OK &&
          (sve_hwcap & ZX_ARM64_FEATURE_ISA_SVE2) != 0) {
        f.has_sve2 = true;
      }
#endif
    }
  }
#elif defined(_WIN32)
  if (IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE) != 0) {
    f.has_crc32 = true;
  }
  if (IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE) != 0) {
    f.has_aes = true;
    f.has_pmull = true;
  }
  if (IsProcessorFeaturePresent(PF_ARM_SHA3_INSTRUCTIONS_AVAILABLE) != 0) {
    f.has_sha3 = true;
  }
#endif
#else  // 32-bit ARM
#if defined(__ARM_NEON)
  f.has_neon = true;
#endif
#if defined(__ARM_FEATURE_CRC32)
  f.has_crc32 = true;
#endif
#if defined(__ARM_NEON) && \
    (defined(__ARM_FEATURE_AES) || defined(__ARM_FEATURE_CRYPTO))
  f.has_aes = true;
#endif
#if defined(__ARM_NEON) && defined(__ARM_FEATURE_CRYPTO)
  f.has_pmull = true;
#endif
#if defined(__linux__) || defined(__ANDROID__)
  // Linux/Android AArch32 uapi/asm/hwcap.h: AT_HWCAP (NEON = 1<<12),
  // AT_HWCAP2 (AES = 1<<0, PMULL = 1<<1, CRC32 = 1<<4).
  constexpr unsigned long kHwcapNeon = 1UL << 12;   // NOLINT(runtime/int)
  constexpr unsigned long kHwcap2Aes = 1UL << 0;    // NOLINT(runtime/int)
  constexpr unsigned long kHwcap2Pmull = 1UL << 1;  // NOLINT(runtime/int)
  constexpr unsigned long kHwcap2Crc32 = 1UL << 4;  // NOLINT(runtime/int)
  unsigned long hwcap = 0;                          // NOLINT(runtime/int)
  unsigned long hwcap2 = 0;                         // NOLINT(runtime/int)
  QueryAuxv(&hwcap, &hwcap2);
  if ((hwcap2 & kHwcap2Crc32) != 0) f.has_crc32 = true;
  if ((hwcap & kHwcapNeon) != 0) f.has_neon = true;
  if (f.has_neon) {
    if ((hwcap2 & kHwcap2Aes) != 0) f.has_aes = true;
    if ((hwcap2 & kHwcap2Pmull) != 0) f.has_pmull = true;
  }
#elif defined(__APPLE__)
  if (ReadSysctlInt("hw.optional.armv8_crc32").value_or(0) != 0) {
    f.has_crc32 = true;
  }
  if (ReadSysctlInt("hw.optional.neon").value_or(0) != 0) f.has_neon = true;
  if (f.has_neon) {
    if (ReadSysctlInt("hw.optional.arm.FEAT_AES").value_or(0) != 0) {
      f.has_aes = true;
    }
    if (ReadSysctlInt("hw.optional.arm.FEAT_PMULL").value_or(0) != 0) {
      f.has_pmull = true;
    }
  }
#endif
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

#endif  // defined(ABSL_INTERNAL_CPU_DETECT_ARM)
