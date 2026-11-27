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

#ifndef ABSL_BASE_INTERNAL_CPU_DETECT_H_
#define ABSL_BASE_INTERNAL_CPU_DETECT_H_

#include "absl/base/config.h"

// Exclude CUDA device compilation and Windows ARM64EC (which defines _M_X64),
// matching Abseil's SIMD macro guards in absl/base/config.h.
#if !defined(__CUDA_ARCH__)
#if defined(__x86_64__) || defined(__x86_64) || defined(__i386__) ||    \
    defined(__i386) ||                                                  \
    ((defined(_M_X64) || defined(_M_AMD64)) && !defined(_M_ARM64EC)) || \
    defined(_M_IX86)
#define ABSL_INTERNAL_CPU_DETECT_X86 1
#elif defined(__aarch64__) || defined(__arm64__) || defined(__arm__) ||  \
    defined(__ARMEL__) || (defined(_M_ARM64) && !defined(_M_ARM64EC)) || \
    defined(_M_ARM)
#define ABSL_INTERNAL_CPU_DETECT_ARM 1
#elif defined(__powerpc64__) || defined(__PPC64__) || defined(__powerpc__) || \
    defined(__PPC__) || defined(__ppc__)
#define ABSL_INTERNAL_CPU_DETECT_PPC 1
#endif
#endif  // !defined(__CUDA_ARCH__)

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// Enumeration of architectures that we have special-case tuning parameters for.
// This set may change over time.
enum class CpuType {
  kUnknown,
  kIntelHaswell,
  kAmdRome,
  kAmdNaples,
  kAmdMilan,
  kAmdGenoa,
  kAmdSiena,
  kAmdTurin,
  kAmdVenice,
  kAmdRyzenV3000,
  kIntelCascadelakeXeon,
  kIntelSkylakeXeon,
  kIntelBroadwell,
  kIntelIcelake,
  kIntelSapphirerapids,
  kIntelEmeraldrapids,
  kIntelGraniterapids,
  kIntelSkylake,
  kIntelIvybridge,
  kIntelSandybridge,
  kIntelWestmere,
  kArmNeoverseN1,
  kArmNeoverseV1,
  kAmpereSiryn,
  kArmNeoverseN2,
  kArmNeoverseV2,
  kArmNeoverseV3,
  kArmNeoverseN3,
  kArmNeoverseN4,
  kNvidiaGrace,
  kNvidiaVera,
};

// Runtime CPU microarchitecture, SMT topology, and OS-verified instruction-set
// detection.
//
// Features requiring OS register-state support (AVX/YMM on x86) are reported
// true only when both the CPU and the operating system enable them.
struct CpuFeatures {
  CpuType type = CpuType::kUnknown;
  int num_contexts_per_cpu = 1;

#if defined(ABSL_INTERNAL_CPU_DETECT_X86)
  bool has_aes = false;   // AES-NI (CPUID.1:ECX[25]).
  bool has_avx2 = false;  // AVX2 + OS XMM/YMM state (XCR0[2:1]).
  bool has_bmi2 = false;  // BMI2 GPR bit-manip (CPUID.7.0:EBX[8]).
#elif defined(ABSL_INTERNAL_CPU_DETECT_ARM)
  bool has_neon = false;   // Advanced SIMD (NEON / ASIMD).
  bool has_crc32 = false;  // ARMv8 FEAT_CRC32.
  bool has_aes = false;    // ARMv8 FEAT_AES (AESE/AESD/AESMC/AESIMC).
  bool has_pmull = false;  // ARMv8 FEAT_PMULL (64x64->128 polynomial mul).
  bool has_sha3 = false;   // ARMv8.2 FEAT_SHA3 (EOR3/RAX1/XAR/BCAX).
#elif defined(ABSL_INTERNAL_CPU_DETECT_PPC)
  bool has_vsx = false;      // Power ISA 2.06 VSX.
  bool has_vcrypto = false;  // Power ISA 2.07 Vector.Crypto + POWER8 vector.
#endif
};

// Returns the CPU features supported by the current host and OS. Thread-safe;
// queries the hardware/OS once and caches the result.
CpuFeatures GetCpuFeatures() noexcept;

// Returns the type of host CPU this code is running on. Returns kUnknown if
// the host CPU is of unknown type, or if detection otherwise fails.
inline CpuType GetCpuType() noexcept { return GetCpuFeatures().type; }

// Returns how many hardware contexts per CPU exist.
inline int NumContextsPerCPU() noexcept {
  return GetCpuFeatures().num_contexts_per_cpu;
}

// Returns whether the host CPU supports simultaneous multithreading (SMT) and
// if it is enabled.
inline bool IsSMTEnabled() noexcept { return NumContextsPerCPU() > 1; }

// Returns whether the host CPU and OS support AVX2 instructions.
inline bool SupportsAvx2() noexcept {
#if defined(ABSL_INTERNAL_CPU_DETECT_X86)
  return GetCpuFeatures().has_avx2;
#else
  return false;
#endif
}

// Returns whether the host CPU supports BMI2 instructions.
inline bool SupportsBmi2() noexcept {
#if defined(ABSL_INTERNAL_CPU_DETECT_X86)
  return GetCpuFeatures().has_bmi2;
#else
  return false;
#endif
}

// Returns whether the host CPU supports 64-bit ARM CRC32 and PMULL
// instructions.
inline bool SupportsArmCRC32PMULL() noexcept {
#if defined(ABSL_INTERNAL_CPU_DETECT_ARM) &&       \
    (defined(__aarch64__) || defined(__arm64__) || \
     (defined(_M_ARM64) && !defined(_M_ARM64EC)))
  const CpuFeatures f = GetCpuFeatures();
  return f.has_neon && f.has_crc32 && f.has_pmull;
#else
  return false;
#endif
}

// Returns whether the host CPU supports 64-bit ARM SHA3 instructions (e.g.,
// EOR3).
inline bool SupportsArmSHA3() noexcept {
#if defined(ABSL_INTERNAL_CPU_DETECT_ARM) &&       \
    (defined(__aarch64__) || defined(__arm64__) || \
     (defined(_M_ARM64) && !defined(_M_ARM64EC)))
  return GetCpuFeatures().has_sha3;
#else
  return false;
#endif
}

// Returns whether the host CPU supports the cryptographic extensions needed by
// the hardware-accelerated Randen implementation.
inline bool SupportsRandenHwAes() noexcept {
#if defined(ABSL_INTERNAL_CPU_DETECT_X86)
  return GetCpuFeatures().has_aes;
#elif defined(ABSL_INTERNAL_CPU_DETECT_ARM)
  const CpuFeatures f = GetCpuFeatures();
  return f.has_neon && f.has_aes;
#elif defined(ABSL_INTERNAL_CPU_DETECT_PPC)
  return GetCpuFeatures().has_vcrypto;
#else
  return false;
#endif
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_CPU_DETECT_H_
