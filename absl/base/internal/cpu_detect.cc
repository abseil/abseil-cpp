// Copyright 2022 The Abseil Authors
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

#include "absl/base/internal/cpu_detect.h"

#include <cstdint>
#include <optional>  // IWYU pragma: keep
#include <string>

#include "absl/base/config.h"

#if defined(__aarch64__) && defined(__linux__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

#if defined(__aarch64__) && defined(__APPLE__)
#if __has_include(<arm/cpu_capabilities_public.h>)
#include <arm/cpu_capabilities_public.h>
#endif
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <intrin.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
#if ABSL_HAVE_BUILTIN(__cpuid)
// MSVC-equivalent __cpuid intrinsic declaration for clang-like compilers
// for non-Windows build environments.
extern void __cpuid(int[4], int);
extern void __cpuidex(int[4], int, int);
#elif !defined(_WIN32) && !defined(_WIN64)
// MSVC defines this function for us.
// https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex
static void __cpuid(int cpu_info[4], int info_type) {
  __asm__ volatile("cpuid \n\t"
                   : "=a"(cpu_info[0]), "=b"(cpu_info[1]), "=c"(cpu_info[2]),
                     "=d"(cpu_info[3])
                   : "a"(info_type), "c"(0));
}
static void __cpuidex(int cpu_info[4], int info_type, int ecx) {
  __asm__ volatile("cpuid \n\t"
                   : "=a"(cpu_info[0]), "=b"(cpu_info[1]), "=c"(cpu_info[2]),
                     "=d"(cpu_info[3])
                   : "a"(info_type), "c"(ecx));
}
#endif  // !defined(_WIN32) && !defined(_WIN64)
#endif  // defined(__x86_64__) || defined(_M_X64)

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

#if defined(__x86_64__) || defined(_M_X64)

