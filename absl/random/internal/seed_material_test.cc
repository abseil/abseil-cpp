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

#include "absl/random/internal/seed_material.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <random>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/types/span.h"

#ifdef __ANDROID__
// Android assert messages only go to system log, so death tests cannot inspect
// the message for matching.
#define ABSL_EXPECT_DEATH_IF_SUPPORTED(statement, regex) \
  EXPECT_DEATH_IF_SUPPORTED(statement, ".*")
#else
#define ABSL_EXPECT_DEATH_IF_SUPPORTED(statement, regex) \
  EXPECT_DEATH_IF_SUPPORTED(statement, regex)
#endif

namespace {

using testing::Each;
using testing::Eq;
using testing::Ne;
using testing::Not;
using testing::Pointwise;

TEST(SeedBitsToBlocks, VerifyCases) {
  EXPECT_EQ(0, absl::random_internal::SeedBitsToBlocks(0));
  EXPECT_EQ(1, absl::random_internal::SeedBitsToBlocks(1));
  EXPECT_EQ(1, absl::random_internal::SeedBitsToBlocks(31));
  EXPECT_EQ(1, absl::random_internal::SeedBitsToBlocks(32));
  EXPECT_EQ(2, absl::random_internal::SeedBitsToBlocks(33));
  EXPECT_EQ(4, absl::random_internal::SeedBitsToBlocks(127));
  EXPECT_EQ(4, absl::random_internal::SeedBitsToBlocks(128));
  EXPECT_EQ(5, absl::random_internal::SeedBitsToBlocks(129));
}

TEST(ReadSeedMaterialFromOSEntropy, SuccessiveReadsAreDistinct) {
  constexpr size_t kSeedMaterialSize = 64;
  uint32_t seed_material_1[kSeedMaterialSize] = {};
  uint32_t seed_material_2[kSeedMaterialSize] = {};

  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::Span<uint32_t>(seed_material_1, kSeedMaterialSize)));
  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::Span<uint32_t>(seed_material_2, kSeedMaterialSize)));

  EXPECT_THAT(seed_material_1, Pointwise(Ne(), seed_material_2));
}

TEST(ReadSeedMaterialFromOSEntropy, LargeBufferAcrossChunks) {
  constexpr size_t kChunkWords = 64;  // 256 bytes (getentropy() chunk size).
  constexpr size_t kNumChunks = 16;
  constexpr size_t kSeedMaterialSize = kChunkWords * kNumChunks;
  constexpr uint32_t kSentinel = 0xAAAAAAAAu;
  std::vector<uint32_t> seed_material_1(kSeedMaterialSize, kSentinel);
  std::vector<uint32_t> seed_material_2(kSeedMaterialSize, kSentinel);

  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::MakeSpan(seed_material_1)));
  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::MakeSpan(seed_material_2)));

  const auto span_1 = absl::MakeConstSpan(seed_material_1);
  const auto span_2 = absl::MakeConstSpan(seed_material_2);
  const auto first_chunk_1 = span_1.first(kChunkWords);
  const auto first_chunk_2 = span_2.first(kChunkWords);
  for (size_t chunk = 0; chunk < kNumChunks; ++chunk) {
    SCOPED_TRACE(chunk);
    const size_t offset = chunk * kChunkWords;
    const auto chunk_1 = span_1.subspan(offset, kChunkWords);
    const auto chunk_2 = span_2.subspan(offset, kChunkWords);
    EXPECT_THAT(chunk_1.first(16), Not(Each(Eq(kSentinel))));
    EXPECT_THAT(chunk_1.first(16), Not(Each(Eq(0u))));
    EXPECT_THAT(chunk_1.last(16), Not(Each(Eq(kSentinel))));
    EXPECT_THAT(chunk_1.last(16), Not(Each(Eq(0u))));
    EXPECT_THAT(chunk_2.first(16), Not(Each(Eq(kSentinel))));
    EXPECT_THAT(chunk_2.first(16), Not(Each(Eq(0u))));
    EXPECT_THAT(chunk_2.last(16), Not(Each(Eq(kSentinel))));
    EXPECT_THAT(chunk_2.last(16), Not(Each(Eq(0u))));
    EXPECT_THAT(chunk_1.first(16), Ne(chunk_2.first(16)));
    EXPECT_THAT(chunk_1.last(16), Ne(chunk_2.last(16)));
    if (chunk > 0) {
      EXPECT_THAT(chunk_1, Ne(first_chunk_1));
      EXPECT_THAT(chunk_2, Ne(first_chunk_2));
      EXPECT_THAT(chunk_1,
                  Ne(span_1.subspan(offset - kChunkWords, kChunkWords)));
      EXPECT_THAT(chunk_2,
                  Ne(span_2.subspan(offset - kChunkWords, kChunkWords)));
    }
  }
}

