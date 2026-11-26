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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/random/internal/seed_material.h"
#include "absl/random/internal/traits.h"

using absl::random_internal::GetSaltMaterial;
using absl::random_internal::MakeSaltedSeedSeq;
using absl::random_internal::SaltedSeedSeq;
using testing::Eq;
using testing::Pointwise;

namespace {

// Arbitrary nothing-up-my-sleeve test seed words.
constexpr std::array<uint32_t, 5> kTestSeed = {1, 3, 5, 7, 9};

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
  std::seed_seq seq(kTestSeed.begin(), kTestSeed.end());

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
  const SaltedSeedSeq<std::seed_seq> const_salted_seq(kTestSeed.begin(),
                                                      kTestSeed.end());
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
  SaltedSeedSeq<std::seed_seq> expected_seq(kTestSeed.begin(), kTestSeed.end());
  uint32_t expected[16];
  expected_seq.generate(std::begin(expected), std::end(expected));

  SaltedSeedSeq<std::seed_seq> source(kTestSeed.begin(), kTestSeed.end());
  SaltedSeedSeq<std::seed_seq> brace_moved{std::move(source)};
  uint32_t actual[16];
  brace_moved.generate(std::begin(actual), std::end(actual));
  EXPECT_THAT(actual, Pointwise(Eq(), expected));
}

