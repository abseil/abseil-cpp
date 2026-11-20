// Copyright 2026 The Abseil Authors
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

#include "absl/base/internal/cpu_detect.h"

#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {
namespace {

using ::testing::Eq;
using ::testing::Ge;

static_assert(std::is_trivially_copyable_v<CpuFeatures>);
static_assert(std::is_trivially_destructible_v<CpuFeatures>);
static_assert(noexcept(GetCpuFeatures()));
static_assert(noexcept(GetCpuType()));
static_assert(noexcept(NumContextsPerCPU()));
static_assert(noexcept(IsSMTEnabled()));
static_assert(noexcept(SupportsAvx2()));
static_assert(noexcept(SupportsBmi2()));
static_assert(noexcept(SupportsArmCRC32PMULL()));
static_assert(noexcept(SupportsArmSHA3()));
static_assert(noexcept(SupportsRandenHwAes()));

TEST(CpuDetectTest, IsDeterministicAndImplicationConsistent) {
  const CpuFeatures a = GetCpuFeatures();
  const CpuFeatures b = GetCpuFeatures();

  EXPECT_THAT(a.type, Eq(b.type));
  EXPECT_THAT(a.type, Eq(GetCpuType()));
  EXPECT_THAT(a.num_contexts_per_cpu, Eq(b.num_contexts_per_cpu));
  EXPECT_THAT(a.num_contexts_per_cpu, Eq(NumContextsPerCPU()));
  EXPECT_THAT(a.num_contexts_per_cpu, Ge(1));
  EXPECT_THAT(IsSMTEnabled(), Eq(a.num_contexts_per_cpu > 1));

#if defined(ABSL_INTERNAL_CPU_DETECT_X86)
  EXPECT_THAT(a.has_aes, Eq(b.has_aes));
  EXPECT_THAT(a.has_avx2, Eq(b.has_avx2));
  EXPECT_THAT(a.has_bmi2, Eq(b.has_bmi2));

  EXPECT_THAT(SupportsAvx2(), Eq(a.has_avx2));
  EXPECT_THAT(SupportsBmi2(), Eq(a.has_bmi2));
  EXPECT_FALSE(SupportsArmCRC32PMULL());
  EXPECT_FALSE(SupportsArmSHA3());
  EXPECT_THAT(SupportsRandenHwAes(), Eq(a.has_aes));
#elif defined(ABSL_INTERNAL_CPU_DETECT_ARM)
  EXPECT_THAT(a.has_neon, Eq(b.has_neon));
  EXPECT_THAT(a.has_crc32, Eq(b.has_crc32));
  EXPECT_THAT(a.has_aes, Eq(b.has_aes));
  EXPECT_THAT(a.has_pmull, Eq(b.has_pmull));
  EXPECT_THAT(a.has_sha3, Eq(b.has_sha3));

  EXPECT_FALSE(SupportsAvx2());
  EXPECT_FALSE(SupportsBmi2());
  EXPECT_THAT(SupportsRandenHwAes(), Eq(a.has_neon && a.has_aes));
#if defined(__aarch64__) || defined(__arm64__) || \
    (defined(_M_ARM64) && !defined(_M_ARM64EC))
  EXPECT_TRUE(a.has_neon);
  EXPECT_THAT(SupportsArmCRC32PMULL(), Eq(a.has_crc32 && a.has_pmull));
  EXPECT_THAT(SupportsArmSHA3(), Eq(a.has_sha3));
#if defined(__APPLE__)
  EXPECT_TRUE(a.has_aes);
  EXPECT_TRUE(a.has_pmull);
  EXPECT_TRUE(a.has_crc32);
#endif
#else
  EXPECT_FALSE(SupportsArmCRC32PMULL());
  EXPECT_FALSE(SupportsArmSHA3());
#endif
  if (a.has_aes || a.has_pmull || a.has_sha3) {
    EXPECT_TRUE(a.has_neon);
  }
#elif defined(ABSL_INTERNAL_CPU_DETECT_PPC)
  EXPECT_THAT(a.has_vsx, Eq(b.has_vsx));
  EXPECT_THAT(a.has_vcrypto, Eq(b.has_vcrypto));
  EXPECT_FALSE(SupportsAvx2());
  EXPECT_FALSE(SupportsBmi2());
  EXPECT_FALSE(SupportsArmCRC32PMULL());
  EXPECT_FALSE(SupportsArmSHA3());
  EXPECT_THAT(SupportsRandenHwAes(), Eq(a.has_vcrypto));
  if (a.has_vcrypto) EXPECT_TRUE(a.has_vsx);
#else
  EXPECT_FALSE(SupportsAvx2());
  EXPECT_FALSE(SupportsBmi2());
  EXPECT_FALSE(SupportsArmCRC32PMULL());
  EXPECT_FALSE(SupportsArmSHA3());
  EXPECT_FALSE(SupportsRandenHwAes());
#endif
}

TEST(CpuDetectTest, MatchesCompilerBuiltinCpuSupportsOnLinuxX86) {
#if defined(ABSL_INTERNAL_CPU_DETECT_X86) && defined(__linux__) && \
    (defined(__GNUC__) || ABSL_HAVE_BUILTIN(__builtin_cpu_supports))
  const CpuFeatures f = GetCpuFeatures();
  EXPECT_THAT(f.has_aes, Eq(__builtin_cpu_supports("aes") != 0));
  EXPECT_THAT(SupportsAvx2(), Eq(__builtin_cpu_supports("avx2") != 0));
  EXPECT_THAT(SupportsBmi2(), Eq(__builtin_cpu_supports("bmi2") != 0));
#endif
}

TEST(CpuDetectTest, MatchesCompilerBuiltinCpuSupportsOnLinuxPpc) {
#if defined(ABSL_INTERNAL_CPU_DETECT_PPC) && defined(__linux__) &&        \
    defined(__GLIBC__) &&                                                 \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 23)) &&       \
    ((defined(__clang__) && ABSL_HAVE_BUILTIN(__builtin_cpu_supports)) || \
     defined(__PPC_CPU_SUPPORTS__))
  const CpuFeatures f = GetCpuFeatures();
  const bool builtin_vsx = __builtin_cpu_supports("vsx") != 0;
  const bool builtin_vcrypto = builtin_vsx &&
                               __builtin_cpu_supports("arch_2_07") != 0 &&
                               __builtin_cpu_supports("vcrypto") != 0;
  EXPECT_THAT(f.has_vsx, Eq(builtin_vsx));
  EXPECT_THAT(f.has_vcrypto, Eq(builtin_vcrypto));
  EXPECT_THAT(SupportsRandenHwAes(), Eq(builtin_vcrypto));
#endif
}

}  // namespace
}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