TEST(ReadSeedMaterialFromOSEntropy, ReadZeroBytesIsNoOp) {
  uint32_t seed_material[32] = {};
  std::memset(seed_material, 0xAA, sizeof(seed_material));
  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::Span<uint32_t>(seed_material, 0)));

  EXPECT_THAT(seed_material, Each(Eq(0xAAAAAAAA)));
}

TEST(ReadSeedMaterialFromOSEntropy, NullPtrVectorArgument) {
#ifdef NDEBUG
  EXPECT_FALSE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::Span<uint32_t>(nullptr, 32)));
#else
  bool result;
  ABSL_EXPECT_DEATH_IF_SUPPORTED(
      result = absl::random_internal::ReadSeedMaterialFromOSEntropy(
          absl::Span<uint32_t>(nullptr, 32)),
      "!= nullptr");
  (void)result;  // suppress unused-variable warning
#endif
}

TEST(ReadSeedMaterialFromURBG, SeedMaterialEqualsVariateSequence) {
  // Two default-constructed instances of std::mt19937_64 are guaranteed to
  // produce equal variate-sequences.
  std::mt19937 urbg_1;
  std::mt19937 urbg_2;
  constexpr size_t kSeedMaterialSize = 1024;
  uint32_t seed_material[kSeedMaterialSize] = {};

  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromURBG(
      &urbg_1, absl::Span<uint32_t>(seed_material, kSeedMaterialSize)));
  for (uint32_t seed : seed_material) {
    EXPECT_EQ(seed, urbg_2());
  }
}

TEST(ReadSeedMaterialFromURBG, ReadZeroBytesIsNoOp) {
  std::mt19937_64 urbg;
  uint32_t seed_material[32];
  std::memset(seed_material, 0xAA, sizeof(seed_material));
  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromURBG(
      &urbg, absl::Span<uint32_t>(seed_material, 0)));

  EXPECT_THAT(seed_material, Each(Eq(0xAAAAAAAA)));
}

TEST(ReadSeedMaterialFromURBG, NullUrbgArgument) {
  constexpr size_t kSeedMaterialSize = 32;
  uint32_t seed_material[kSeedMaterialSize];
#ifdef NDEBUG
  EXPECT_FALSE(absl::random_internal::ReadSeedMaterialFromURBG<std::mt19937_64>(
      nullptr, absl::Span<uint32_t>(seed_material, kSeedMaterialSize)));
#else
  bool result;
  ABSL_EXPECT_DEATH_IF_SUPPORTED(
      result = absl::random_internal::ReadSeedMaterialFromURBG<std::mt19937_64>(
          nullptr, absl::Span<uint32_t>(seed_material, kSeedMaterialSize)),
      "!= nullptr");
  (void)result;  // suppress unused-variable warning
#endif
}

TEST(ReadSeedMaterialFromURBG, NullPtrVectorArgument) {
  std::mt19937_64 urbg;
#ifdef NDEBUG
  EXPECT_FALSE(absl::random_internal::ReadSeedMaterialFromURBG(
      &urbg, absl::Span<uint32_t>(nullptr, 32)));
#else
  bool result;
  ABSL_EXPECT_DEATH_IF_SUPPORTED(
      result = absl::random_internal::ReadSeedMaterialFromURBG(
          &urbg, absl::Span<uint32_t>(nullptr, 32)),
      "!= nullptr");
  (void)result;  // suppress unused-variable warning
#endif
}

// The avalanche effect is a desirable cryptographic property of hashes in which
// changing a single bit in the input causes each bit of the output to be
// changed with probability near 50%.
//
// https://en.wikipedia.org/wiki/Avalanche_effect