namespace {

enum class Vendor {
  kUnknown,
  kIntel,
  kAmd,
};

Vendor GetVendor() {
  // Get the vendor string (issue CPUID with eax = 0).
  int cpu_info[4];
  __cpuid(cpu_info, 0);

  std::string vendor;
  vendor.append(reinterpret_cast<char*>(&cpu_info[1]), 4);
  vendor.append(reinterpret_cast<char*>(&cpu_info[3]), 4);
  vendor.append(reinterpret_cast<char*>(&cpu_info[2]), 4);
  if (vendor == "GenuineIntel") {
    return Vendor::kIntel;
  } else if (vendor == "AuthenticAMD") {
    return Vendor::kAmd;
  } else {
    return Vendor::kUnknown;
  }
}

CpuType GetIntelCpuType() {
  // To get general information and extended features we send eax = 1 and
  // ecx = 0 to cpuid.  The response is returned in eax, ebx, ecx and edx.
  // (See Intel 64 and IA-32 Architectures Software Developer's Manual
  // Volume 2A: Instruction Set Reference, A-M CPUID).
  // https://www.intel.com/content/www/us/en/architecture-and-technology/64-ia-32-architectures-software-developer-vol-2a-manual.html
  // https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex
  int cpu_info[4];
  __cpuid(cpu_info, 1);

  // Response in eax bits as follows:
  // 0-3 (stepping id)
  // 4-7 (model number),
  // 8-11 (family code),
  // 12-13 (processor type),
  // 16-19 (extended model)
  // 20-27 (extended family)

  int family = (cpu_info[0] >> 8) & 0x0f;
  int model_num = (cpu_info[0] >> 4) & 0x0f;
  int ext_family = (cpu_info[0] >> 20) & 0xff;
  int ext_model_num = (cpu_info[0] >> 16) & 0x0f;

  int brand_id = cpu_info[1] & 0xff;

  // Process the extended family and model info if necessary
  if (family == 0x0f) {
    family += ext_family;
  }

  if (family == 0x0f || family == 0x6) {
    model_num += (ext_model_num << 4);
  }

  switch (brand_id) {
    case 0:  // no brand ID, so parse CPU family/model
      switch (family) {
        case 6:  // Most PentiumIII processors are in this category
          switch (model_num) {
            case 0x25:  // Westmere: Arrandale / Clarkdale
            case 0x2c:  // Westmere: Gulftown
            case 0x2f:  // Westmere-EX
              return CpuType::kIntelWestmere;
            case 0x2a:  // Sandybridge (client)
            case 0x2d:  // Sandybridge
              return CpuType::kIntelSandybridge;
            case 0x3a:  // Ivybridge (client)
            case 0x3e:  // Ivybridge
              return CpuType::kIntelIvybridge;
            case 0x3c:  // Haswell (client)
            case 0x3f:  // Haswell
            case 0x45:  // Haswell-ULT
            case 0x46:  // Haswell (GT3e)
              return CpuType::kIntelHaswell;
            case 0x3d:  // Broadwell (client)
            case 0x47:  // Broadwell (H/C)
            case 0x4f:  // Broadwell
            case 0x56:  // BroadwellDE
              return CpuType::kIntelBroadwell;
            case 0x55:                         // Skylake Xeon
              if ((cpu_info[0] & 0x0f) < 5) {  // stepping < 5 is skylake
                return CpuType::kIntelSkylakeXeon;
              } else {  // stepping >= 5 is cascadelake
                return CpuType::kIntelCascadelakeXeon;
              }
            case 0x4e:  // Skylake (mobile)
            case 0x5e:  // Skylake (client)
            case 0x8e:  // Kaby Lake / Coffee Lake / Whiskey Lake / Amber Lake
            case 0x9e:  // Kaby Lake / Coffee Lake (desktop/H/Xeon E)
            case 0xa5:  // Comet Lake (desktop/H)
            case 0xa6:  // Comet Lake-U
              return CpuType::kIntelSkylake;
            case 0x6a:  // Ice Lake-SP
            case 0x6c:  // Ice Lake-DE
            case 0x7d:  // Ice Lake-Y
            case 0x7e:  // Ice Lake-U
            case 0x8c:  // Tiger Lake-U
            case 0x8d:  // Tiger Lake-H
            case 0xa7:  // Rocket Lake (Cypress Cove)
              return CpuType::kIntelIcelake;
            case 0x8f:  // Sapphire Rapids
              return CpuType::kIntelSapphirerapids;
            case 0xcf:  // Emerald Rapids
              return CpuType::kIntelEmeraldrapids;
            case 0xad:  // Granite Rapids-SP/AP
            case 0xae:  // Granite Rapids-D
              return CpuType::kIntelGraniterapids;
            default:
              return CpuType::kUnknown;
          }
        default:
          return CpuType::kUnknown;
      }
    default:
      return CpuType::kUnknown;
  }
}

CpuType GetAmdCpuType() {
  // To get general information and extended features we send eax = 1 and
  // ecx = 0 to cpuid.  The response is returned in eax, ebx, ecx and edx.
  // (See Intel 64 and IA-32 Architectures Software Developer's Manual
  // Volume 2A: Instruction Set Reference, A-M CPUID).
  // https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex
  int cpu_info[4];
  __cpuid(cpu_info, 1);

  // Response in eax bits as follows:
  // 0-3 (stepping id)
  // 4-7 (model number),
  // 8-11 (family code),
  // 12-13 (processor type),
  // 16-19 (extended model)
  // 20-27 (extended family)

  int family = (cpu_info[0] >> 8) & 0x0f;
  int ext_family = (cpu_info[0] >> 20) & 0xff;
  int ext_model_num = (cpu_info[0] >> 16) & 0x0f;

  if (family == 0x0f) {
    family += ext_family;
  }

  switch (family) {
    case 0x17:
      switch (ext_model_num) {
        case 0x0:  // Zen 1: Naples / Summit Ridge / Whitehaven / Snowy Owl
        case 0x1:  // Zen 1 / Zen+: Raven Ridge / Picasso / Banded Kestrel
        case 0x2:  // Zen+: Dali
          return CpuType::kAmdNaples;
        case 0x3:  // Zen 2: Rome / Castle Peak
        case 0x4:  // Zen 2: Van Gogh
        case 0x6:  // Zen 2: Renoir / Grey Hawk
        case 0x7:  // Zen 2: Matisse
        case 0x8:  // Zen 2: Lucienne
        case 0x9:  // Zen 2: Mero
        case 0xa:  // Zen 2: Mendocino
          return CpuType::kAmdRome;
        default:
          return CpuType::kUnknown;
      }
      break;
    case 0x19:
      switch (ext_model_num) {
        case 0x0:  // Zen 3: Milan / Chagall (Threadripper PRO 5000)
        case 0x2:  // Zen 3: Vermeer (Ryzen 5000 desktop)
        case 0x3:  // Zen 3: Badami
        case 0x5:  // Zen 3: Cezanne / Barcelo (Ryzen 5000 mobile)
          return CpuType::kAmdMilan;
        case 0x1:  // Zen 4: Genoa / Storm Peak (Threadripper PRO 7000)
        case 0x6:  // Zen 4: Raphael (Ryzen 7000 desktop) / Dragon Range
        case 0x7:  // Zen 4: Phoenix / Hawk Point (Ryzen 7040/8040 mobile)
          return CpuType::kAmdGenoa;
        case 0x4:  // Zen 3+: Rembrandt (Ryzen 6000 mobile / Ryzen V3000)
          return CpuType::kAmdRyzenV3000;
        case 0xa:  // Zen 4c: Bergamo / Siena
          return CpuType::kAmdSiena;
        default:
          return CpuType::kUnknown;
      }
      break;
    case 0x1A:
      switch (ext_model_num) {
        case 0x0:  // Zen 5: Turin
        case 0x1:  // Zen 5c: Turin Dense
        case 0x2:  // Zen 5: Strix Point / Krackan Point
        case 0x4:  // Zen 5: Granite Ridge (Ryzen 9000) / Shimada Peak
        case 0x6:  // Zen 5: Fire Range
        case 0x7:  // Zen 5: Strix Halo
          return CpuType::kAmdTurin;
        case 0x5:  // Zen 6: Venice
        case 0x8:  // Zen 6: Olympic Ridge
        case 0x9:  // Zen 6: Medusa Point
        case 0xa:  // Zen 6: Medusa Halo
        case 0xc:  // Zen 6: Venice Dense
          return CpuType::kAmdVenice;
        default:
          return CpuType::kUnknown;
      }
      break;
    default:
      return CpuType::kUnknown;
  }
}

}  // namespace

