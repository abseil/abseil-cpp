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

#if defined(ABSL_INTERNAL_CPU_DETECT_X86)

#include <cstdint>
#include <cstring>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>

#include <cstddef>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {
namespace {

void Cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax, uint32_t* ebx,
           uint32_t* ecx, uint32_t* edx) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
  int info[4];
  __cpuidex(info, static_cast<int>(leaf), static_cast<int>(subleaf));
  *eax = static_cast<uint32_t>(info[0]);
  *ebx = static_cast<uint32_t>(info[1]);
  *ecx = static_cast<uint32_t>(info[2]);
  *edx = static_cast<uint32_t>(info[3]);
#else
  __cpuid_count(leaf, subleaf, *eax, *ebx, *ecx, *edx);
#endif
}

#if defined(__APPLE__)
bool SysctlBool(const char* name) noexcept {
  int value = 0;
  size_t size = sizeof(value);
  return sysctlbyname(name, &value, &size, nullptr, 0) == 0 &&
         size == sizeof(value) && value != 0;
}
#endif

uint64_t ReadXcr0() noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
  return _xgetbv(0);
#else
  uint32_t eax = 0;
  uint32_t edx = 0;
  // Raw encoding of `xgetbv` with ECX = 0; avoids requiring `-mxsave`.
  __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(0));
  return (static_cast<uint64_t>(edx) << 32) | eax;
#endif
}

CpuType DetectIntelCpuType(uint32_t eax, uint32_t ebx) noexcept {
  if ((ebx & 0xffU) != 0) return CpuType::kUnknown;  // Non-zero brand ID.
  const uint32_t stepping = eax & 0x0fU;
  uint32_t model = (eax >> 4) & 0x0fU;
  uint32_t family = (eax >> 8) & 0x0fU;
  const uint32_t ext_model = (eax >> 16) & 0x0fU;
  const uint32_t ext_family = (eax >> 20) & 0xffU;
  if (family == 0x0fU) family += ext_family;
  if (family == 0x0fU || family == 0x06U) model += (ext_model << 4);
  if (family != 0x06U) return CpuType::kUnknown;

  switch (model) {
    case 0x2c:
      return CpuType::kIntelWestmere;
    case 0x2d:
      return CpuType::kIntelSandybridge;
    case 0x3e:
      return CpuType::kIntelIvybridge;
    case 0x3c:
    case 0x3f:
      return CpuType::kIntelHaswell;
    case 0x4f:
    case 0x56:
      return CpuType::kIntelBroadwell;
    case 0x55:
      return stepping < 5 ? CpuType::kIntelSkylakeXeon
                          : CpuType::kIntelCascadelakeXeon;
    case 0x5e:
      return CpuType::kIntelSkylake;
    case 0x6a:
      return CpuType::kIntelIcelake;
    case 0x8f:
      return CpuType::kIntelSapphirerapids;
    case 0xad:
      return CpuType::kIntelGraniterapids;
    case 0xcf:
      return CpuType::kIntelEmeraldrapids;
    default:
      return CpuType::kUnknown;
  }
}

CpuType DetectAmdCpuType(uint32_t eax) noexcept {
  uint32_t model = (eax >> 4) & 0x0fU;
  uint32_t family = (eax >> 8) & 0x0fU;
  const uint32_t ext_model = (eax >> 16) & 0x0fU;
  const uint32_t ext_family = (eax >> 20) & 0xffU;
  if (family == 0x0fU) {
    family += ext_family;
    model += (ext_model << 4);
  }
  switch (family) {
    case 0x17:
      switch (model) {
        case 0x00:
        case 0x01:
          return CpuType::kAmdNaples;
        case 0x30:
        case 0x31:
          return CpuType::kAmdRome;
        default:
          return CpuType::kUnknown;
      }
    case 0x19:
      switch (model) {
        case 0x00:
        case 0x01:
          return CpuType::kAmdMilan;
        case 0x10:
        case 0x11:
          return CpuType::kAmdGenoa;
        case 0x44:
          return CpuType::kAmdRyzenV3000;
        case 0xa0:
          return CpuType::kAmdSiena;
        default:
          return CpuType::kUnknown;
      }
    case 0x1a:
      if (model >= 0x50 && model <= 0x5f) return CpuType::kAmdVenice;
      return model == 0x02 ? CpuType::kAmdTurin : CpuType::kUnknown;
    default:
      return CpuType::kUnknown;
  }
}