TEST(MixSequenceIntoSeedMaterial, AvalancheEffectTestOneBitLong) {
  std::vector<uint32_t> seed_material = {1, 2, 3, 4, 5, 6, 7, 8};

  // For every 32-bit number with exactly one bit set, verify the avalanche
  // effect holds.  In order to reduce flakiness of tests, accept values
  // anywhere in the range of 30%-70%.
  for (uint32_t v = 1; v != 0; v <<= 1) {
    std::vector<uint32_t> seed_material_copy = seed_material;
    absl::random_internal::MixIntoSeedMaterial(
        absl::Span<uint32_t>(&v, 1),
        absl::Span<uint32_t>(seed_material_copy.data(),
                             seed_material_copy.size()));

    uint32_t changed_bits = 0;
    for (size_t i = 0; i < seed_material.size(); i++) {
      std::bitset<sizeof(uint32_t) * 8> bitset(seed_material[i] ^
                                               seed_material_copy[i]);
      changed_bits += bitset.count();
    }

    EXPECT_LE(changed_bits, 0.7 * sizeof(uint32_t) * 8 * seed_material.size());
    EXPECT_GE(changed_bits, 0.3 * sizeof(uint32_t) * 8 * seed_material.size());
  }
}

TEST(MixSequenceIntoSeedMaterial, AvalancheEffectTestOneBitShort) {
  std::vector<uint32_t> seed_material = {1};

  // For every 32-bit number with exactly one bit set, verify the avalanche
  // effect holds.  In order to reduce flakiness of tests, accept values
  // anywhere in the range of 30%-70%.
  for (uint32_t v = 1; v != 0; v <<= 1) {
    std::vector<uint32_t> seed_material_copy = seed_material;
    absl::random_internal::MixIntoSeedMaterial(
        absl::Span<uint32_t>(&v, 1),
        absl::Span<uint32_t>(seed_material_copy.data(),
                             seed_material_copy.size()));

    uint32_t changed_bits = 0;
    for (size_t i = 0; i < seed_material.size(); i++) {
      std::bitset<sizeof(uint32_t) * 8> bitset(seed_material[i] ^
                                               seed_material_copy[i]);
      changed_bits += bitset.count();
    }

    EXPECT_LE(changed_bits, 0.7 * sizeof(uint32_t) * 8 * seed_material.size());
    EXPECT_GE(changed_bits, 0.3 * sizeof(uint32_t) * 8 * seed_material.size());
  }
}