CpuType GetCpuType() {
  switch (GetVendor()) {
    case Vendor::kIntel:
      return GetIntelCpuType();
    case Vendor::kAmd:
      return GetAmdCpuType();
    default:
      return CpuType::kUnknown;
  }
}

bool SupportsArmCRC32PMULL() { return false; }

bool SupportsBmi2() {
  int cpu_info[4];
  __cpuid(cpu_info, 0);
  if (cpu_info[0] < 7) {
    return false;
  }
  __cpuidex(cpu_info, 7, 0);
  return (cpu_info[1] & (1 << 8)) != 0;
}

bool SupportsAvx2() {
  int cpu_info[4];
  __cpuid(cpu_info, 0);
  if (cpu_info[0] < 7) {
    return false;
  }
  __cpuid(cpu_info, 1);
  // Check PCLMULQDQ (bit 1), OSXSAVE (bit 27), and AVX (bit 28) in ECX.
  constexpr int kPclmulAvxOsxsave = (1 << 1) | (1 << 27) | (1 << 28);
  if ((cpu_info[2] & kPclmulAvxOsxsave) != kPclmulAvxOsxsave) {
    return false;
  }
  __cpuidex(cpu_info, 7, 0);
  // Check AVX2 (EBX bit 5).
  return (cpu_info[1] & (1 << 5)) != 0;
}

bool SupportsVpclmulqdq() {
  if (!SupportsAvx2()) {
    return false;
  }
  int cpu_info[4];
  __cpuidex(cpu_info, 7, 0);
  // Check VPCLMULQDQ (ECX bit 10).
  return (cpu_info[2] & (1 << 10)) != 0;
}

#elif defined(__aarch64__) && defined(__linux__)

#ifndef HWCAP_CPUID
#define HWCAP_CPUID (1 << 11)
#endif