// Queries hardware threads per core via CPUID leaf 0xb. AMD CPUs prior to Zen 2
// (Rome) lack leaf 0xb and intentionally fall back to 1 rather than leaf 1
// EBX[23:16] (which reports total logical processors per package).
int DetectNumContextsPerCpu(uint32_t max_leaf, uint32_t leaf1_edx) noexcept {
  if (max_leaf < 0xbU || (leaf1_edx & (1U << 28)) == 0) return 1;
  for (uint32_t subleaf = 0; subleaf < 4; ++subleaf) {
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    Cpuid(0xbU, subleaf, &eax, &ebx, &ecx, &edx);
    const uint32_t level_type = (ecx >> 8) & 0xffU;
    if (level_type == 0) break;
    if (level_type == 1) {
      const int num_threads = static_cast<int>(ebx & 0xffffU);
      if (num_threads >= 1) return num_threads;
    }
  }
  return 1;
}

CpuFeatures DetectCpuFeatures() noexcept {
  CpuFeatures f;
  uint32_t eax = 0;
  uint32_t ebx = 0;
  uint32_t ecx = 0;
  uint32_t edx = 0;
#if defined(_MSC_VER) && !defined(__clang__)
  int info[4];
  __cpuid(info, 0);
  const uint32_t max_leaf = static_cast<uint32_t>(info[0]);
  if (max_leaf < 1) return f;
  ebx = static_cast<uint32_t>(info[1]);
  ecx = static_cast<uint32_t>(info[2]);
  edx = static_cast<uint32_t>(info[3]);
#else
  const uint32_t max_leaf = __get_cpuid_max(0, nullptr);
  if (max_leaf < 1) return f;
  Cpuid(0, 0, &eax, &ebx, &ecx, &edx);
#endif
  char vendor[12];
  std::memcpy(vendor, &ebx, 4);
  std::memcpy(vendor + 4, &edx, 4);
  std::memcpy(vendor + 8, &ecx, 4);
  const bool is_intel = std::memcmp(vendor, "GenuineIntel", 12) == 0;
  const bool is_amd = std::memcmp(vendor, "AuthenticAMD", 12) == 0;

  Cpuid(1, 0, &eax, &ebx, &ecx, &edx);
  if (is_intel) {
    f.type = DetectIntelCpuType(eax, ebx);
  } else if (is_amd) {
    f.type = DetectAmdCpuType(eax);
  }
  f.num_contexts_per_cpu = DetectNumContextsPerCpu(max_leaf, edx);

  f.has_ssse3 = (ecx & (1U << 9)) != 0;
  f.has_sse4_1 = (ecx & (1U << 19)) != 0;
  f.has_aes = (ecx & (1U << 25)) != 0;

  const bool osxsave = (ecx & (1U << 27)) != 0;
  const bool avx_bit = (ecx & (1U << 28)) != 0;
  bool ymm_ok = false;
  bool zmm_ok = false;
  if (osxsave && avx_bit) {
    const uint64_t xcr0 = ReadXcr0();
    // XCR0[2:1] = YMM + XMM state enabled by OS.
    ymm_ok = (xcr0 & 0x6U) == 0x6U;
    if (ymm_ok) {
      // XCR0[7:5] = Hi16_ZMM + ZMM_Hi256 + opmask state, plus XCR0[2:1].
      zmm_ok = (xcr0 & 0xe6U) == 0xe6U;
#if defined(__APPLE__)
      // Darwin enables ZMM state lazily on first AVX-512 instruction, leaving
      // XCR0[7:5] clear prior to use; consult the kernel capability flag.
      if (!zmm_ok) zmm_ok = SysctlBool("hw.optional.avx512f");
#endif
    }
  }

  if (max_leaf < 7) return f;
  Cpuid(7, 0, &eax, &ebx, &ecx, &edx);
  // BMI2 operates on GPRs and does not require OSXSAVE or YMM state.
  f.has_bmi2 = (ebx & (1U << 8)) != 0;
  if (ymm_ok) {
    f.has_avx2 = (ebx & (1U << 5)) != 0;
    f.has_vaes = (ecx & (1U << 9)) != 0;
    if (zmm_ok && (ebx & (1U << 16)) != 0) {
      f.has_avx512f = true;
      f.has_avx512bw = (ebx & (1U << 30)) != 0;
      f.has_avx512vl = (ebx & (1U << 31)) != 0;
    }
  }
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

#endif  // defined(ABSL_INTERNAL_CPU_DETECT_X86)