TEST(SaltedSeedSeq, MakeSaltedSeedSeqReturnsOwningFromNonOwningRef) {
  SaltedSeedSeq<std::seed_seq> expected_seq(kTestSeed.begin(), kTestSeed.end());
  uint32_t expected[16];
  expected_seq.generate(std::begin(expected), std::end(expected));

  std::optional<SaltedSeedSeq<std::seed_seq>> owned_from_mutable;
  std::optional<SaltedSeedSeq<std::seed_seq>> owned_from_const;
  {
    std::seed_seq raw(kTestSeed.begin(), kTestSeed.end());
    auto made_mutable = MakeSaltedSeedSeq(SaltedSeedSeq<std::seed_seq&>(raw));
    static_assert(
        std::is_same_v<decltype(made_mutable), SaltedSeedSeq<std::seed_seq>>);
    owned_from_mutable.emplace(std::move(made_mutable));

    const std::seed_seq const_raw(kTestSeed.begin(), kTestSeed.end());
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

// SaltedSeedSeq constructor constraints.
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
static_assert(std::is_constructible_v<SaltedSeedSeq<const std::seed_seq&>,
                                      const std::seed_seq&>);
static_assert(std::is_constructible_v<SaltedSeedSeq<const std::seed_seq&>,
                                      std::seed_seq&>);
static_assert(!std::is_constructible_v<SaltedSeedSeq<const std::seed_seq&>,
                                       std::seed_seq>);
static_assert(!std::is_constructible_v<SaltedSeedSeq<const std::seed_seq&>,
                                       const std::seed_seq>);
// Note: SaltedSeedSeq<T&&> is rejected by a class-scope static_assert.

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
}

TEST(SaltedSeedSeq, GenerateAcceptsVolatileUint32Outputs) {
  constexpr size_t kNumBlocks = 4;
  SaltedSeedSeq<std::seed_seq> seq(kTestSeed.begin(), kTestSeed.end());

  uint32_t expected[kNumBlocks] = {};
  seq.generate(std::begin(expected), std::end(expected));

  volatile uint32_t seed_material[kNumBlocks] = {};
  seq.generate(std::begin(seed_material), std::end(seed_material));
  uint32_t copy[kNumBlocks] = {};
  std::copy(std::begin(seed_material), std::end(seed_material),
            std::begin(copy));
  EXPECT_THAT(copy, Pointwise(Eq(), expected));
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

// Known salt-invariant 4-way collision quadruple for raw C++11
// `std::seed_seq{a, b}.generate(2)`, plus an 8-word pair that collides at 4
// outputs (exercising the `s > n` XOF path).
constexpr std::array<std::array<uint32_t, 2>, 4> kFourWayTwoWordCollision = {{
    {0x7ad2099du, 0x20261120u},
    {0x2a001490u, 0xcf541c13u},
    {0x592e1f83u, 0xfe822706u},
    {0x6f465452u, 0x149a5bd5u},
}};
constexpr std::array<uint32_t, 8> kCollisionS8N4A = {
    0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u,
    0x0000bf91u, 0x67c0a593u, 0x313389e7u, 0xee5c8e5cu};
constexpr std::array<uint32_t, 8> kCollisionS8N4B = {
    0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u,
    0x00011835u, 0xa5168553u, 0xcee80a7au, 0xee5ce700u};

TEST(SaltedSeedSeq, SeparatesRawSeedSeqCollisionsPreservingParams) {
  if (!GetSaltMaterial().has_value()) {
    GTEST_SKIP() << "Salt material unavailable";
  }

  // `s <= n` PRP path: 4-way collision quadruple at n = 2.
  std::set<std::array<uint32_t, 2>> raw_outputs;
  std::set<std::array<uint32_t, 2>> salted_outputs;
  for (const auto& seed : kFourWayTwoWordCollision) {
    std::array<uint32_t, 2> raw{};
    std::seed_seq raw_seq(seed.begin(), seed.end());
    raw_seq.generate(raw.begin(), raw.end());
    raw_outputs.insert(raw);

    std::array<uint32_t, 2> salted{};
    SaltedSeedSeq<std::seed_seq> salted_seq(seed.begin(), seed.end());
    EXPECT_EQ(salted_seq.size(), seed.size());
    std::vector<uint32_t> params;
    salted_seq.param(std::back_inserter(params));
    EXPECT_THAT(params, Pointwise(Eq(), seed));
    salted_seq.generate(salted.begin(), salted.end());
    salted_outputs.insert(salted);
  }
  EXPECT_THAT(raw_outputs, testing::SizeIs(1u));
  EXPECT_THAT(salted_outputs, testing::SizeIs(4u));

  // `s > n` XOF path: 8-word pair colliding at n = 4.
  std::array<uint32_t, 4> raw_a{}, raw_b{}, salted_a{}, salted_b{};
  std::seed_seq(kCollisionS8N4A.begin(), kCollisionS8N4A.end())
      .generate(raw_a.begin(), raw_a.end());
  std::seed_seq(kCollisionS8N4B.begin(), kCollisionS8N4B.end())
      .generate(raw_b.begin(), raw_b.end());
  SaltedSeedSeq<std::seed_seq>(kCollisionS8N4A.begin(), kCollisionS8N4A.end())
      .generate(salted_a.begin(), salted_a.end());
  SaltedSeedSeq<std::seed_seq>(kCollisionS8N4B.begin(), kCollisionS8N4B.end())
      .generate(salted_b.begin(), salted_b.end());
  EXPECT_EQ(raw_a, raw_b);
  EXPECT_NE(salted_a, salted_b);
}

TEST(SaltedSeedSeq, SingleWordGenerateIsBijective) {
  if (!GetSaltMaterial().has_value()) {
    GTEST_SKIP() << "Salt material unavailable";
  }
  constexpr uint32_t kSamples = 10000;
  std::unordered_set<uint32_t> outputs;
  outputs.reserve(kSamples);
  for (uint32_t seed = 0; seed < kSamples; ++seed) {
    SaltedSeedSeq<std::seed_seq> seq({seed});
    uint32_t out = 0;
    seq.generate(&out, &out + 1);
    EXPECT_TRUE(outputs.insert(out).second) << "Collision at seed=" << seed;
  }
}

TEST(SaltedSeedSeq, DistinguishesZeroPaddedLengthsAndFull60WordSeeds) {
  if (!GetSaltMaterial().has_value()) {
    GTEST_SKIP() << "Salt material unavailable";
  }
  auto gen_n = [](std::initializer_list<uint32_t> init, size_t n) {
    SaltedSeedSeq<std::seed_seq> seq(init);
    std::vector<uint32_t> out(n, 0);
    seq.generate(out.begin(), out.end());
    return out;
  };

  EXPECT_NE(gen_n({}, 4), gen_n({0u}, 4));
  EXPECT_NE(gen_n({1u}, 4), gen_n({1u, 0u}, 4));
  EXPECT_NE(gen_n({}, 60), gen_n({0u}, 60));

  // Seeds differing only in word 59 (bit 1919) or word 63 (past 60 words)
  // must produce distinct 60-word outputs.
  std::vector<uint32_t> seed60_a(60, 0u);
  std::vector<uint32_t> seed60_b(60, 0u);
  seed60_b[59] = 1u;
  SaltedSeedSeq<std::seed_seq> seq60_a(seed60_a.begin(), seed60_a.end());
  SaltedSeedSeq<std::seed_seq> seq60_b(seed60_b.begin(), seed60_b.end());
  std::array<uint32_t, 60> out60_a{};
  std::array<uint32_t, 60> out60_b{};
  seq60_a.generate(out60_a.begin(), out60_a.end());
  seq60_b.generate(out60_b.begin(), out60_b.end());
  EXPECT_NE(out60_a, out60_b);

  std::vector<uint32_t> seed64_a(64, 0u);
  std::vector<uint32_t> seed64_b(64, 0u);
  seed64_b[63] = 1u;
  SaltedSeedSeq<std::seed_seq> seq64_a(seed64_a.begin(), seed64_a.end());
  SaltedSeedSeq<std::seed_seq> seq64_b(seed64_b.begin(), seed64_b.end());
  std::array<uint32_t, 60> out64_a{};
  std::array<uint32_t, 60> out64_b{};
  seq64_a.generate(out64_a.begin(), out64_a.end());
  seq64_b.generate(out64_b.begin(), out64_b.end());
  EXPECT_NE(out64_a, out64_b);
}

TEST(SaltedSeedSeq, CustomSeedSeqIsRequestedWithExactCountAndSalted) {
  static_assert(absl::random_internal::IsSaltedSeedSeq<
                SaltedSeedSeq<std::seed_seq>>::value);
  static_assert(absl::random_internal::IsSaltedSeedSeq<
                SaltedSeedSeq<CountingSeedSeq&>>::value);
  static_assert(
      !absl::random_internal::IsSaltedSeedSeq<CountingSeedSeq>::value);

  auto verify_exact_count = [](size_t reported_size, size_t requested) {
    std::vector<uint32_t> raw(requested, 0);
    CountingSeedSeq raw_seq(reported_size);
    raw_seq.generate(raw.begin(), raw.end());

    std::vector<uint32_t> salted(requested, 0);
    CountingSeedSeq seq(reported_size);
    SaltedSeedSeq<CountingSeedSeq&> salted_seq(seq);
    salted_seq.generate(salted.begin(), salted.end());
    EXPECT_EQ(seq.calls, 1u);
    EXPECT_EQ(seq.last_requested, requested);
    if (GetSaltMaterial().has_value()) {
      EXPECT_NE(salted, raw);
    }
  };

  verify_exact_count(0u, 4u);
  verify_exact_count(1u, 1u);
  verify_exact_count(4u, 4u);
  verify_exact_count(40u, 4u);
  verify_exact_count((std::numeric_limits<size_t>::max)(), 4u);
  verify_exact_count(4u, 8u);
}

template <typename SSeq, typename = void>
struct HasParamOut : std::false_type {};

template <typename SSeq>
struct HasParamOut<
    SSeq,
    std::void_t<decltype(std::declval<SSeq&>().param(
        std::declval<std::back_insert_iterator<std::vector<uint32_t>>>()))>>
    : std::true_type {};

struct MutableOnlyParamSizeSeq {
  using result_type = uint32_t;
  size_t size() { return 1u; }
  template <typename OutIt>
  void param(OutIt out) {
    *out++ = 42u;
  }
  template <typename OutIt>
  void generate(OutIt begin, OutIt end) {
    std::fill(begin, end, uint32_t{42u});
  }
};

struct DualConstParamSizeSeq {
  using result_type = uint32_t;
  size_t size() { return 1u; }
  size_t size() const { return 2u; }
  template <typename OutIt>
  void param(OutIt out) {
    *out++ = 10u;
  }
  template <typename OutIt>
  void param(OutIt out) const {
    *out++ = 20u;
    *out++ = 21u;
  }
  template <typename OutIt>
  void generate(OutIt begin, OutIt end) {
    std::fill(begin, end, uint32_t{7u});
  }
};

struct GrowingNoSizeParamSeq {
  using result_type = uint32_t;
  std::vector<uint32_t> words;
  GrowingNoSizeParamSeq() = default;
  template <typename It>
  GrowingNoSizeParamSeq(It begin, It end) : words(begin, end) {}
  template <typename OutIt>
  void param(OutIt out) const {
    for (uint32_t w : words) *out++ = w;
  }
  template <typename OutIt>
  void generate(OutIt begin, OutIt end) {
    std::fill(begin, end, uint32_t{1u});
  }
};

#ifdef ABSL_HAVE_EXCEPTIONS
struct ThrowingParamSeq {
  using result_type = uint32_t;
  ThrowingParamSeq() = default;
  template <typename It>
  ThrowingParamSeq(It, It) {}
  template <typename OutIt>
  void param(OutIt out) const {
    for (uint32_t i = 0; i < 12; ++i) *out++ = 0x5a5a0000u + i;
    throw 123;
  }
  template <typename OutIt>
  void generate(OutIt begin, OutIt end) {
    std::fill(begin, end, uint32_t{1u});
  }
};
#endif

TEST(SaltedSeedSeq, DeepConstnessOnSizeAndParam) {
  using absl::random_internal::HasSeedSeqSize;
  static_assert(HasSeedSeqSize<SaltedSeedSeq<MutableOnlyParamSizeSeq>>::value);
  static_assert(
      !HasSeedSeqSize<const SaltedSeedSeq<MutableOnlyParamSizeSeq>>::value);
  static_assert(!HasParamOut<SaltedSeedSeq<MutableOnlyParamSizeSeq>>::value);
  static_assert(
      !HasParamOut<const SaltedSeedSeq<MutableOnlyParamSizeSeq>>::value);

  SaltedSeedSeq<DualConstParamSizeSeq> mutable_seq;
  EXPECT_EQ(mutable_seq.size(), 1u);
  std::vector<uint32_t> mutable_params;
  mutable_seq.param(std::back_inserter(mutable_params));
  EXPECT_THAT(mutable_params, testing::ElementsAre(20u, 21u));

  const SaltedSeedSeq<DualConstParamSizeSeq> const_seq;
  EXPECT_EQ(const_seq.size(), 2u);
  std::vector<uint32_t> const_params;
  const_seq.param(std::back_inserter(const_params));
  EXPECT_THAT(const_params, testing::ElementsAre(20u, 21u));
}

TEST(SaltedSeedSeq, MakeSaltedSeedSeqUnwrapsConstOwningAndHandlesGrowth) {
  auto unwrapped = MakeSaltedSeedSeq(
      SaltedSeedSeq<const std::seed_seq>(kTestSeed.begin(), kTestSeed.end()));
  static_assert(
      std::is_same_v<decltype(unwrapped), SaltedSeedSeq<std::seed_seq>>);
  uint32_t out[4] = {};
  unwrapped.generate(std::begin(out), std::end(out));
  SaltedSeedSeq<std::seed_seq> expected_seq(kTestSeed.begin(), kTestSeed.end());
  uint32_t expected[4] = {};
  expected_seq.generate(std::begin(expected), std::end(expected));
  EXPECT_THAT(out, Pointwise(Eq(), expected));

  // Exceed inline capacity (8) without `size()` to exercise reallocation and
  // exit-time buffer cleansing.
  std::vector<uint32_t> wide_seed(20);
  for (size_t i = 0; i < wide_seed.size(); ++i) {
    wide_seed[i] = static_cast<uint32_t>(100 + i);
  }
  const GrowingNoSizeParamSeq growing(wide_seed.begin(), wide_seed.end());
  auto salted_growing = MakeSaltedSeedSeq(growing);
  std::vector<uint32_t> round_tripped;
  salted_growing.param(std::back_inserter(round_tripped));
  EXPECT_THAT(round_tripped, Pointwise(Eq(), wide_seed));

#ifdef ABSL_HAVE_EXCEPTIONS
  const ThrowingParamSeq throwing_seq;
  EXPECT_THROW((void)MakeSaltedSeedSeq(throwing_seq), int);
#endif
}

}  // namespace