#define ABSL_INTERNAL_AARCH64_ID_REG_READ(id, val) \
  asm("mrs %0, " #id : "=r"(val))

CpuType GetCpuType() {
  // MIDR_EL1 is not visible to EL0, however the access will be emulated by
  // linux if AT_HWCAP has HWCAP_CPUID set.
  //
  // This method will be unreliable on heterogeneous computing systems (ex:
  // big.LITTLE) since the value of MIDR_EL1 will change based on the calling
  // thread.
  uint64_t hwcaps = getauxval(AT_HWCAP);
  if (hwcaps & HWCAP_CPUID) {
    uint64_t midr = 0;
    ABSL_INTERNAL_AARCH64_ID_REG_READ(MIDR_EL1, midr);
    uint32_t implementer = (midr >> 24) & 0xff;
    uint32_t part_number = (midr >> 4) & 0xfff;
    switch (implementer) {
      case 0x41:
        switch (part_number) {
          case 0xd0c:
            return CpuType::kArmNeoverseN1;
          case 0xd40:
            return CpuType::kArmNeoverseV1;
          case 0xd49:
            return CpuType::kArmNeoverseN2;
          case 0xd4f: {
            uint64_t isar0 = 0;
            ABSL_INTERNAL_AARCH64_ID_REG_READ(ID_AA64ISAR0_EL1, isar0);
            if (((isar0 >> 60) & 0xf) == 0x0) {
              return CpuType::kNvidiaGrace;
            }
            return CpuType::kArmNeoverseV2;
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
        break;
      case 0xc0:
        switch (part_number) {
          case 0xac3:
            return CpuType::kAmpereSiryn;
          default:
            return CpuType::kUnknown;
        }
        break;
      default:
        return CpuType::kUnknown;
    }
  }
  return CpuType::kUnknown;
}

bool SupportsArmCRC32PMULL() {
#if defined(HWCAP_CRC32) && defined(HWCAP_PMULL)
  uint64_t hwcaps = getauxval(AT_HWCAP);
  return (hwcaps & HWCAP_CRC32) && (hwcaps & HWCAP_PMULL);
#else
  return false;
#endif
}

bool SupportsBmi2() { return false; }

bool SupportsAvx2() { return false; }

bool SupportsVpclmulqdq() { return false; }

#elif defined(__aarch64__) && defined(__APPLE__)

CpuType GetCpuType() { return CpuType::kUnknown; }

template <typename T>
static std::optional<T> ReadSysctlByName(const char* name) {
  T val;
  size_t val_size = sizeof(T);
  int ret = sysctlbyname(name, &val, &val_size, nullptr, 0);
  if (ret == -1) {
    return std::nullopt;
  }
  return val;
}

bool SupportsArmCRC32PMULL() {
  static const bool supported = []() {
  // Newer XNU kernels support querying all capabilities in a single
  // sysctlbyname.
#if defined(CAP_BIT_CRC32) && defined(CAP_BIT_FEAT_PMULL)
    const std::optional<uint64_t> caps =
        ReadSysctlByName<uint64_t>("hw.optional.arm.caps");
    if (caps.has_value()) {
      constexpr uint64_t kCrc32AndPmullCaps =
          (uint64_t{1} << CAP_BIT_CRC32) | (uint64_t{1} << CAP_BIT_FEAT_PMULL);
      return (*caps & kCrc32AndPmullCaps) == kCrc32AndPmullCaps;
    }
#endif

    // https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics#3915619
    const std::optional<int> armv8_crc32 =
        ReadSysctlByName<int>("hw.optional.armv8_crc32");
    if (armv8_crc32.value_or(0) == 0) {
      return false;
    }
    // https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics#3918855
    const std::optional<int> feat_pmull =
        ReadSysctlByName<int>("hw.optional.arm.FEAT_PMULL");
    if (feat_pmull.value_or(0) == 0) {
      return false;
    }
    return true;
  }();
  return supported;
}

bool SupportsBmi2() { return false; }

bool SupportsAvx2() { return false; }

bool SupportsVpclmulqdq() { return false; }

#else

CpuType GetCpuType() { return CpuType::kUnknown; }

bool SupportsArmCRC32PMULL() { return false; }

bool SupportsBmi2() { return false; }

bool SupportsAvx2() { return false; }

bool SupportsVpclmulqdq() { return false; }

#endif

// Returns how many hardware contexts per CPU exist. Note: AMD CPUs prior to Zen
// 2 (Rome, 2019) do not support CPUID leaf 0xb. We intentionally avoid falling
// back to leaf 1 ebx[23:16] because it reports total logical processors per
// package (not threads per core), which risks false positives on older
// multi-core non-SMT chips. Pre-Zen 2 AMD safely defaults to 1.
int NumContextsPerCPU() {
#if defined(__x86_64__) || defined(_M_X64)
  int info[4];
  __cpuid(info, 0);
  if (info[0] < 0xb) {
    return 1;
  }

  __cpuid(info, 1);
  bool has_ht = (info[3] & (1 << 28)) != 0;
  if (!has_ht) {
    return 1;
  }

  for (int sub_leaf = 0; sub_leaf < 4; ++sub_leaf) {
    __cpuidex(info, 0xb, sub_leaf);
    int level_type = (info[2] >> 8) & 0xff;
    if (level_type == 0) {
      break;
    }
    if (level_type == 1) {
      int num_threads = info[1] & 0x0ffff;
      if (num_threads >= 1) {
        return num_threads;
      }
    }
  }

  return 1;
#else
  return 1;
#endif
}

bool IsSMTEnabled() { return NumContextsPerCPU() > 1; }

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