TEST(CleanseSeedMaterial, ZeroesBufferContents) {
  uint32_t buf[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
  // Null pointer and zero-length calls must be safe no-ops.
  absl::random_internal::CleanseSeedMaterial(nullptr, 0);
  absl::random_internal::CleanseSeedMaterial(nullptr, 16);
  absl::random_internal::CleanseSeedMaterial(buf, 0);
  EXPECT_EQ(buf[0], 1u);

  absl::random_internal::CleanseSeedMaterial(buf, sizeof(buf));
  EXPECT_THAT(buf, Each(Eq(0u)));
}

TEST(ScopedCleanse, ZeroesBufferOnScopeExit) {
  uint32_t raw_buf[4] = {1u, 2u, 3u, 4u};
  {
    absl::random_internal::ScopedCleanse cleaner(raw_buf, sizeof(raw_buf));
    EXPECT_EQ(raw_buf[0], 1u);
  }
  EXPECT_THAT(raw_buf, Each(Eq(0u)));

  std::array<uint32_t, 4> span_buf = {5u, 6u, 7u, 8u};
  {
    absl::random_internal::ScopedCleanse cleaner(absl::MakeSpan(span_buf));
    EXPECT_EQ(span_buf[0], 5u);
  }
  EXPECT_THAT(span_buf, Each(Eq(0u)));
}

TEST(NormalizeSaltMaterial, PreservesFourWordsVerbatimAndMatchesKat) {
  using absl::random_internal::NormalizeSaltMaterial;
  using absl::random_internal::SaltMaterial;

  constexpr SaltMaterial kFull = {
      0x01020304u,
      0x05060708u,
      0x090a0b0cu,
      0x0d0e0f10u,
  };
  EXPECT_EQ(NormalizeSaltMaterial(kFull), kFull);

  // Reference SipHash-2-4-128 vector for 1-word input {0x12345678}.
  const uint32_t kSingleWord[1] = {0x12345678u};
  constexpr SaltMaterial kExpectedNorm1 = {
      0x34027de4u,
      0x0ed68fd1u,
      0xe1c4d52au,
      0xa8e31af5u,
  };
  EXPECT_EQ(NormalizeSaltMaterial(kSingleWord), kExpectedNorm1);

  // Distinct lengths and trailing words beyond 32/128 bits must not collide.
  const uint32_t kTwoWordsZeroTail[2] = {0x12345678u, 0u};
  const uint32_t kTwoWordsNonZeroTail[2] = {0x12345678u, 1u};
  EXPECT_NE(NormalizeSaltMaterial(kSingleWord),
            NormalizeSaltMaterial(kTwoWordsZeroTail));
  EXPECT_NE(NormalizeSaltMaterial(kTwoWordsZeroTail),
            NormalizeSaltMaterial(kTwoWordsNonZeroTail));

  const uint32_t kEightWordsA[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 0u};
  const uint32_t kEightWordsB[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 1u};
  EXPECT_NE(NormalizeSaltMaterial(kEightWordsA),
            NormalizeSaltMaterial(kEightWordsB));
}

TEST(DeriveSeedMaterialWithSalt, MatchesSipHashKatAndSeparatesDomains) {
  using absl::random_internal::DeriveSeedMaterialWithSalt;
  using absl::random_internal::SaltMaterial;

  constexpr SaltMaterial kSalt = {
      0x01020304u,
      0x05060708u,
      0x090a0b0cu,
      0x0d0e0f10u,
  };
  constexpr uint32_t kSeed[3] = {0xdeadbeefu, 0xcafebabeu, 0x01234567u};

  // Empty output must be a safe no-op.
  DeriveSeedMaterialWithSalt(kSalt, /*domain_tag=*/2u, kSeed,
                             absl::Span<uint32_t>());

  // Direct <= 4 word output path (reference SipHash-2-4-128 KAT).
  std::array<uint32_t, 4> out4{};
  DeriveSeedMaterialWithSalt(kSalt, /*domain_tag=*/2u, kSeed,
                             absl::MakeSpan(out4));
  constexpr std::array<uint32_t, 4> kExpected4 = {
      0x7d542ffcu,
      0x05ac2984u,
      0xcc36cbc9u,
      0x585ef79du,
  };
  EXPECT_EQ(out4, kExpected4);

  // Wide (> 4 words) 256-bit intermediate key path (reference KAT).
  std::array<uint32_t, 10> out{};
  DeriveSeedMaterialWithSalt(kSalt, /*domain_tag=*/2u, kSeed,
                             absl::MakeSpan(out));
  constexpr std::array<uint32_t, 10> kExpected = {
      0x8eb9a06fu, 0x21b60e8bu, 0xd689ab87u, 0x24bd0bfbu, 0x05954dd6u,
      0xbd2f20c4u, 0x96350bceu, 0xd8c58cafu, 0xb038aa60u, 0x171e3ef0u,
  };
  EXPECT_EQ(out, kExpected);

  // Empty input must be distinct from a single zero word.
  std::array<uint32_t, 4> out_empty{};
  std::array<uint32_t, 4> out_zero{};
  const uint32_t kZero[1] = {0u};
  DeriveSeedMaterialWithSalt(kSalt, /*domain_tag=*/2u, {},
                             absl::MakeSpan(out_empty));
  DeriveSeedMaterialWithSalt(kSalt, /*domain_tag=*/2u, kZero,
                             absl::MakeSpan(out_zero));
  EXPECT_NE(out_empty, out_zero);

  // Flipping a bit in word 3 (bits 96..127) of the salt must change outputs.
  SaltMaterial flipped_salt = kSalt;
  flipped_salt[3] ^= 0x80000000u;
  std::array<uint32_t, 10> out_flipped{};
  DeriveSeedMaterialWithSalt(flipped_salt, /*domain_tag=*/2u, kSeed,
                             absl::MakeSpan(out_flipped));
  EXPECT_THAT(out, Pointwise(Ne(), out_flipped));
}

TEST(PermuteSeedMaterialWithSalt, IsBijectiveForSingleWordAndDiffusesAllBits) {
  using absl::random_internal::PermuteSeedMaterialWithSalt;
  using absl::random_internal::SaltMaterial;

  constexpr SaltMaterial kSalt = {
      0x243f6a88u,
      0x85a308d3u,
      0x13198a2eu,
      0x03707344u,
  };

  // Empty span must be a safe no-op.
  PermuteSeedMaterialWithSalt(kSalt, /*domain_tag=*/1u, /*param_size=*/0u,
                              absl::Span<uint32_t>());

  // Reference SipHash-2-4-128 Feistel KATs for n = 1 and odd n = 5.
  uint32_t kat1 = 0x12345678u;
  PermuteSeedMaterialWithSalt(kSalt, /*domain_tag=*/1u, /*param_size=*/1u,
                              absl::MakeSpan(&kat1, 1));
  EXPECT_EQ(kat1, 0x07052b2au);

  std::array<uint32_t, 5> kat5 = {1u, 2u, 3u, 4u, 5u};
  PermuteSeedMaterialWithSalt(kSalt, /*domain_tag=*/1u, /*param_size=*/5u,
                              absl::MakeSpan(kat5));
  constexpr std::array<uint32_t, 5> kExpectedKat5 = {
      0x4cf87f87u, 0x56902be0u, 0x2069d875u, 0xab38df4fu, 0x531447b9u,
  };
  EXPECT_EQ(kat5, kExpectedKat5);

  // Two-word balanced Feistel must diffuse both halves.
  std::array<uint32_t, 2> pair_a = {0u, 0u};
  std::array<uint32_t, 2> pair_b = {0u, 1u};
  PermuteSeedMaterialWithSalt(kSalt, /*domain_tag=*/1u, /*param_size=*/2u,
                              absl::MakeSpan(pair_a));
  PermuteSeedMaterialWithSalt(kSalt, /*domain_tag=*/1u, /*param_size=*/2u,
                              absl::MakeSpan(pair_b));
  EXPECT_THAT(pair_a, Pointwise(Ne(), pair_b));

  // Single-word 6-round Feistel PRP must have zero collisions.
  constexpr size_t kSamples = 10000;
  absl::flat_hash_set<uint32_t> seen;
  seen.reserve(kSamples);
  for (uint32_t i = 0; i < kSamples; ++i) {
    uint32_t word = i;
    PermuteSeedMaterialWithSalt(kSalt, /*domain_tag=*/1u, /*param_size=*/1u,
                                absl::MakeSpan(&word, 1));
    EXPECT_TRUE(seen.insert(word).second);
  }

  // Domain separation: param_size and domain_tag separate identical inputs.
  std::array<uint32_t, 4> a = {1u, 0u, 0u, 0u};
  std::array<uint32_t, 4> b = a;
  std::array<uint32_t, 4> c = a;
  PermuteSeedMaterialWithSalt(kSalt, /*domain_tag=*/1u, /*param_size=*/1u,
                              absl::MakeSpan(a));
  PermuteSeedMaterialWithSalt(kSalt, /*domain_tag=*/1u, /*param_size=*/2u,
                              absl::MakeSpan(b));
  PermuteSeedMaterialWithSalt(kSalt, /*domain_tag=*/3u, /*param_size=*/1u,
                              absl::MakeSpan(c));
  EXPECT_NE(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(b, c);

  // Avalanche across all 60 words when flipping bit 0 of the input or word 3
  // of the 128-bit salt.
  std::array<uint32_t, 60> base{};
  base[0] = 1u;
  std::array<uint32_t, 60> diff_in = base;
  diff_in[0] ^= 1u;
  std::array<uint32_t, 60> diff_salt = base;
  SaltMaterial salt_b = kSalt;
  salt_b[3] ^= 1u;
  PermuteSeedMaterialWithSalt(kSalt, 1u, 1u, absl::MakeSpan(base));
  PermuteSeedMaterialWithSalt(kSalt, 1u, 1u, absl::MakeSpan(diff_in));
  PermuteSeedMaterialWithSalt(salt_b, 1u, 1u, absl::MakeSpan(diff_salt));

  size_t changed_in = 0;
  size_t changed_salt = 0;
  for (size_t i = 0; i < base.size(); ++i) {
    changed_in += std::bitset<32>(base[i] ^ diff_in[i]).count();
    changed_salt += std::bitset<32>(base[i] ^ diff_salt[i]).count();
  }
  const double total_bits = static_cast<double>(base.size() * 32);
  EXPECT_GE(changed_in, 0.4 * total_bits);
  EXPECT_LE(changed_in, 0.6 * total_bits);
  EXPECT_GE(changed_salt, 0.4 * total_bits);
  EXPECT_LE(changed_salt, 0.6 * total_bits);
}

}  // namespace
