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

#include "absl/random/internal/salted_seed_seq.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/random/internal/seed_material.h"

using absl::random_internal::GetSaltMaterial;
using absl::random_internal::MakeSaltedSeedSeq;
using absl::random_internal::SaltedSeedSeq;
using testing::Eq;
using testing::Pointwise;

namespace {

template <typename Sseq>
void ConformsToInterface() {
  // Check that the SeedSequence can be default-constructed.
  {
    Sseq default_constructed_seq;
  }
  // Check that the SeedSequence can be constructed with two iterators.
  {
    uint32_t init_array[] = {1, 3, 5, 7, 9};
    Sseq iterator_constructed_seq(std::begin(init_array), std::end(init_array));
  }
  // Check that the SeedSequence can be std::initializer_list-constructed.
  {
    Sseq list_constructed_seq = {1, 3, 5, 7, 9, 11, 13};
  }
  // Check that param() and size() return state provided to constructor.
  {
    uint32_t init_array[] = {1, 2, 3, 4, 5};
    Sseq seq(std::begin(init_array), std::end(init_array));
    EXPECT_EQ(seq.size(), std::size(init_array));

    std::vector<uint32_t> state_vector;
    seq.param(std::back_inserter(state_vector));

    EXPECT_EQ(state_vector.size(), std::size(init_array));
    for (int i = 0; i < state_vector.size(); i++) {
      EXPECT_EQ(state_vector[i], i + 1);
    }
  }
  // Check for presence of generate() method.
  {
    Sseq seq;
    uint32_t seeds[5];

    seq.generate(std::begin(seeds), std::end(seeds));
  }
}

TEST(SaltedSeedSeq, CheckInterfaces) {
  // Control case
  ConformsToInterface<std::seed_seq>();

  // Abseil classes
  ConformsToInterface<SaltedSeedSeq<std::seed_seq>>();
}

TEST(SaltedSeedSeq, CheckConstructingFromOtherSequence) {
  std::vector<uint32_t> seed_values(10, 1);
  std::seed_seq seq(seed_values.begin(), seed_values.end());
  auto salted_seq = MakeSaltedSeedSeq(std::move(seq));

  EXPECT_EQ(seq.size(), salted_seq.size());

  std::vector<uint32_t> param_result;
  seq.param(std::back_inserter(param_result));

  EXPECT_EQ(seed_values, param_result);
}

TEST(SaltedSeedSeq, SaltedSaltedSeedSeqIsNotDoubleSalted) {
  uint32_t init[] = {1, 3, 5, 7, 9};

  std::seed_seq seq(std::begin(init), std::end(init));

  // The first salting.
  SaltedSeedSeq<std::seed_seq> salted_seq = MakeSaltedSeedSeq(std::move(seq));
  uint32_t a[16];
  salted_seq.generate(std::begin(a), std::end(a));

  // The second salting (rvalue).
  SaltedSeedSeq<std::seed_seq> salted_salted_seq =
      MakeSaltedSeedSeq(std::move(salted_seq));
  uint32_t b[16];
  salted_salted_seq.generate(std::begin(b), std::end(b));

  // ... both should be equal.
  EXPECT_THAT(b, Pointwise(Eq(), a)) << "a[0] " << a[0];

  // Lvalue SaltedSeedSeq must also not be double-salted.
  auto lvalue_salted_seq = MakeSaltedSeedSeq(salted_salted_seq);
  uint32_t c[16];
  lvalue_salted_seq.generate(std::begin(c), std::end(c));
  EXPECT_THAT(c, Pointwise(Eq(), a));

  // Const lvalue and const rvalue SaltedSeedSeq must also not be double-salted.
  const SaltedSeedSeq<std::seed_seq> const_salted_seq(std::begin(init),
                                                      std::end(init));
  auto from_const_lvalue = MakeSaltedSeedSeq(const_salted_seq);
  uint32_t e[16];
  from_const_lvalue.generate(std::begin(e), std::end(e));
  EXPECT_THAT(e, Pointwise(Eq(), a));

  auto from_const_rvalue = MakeSaltedSeedSeq(std::move(const_salted_seq));
  uint32_t f[16];
  from_const_rvalue.generate(std::begin(f), std::end(f));
  EXPECT_THAT(f, Pointwise(Eq(), a));
}

TEST(SaltedSeedSeq, BraceMoveInvokesMoveConstructor) {
  SaltedSeedSeq<std::seed_seq> expected_seq{1, 3, 5, 7, 9};
  uint32_t expected[16];
  expected_seq.generate(std::begin(expected), std::end(expected));

  SaltedSeedSeq<std::seed_seq> source{1, 3, 5, 7, 9};
  SaltedSeedSeq<std::seed_seq> brace_moved{std::move(source)};
  uint32_t actual[16];
  brace_moved.generate(std::begin(actual), std::end(actual));
  EXPECT_THAT(actual, Pointwise(Eq(), expected));
}

TEST(SaltedSeedSeq, MakeSaltedSeedSeqReturnsOwningFromNonOwningRef) {
  uint32_t init[] = {1, 3, 5, 7, 9};
  SaltedSeedSeq<std::seed_seq> expected_seq(std::begin(init), std::end(init));
  uint32_t expected[16];
  expected_seq.generate(std::begin(expected), std::end(expected));

  std::optional<SaltedSeedSeq<std::seed_seq>> owned_from_mutable;
  std::optional<SaltedSeedSeq<std::seed_seq>> owned_from_const;
  {
    std::seed_seq raw(std::begin(init), std::end(init));
    auto made_mutable = MakeSaltedSeedSeq(SaltedSeedSeq<std::seed_seq&>(raw));
    static_assert(
        std::is_same_v<decltype(made_mutable), SaltedSeedSeq<std::seed_seq>>);
    owned_from_mutable.emplace(std::move(made_mutable));

    const std::seed_seq const_raw(std::begin(init), std::end(init));
    auto made_const =
        MakeSaltedSeedSeq(SaltedSeedSeq<const std::seed_seq&>(const_raw));
    static_assert(
        std::is_same_v<decltype(made_const), SaltedSeedSeq<std::seed_seq>>);
    owned_from_const.emplace(std::move(made_const));
  }

  uint32_t from_mutable[16];
  owned_from_mutable->generate(std::begin(from_mutable),
                               std::end(from_mutable));
  EXPECT_THAT(from_mutable, Pointwise(Eq(), expected));

  uint32_t from_const[16];
  owned_from_const->generate(std::begin(from_const), std::end(from_const));
  EXPECT_THAT(from_const, Pointwise(Eq(), expected));
}

TEST(SaltedSeedSeq, ConstructorConstraints) {
  static_assert(std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                        std::initializer_list<int>>);
  static_assert(std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                        std::initializer_list<uint32_t>>);
  static_assert(std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                        std::initializer_list<uint64_t>>);
  static_assert(!std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                         std::initializer_list<double>>);
  static_assert(!std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                         std::initializer_list<const char*>>);
  static_assert(std::is_constructible_v<SaltedSeedSeq<std::seed_seq>, uint32_t*,
                                        uint32_t*>);
  static_assert(
      !std::is_constructible_v<SaltedSeedSeq<std::seed_seq>, double*, double*>);
  static_assert(
      !std::is_constructible_v<SaltedSeedSeq<std::seed_seq>, double, double>);
  static_assert(
      !std::is_constructible_v<SaltedSeedSeq<std::seed_seq>, void*, void*>);

  static_assert(
      !std::is_default_constructible_v<SaltedSeedSeq<std::seed_seq&>>);
  static_assert(!std::is_constructible_v<SaltedSeedSeq<std::seed_seq&>,
                                         std::initializer_list<uint32_t>>);
  static_assert(!std::is_constructible_v<SaltedSeedSeq<std::seed_seq&>,
                                         uint32_t*, uint32_t*>);
  static_assert(
      std::is_constructible_v<SaltedSeedSeq<std::seed_seq&>, std::seed_seq&>);

  // Note: SaltedSeedSeq<T&&> is rejected by a class-scope static_assert.
  static_assert(absl::random_internal::is_movable_owning_salted_seed_seq<
                SaltedSeedSeq<std::seed_seq>>::value);
  static_assert(!absl::random_internal::is_movable_owning_salted_seed_seq<
                SaltedSeedSeq<std::seed_seq&>>::value);
  static_assert(!absl::random_internal::is_movable_owning_salted_seed_seq<
                const SaltedSeedSeq<std::seed_seq>>::value);

  // Also cover >8 elements in MakeSaltedSeedSeq (out-of-line InlinedVector).
  std::vector<uint32_t> large_init(20);
  for (size_t i = 0; i < large_init.size(); ++i) {
    large_init[i] = static_cast<uint32_t>(i * 3 + 1);
  }
  const std::seed_seq large_const(large_init.begin(), large_init.end());
  SaltedSeedSeq<std::seed_seq> large_direct(large_init.begin(),
                                            large_init.end());
  auto large_made = MakeSaltedSeedSeq(large_const);
  uint32_t expected_large[16];
  uint32_t actual_large[16];
  large_direct.generate(std::begin(expected_large), std::end(expected_large));
  large_made.generate(std::begin(actual_large), std::end(actual_large));
  EXPECT_THAT(actual_large, Pointwise(Eq(), expected_large));

  SaltedSeedSeq<std::seed_seq> from_u32{1u, 2u, 3u};
  EXPECT_EQ(from_u32.size(), 3u);

  SaltedSeedSeq<std::seed_seq> from_u64{uint64_t{1}, uint64_t{2}};
  EXPECT_EQ(from_u64.size(), 2u);
}

TEST(SaltedSeedSeq, SeedMaterialIsSalted) {
  const size_t kNumBlocks = 16;

  uint32_t seed_material[kNumBlocks];
  std::random_device urandom{"/dev/urandom"};
  for (uint32_t& seed : seed_material) {
    seed = urandom();
  }

  std::seed_seq seq(std::begin(seed_material), std::end(seed_material));
  SaltedSeedSeq<std::seed_seq> salted_seq(std::begin(seed_material),
                                          std::end(seed_material));

  bool salt_is_available = GetSaltMaterial().has_value();

  // If salt is available generated sequence should be different.
  if (salt_is_available) {
    uint32_t outputs[kNumBlocks];
    uint32_t salted_outputs[kNumBlocks];

    seq.generate(std::begin(outputs), std::end(outputs));
    salted_seq.generate(std::begin(salted_outputs), std::end(salted_outputs));

    EXPECT_THAT(outputs, Pointwise(testing::Ne(), salted_outputs));
  }
}

TEST(SaltedSeedSeq, GenerateAcceptsDifferentTypes) {
  const size_t kNumBlocks = 4;

  SaltedSeedSeq<std::seed_seq> seq({1, 2, 3});

  uint32_t expected[kNumBlocks];
  seq.generate(std::begin(expected), std::end(expected));

  // 32-bit outputs
  {
    unsigned long seed_material[kNumBlocks];  // NOLINT(runtime/int)
    seq.generate(std::begin(seed_material), std::end(seed_material));
    EXPECT_THAT(seed_material, Pointwise(Eq(), expected));
  }
  {
    unsigned int seed_material[kNumBlocks];  // NOLINT(runtime/int)
    seq.generate(std::begin(seed_material), std::end(seed_material));
    EXPECT_THAT(seed_material, Pointwise(Eq(), expected));
  }

  // 64-bit outputs.
  {
    uint64_t seed_material[kNumBlocks];
    seq.generate(std::begin(seed_material), std::end(seed_material));
    EXPECT_THAT(seed_material, Pointwise(Eq(), expected));
  }
  {
    int64_t seed_material[kNumBlocks];
    seq.generate(std::begin(seed_material), std::end(seed_material));
    EXPECT_THAT(seed_material, Pointwise(Eq(), expected));
  }

  // Volatile uint32_t outputs (routes through DefaultTag rather than Span).
  {
    volatile uint32_t seed_material[kNumBlocks] = {};
    seq.generate(std::begin(seed_material), std::end(seed_material));
    for (size_t i = 0; i < kNumBlocks; ++i) {
      EXPECT_EQ(seed_material[i], expected[i]);
    }
  }

  // Out-of-line (>8) DefaultTag buffer path.
  {
    constexpr size_t kLargeBlocks = 12;
    uint32_t expected_large[kLargeBlocks];
    uint64_t actual_large[kLargeBlocks];
    seq.generate(std::begin(expected_large), std::end(expected_large));
    seq.generate(std::begin(actual_large), std::end(actual_large));
    EXPECT_THAT(actual_large, Pointwise(Eq(), expected_large));
  }
}

struct CountingSeedSeq {
  using result_type = uint32_t;
  size_t reported_size = 0;
  size_t calls = 0;
  size_t last_requested = 0;

  explicit CountingSeedSeq(size_t s) : reported_size(s) {}
  size_t size() const { return reported_size; }
  template <typename OutIt>
  void generate(OutIt begin, OutIt end) {
    ++calls;
    last_requested = static_cast<size_t>(std::distance(begin, end));
    uint32_t v = 1;
    for (OutIt it = begin; it != end; ++it) {
      *it = v++;
    }
  }
};

template <size_t S, size_t N>
void VerifyTruncatedCollisionSeparated(const uint32_t (&seed_a)[S],
                                       const uint32_t (&seed_b)[S]) {
  std::seed_seq raw_a(std::begin(seed_a), std::end(seed_a));
  std::seed_seq raw_b(std::begin(seed_b), std::end(seed_b));
  uint32_t raw_out_a[N] = {};
  uint32_t raw_out_b[N] = {};
  raw_a.generate(std::begin(raw_out_a), std::end(raw_out_a));
  raw_b.generate(std::begin(raw_out_b), std::end(raw_out_b));
  // Precondition: raw std::seed_seq genuinely collides at width N.
  ASSERT_THAT(raw_out_a, Pointwise(Eq(), raw_out_b));

  SaltedSeedSeq<std::seed_seq> salted_a(std::begin(seed_a), std::end(seed_a));
  SaltedSeedSeq<std::seed_seq> salted_b(std::begin(seed_b), std::end(seed_b));

  // size() and param() remain unsalted per [rand.req.seedseq].
  EXPECT_EQ(salted_a.size(), S);
  EXPECT_EQ(salted_b.size(), S);
  std::vector<uint32_t> params_a;
  salted_a.param(std::back_inserter(params_a));
  EXPECT_THAT(params_a, Pointwise(Eq(), seed_a));

  if (GetSaltMaterial().has_value()) {
    uint32_t salted_out_a[N] = {};
    uint32_t salted_out_b[N] = {};
    salted_a.generate(std::begin(salted_out_a), std::end(salted_out_a));
    salted_b.generate(std::begin(salted_out_b), std::end(salted_out_b));
    EXPECT_THAT(salted_out_a, testing::Not(Pointwise(Eq(), salted_out_b)));
  }
}

TEST(SaltedSeedSeq, EliminatesKnownStdSeedSeqTruncationCollisions) {
  // Exact [rand.util.seedseq] collision pairs where two distinct s-word seeds
  // yield bit-for-bit identical n-word std::seed_seq::generate() outputs.
  constexpr uint32_t kS2A[2] = {0x00000003u, 0xef243a0au};
  constexpr uint32_t kS2B[2] = {0x0b8bb54eu, 0xfaafef55u};
  VerifyTruncatedCollisionSeparated<2, 2>(kS2A, kS2B);

  constexpr uint32_t kS3A[3] = {0x0000269du, 0x4cc2d2d4u, 0x507fd03cu};
  constexpr uint32_t kS3B[3] = {0x00033c9bu, 0xa08a4e6cu, 0x5082e63au};
  VerifyTruncatedCollisionSeparated<3, 3>(kS3A, kS3B);

  constexpr uint32_t kS4A[4] = {0x00010253u, 0x4c1127d0u, 0xc406ad4cu,
                                0x13a2704du};
  constexpr uint32_t kS4B[4] = {0x000135d7u, 0xc89ecda0u, 0x24996d88u,
                                0x13a2a3d1u};
  VerifyTruncatedCollisionSeparated<4, 4>(kS4A, kS4B);

  constexpr uint32_t kS8N8A[8] = {0x7496df94u, 0x2f701878u, 0xf374430bu,
                                  0x33e4d291u, 0x8e5b51a3u, 0x0f511665u,
                                  0xd44a6ec8u, 0xbb00fd0au};
  constexpr uint32_t kS8N8B[8] = {0x64e949bau, 0x47fb3261u, 0xdeaae914u,
                                  0xfdd32ba0u, 0xa4d1fd8du, 0x357706e1u,
                                  0xc49cd8eeu, 0xbb01dbdfu};
  VerifyTruncatedCollisionSeparated<8, 8>(kS8N8A, kS8N8B);

  constexpr uint32_t kS8N4A[8] = {0x00000001u, 0x00000002u, 0x00000003u,
                                  0x00000004u, 0x0000bf91u, 0x67c0a593u,
                                  0x313389e7u, 0xee5c8e5cu};
  constexpr uint32_t kS8N4B[8] = {0x00000001u, 0x00000002u, 0x00000003u,
                                  0x00000004u, 0x00011835u, 0xa5168553u,
                                  0xcee80a7au, 0xee5ce700u};
  VerifyTruncatedCollisionSeparated<8, 4>(kS8N4A, kS8N4B);
}

TEST(SaltedSeedSeq, ExpansionRespectsZeroSizeAndCapsAt60Words) {
  uint32_t out4[4] = {};

  // Zero-size sequences (e.g. stateful entropy seeders) must never over-pull.
  CountingSeedSeq zero_seq(0);
  SaltedSeedSeq<CountingSeedSeq&> salted_zero(zero_seq);
  salted_zero.generate(std::begin(out4), std::end(out4));
  EXPECT_EQ(zero_seq.calls, 1u);
  EXPECT_EQ(zero_seq.last_requested, 4u);

  if (GetSaltMaterial().has_value()) {
    // s = 4, n = 4 expands to 2 * s = 8.
    CountingSeedSeq seq4(4);
    SaltedSeedSeq<CountingSeedSeq&> salted4(seq4);
    salted4.generate(std::begin(out4), std::end(out4));
    EXPECT_EQ(seq4.last_requested, 8u);

    // s >= 30 caps expansion at 60 without overflow even at SIZE_MAX.
    CountingSeedSeq seq40(40);
    SaltedSeedSeq<CountingSeedSeq&> salted40(seq40);
    salted40.generate(std::begin(out4), std::end(out4));
    EXPECT_EQ(seq40.last_requested, 60u);

    CountingSeedSeq seq_max(static_cast<size_t>(-1));
    SaltedSeedSeq<CountingSeedSeq&> salted_max(seq_max);
    salted_max.generate(std::begin(out4), std::end(out4));
    EXPECT_EQ(seq_max.last_requested, 60u);

    // Requests already >= min(60, 2 * s) do not expand further.
    uint32_t out8[8] = {};
    salted4.generate(std::begin(out8), std::end(out8));
    EXPECT_EQ(seq4.last_requested, 8u);
  }
}

}  // namespace
