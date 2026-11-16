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

#include "absl/random/internal/nonsecure_base.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iterator>
#include <random>
#include <thread>  // NOLINT
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"

namespace {

using ExampleNonsecureURBG =
    absl::random_internal::NonsecureURBGBase<std::mt19937>;

template <typename T>
void Use(const T&) {}

}  // namespace

TEST(NonsecureURBGBase, DefaultConstructorIsValid) {
  ExampleNonsecureURBG urbg;
}

// Ensure that the recommended template-instantiations are valid.
TEST(RecommendedTemplates, CanBeConstructed) {
  absl::BitGen default_generator;
  absl::InsecureBitGen insecure_generator;
}

TEST(RecommendedTemplates, CanDiscardValues) {
  absl::BitGen default_generator;
  absl::InsecureBitGen insecure_generator;

  default_generator.discard(5);
  insecure_generator.discard(5);
}

TEST(NonsecureURBGBase, StandardInterface) {
  // Names after definition of [rand.req.urbg] in C++ standard.
  // e us a value of E
  // v is a lvalue of E
  // x, y are possibly const values of E
  // s is a value of T
  // q is a value satisfying requirements of seed_sequence
  // z is a value of type unsigned long long
  // os is a some specialization of basic_ostream
  // is is a some specialization of basic_istream

  using E = absl::random_internal::NonsecureURBGBase<std::minstd_rand>;

  using T = typename E::result_type;

  static_assert(!std::is_copy_constructible_v<E>,
                "NonsecureURBGBase should not be copy constructible");
  static_assert(!std::is_constructible_v<E, E&>,
                "NonsecureURBGBase should not be constructible from non-const "
                "lvalue reference");
  static_assert(!std::is_constructible_v<E, int>,
                "NonsecureURBGBase should not be constructible from int");
  static_assert(!std::is_constructible_v<E, T>,
                "NonsecureURBGBase should not be constructible from "
                "result_type");
  static_assert(!std::is_constructible_v<absl::BitGen, int>,
                "BitGen should not be constructible from int");
  static_assert(!std::is_constructible_v<absl::BitGen, absl::BitGen&>,
                "BitGen should not be constructible from BitGen&");

  static_assert(!std::is_copy_assignable_v<E>,
                "NonsecureURBGBase should not be copy assignable");

  static_assert(std::is_move_constructible_v<E>,
                "NonsecureURBGBase should be move constructible");

  static_assert(std::is_move_assignable_v<E>,
                "NonsecureURBGBase should be move assignable");

  static_assert(std::is_same_v<decltype(std::declval<E>()()), T>,
                "return type of operator() must be result_type");

  {
    const E x, y;
    Use(x);
    Use(y);

    static_assert(std::is_same_v<decltype(x == y), bool>,
                  "return type of operator== must be bool");

    static_assert(std::is_same_v<decltype(x != y), bool>,
                  "return type of operator== must be bool");
  }

  E e;
  std::seed_seq q{1, 2, 3};

  E{};
  E{q};

  // Copy constructor not supported.
  // E{x};

  // result_type seed constructor not supported.
  // E{T{1}};

  // Move constructors are supported.
  {
    E tmp(q);
    E m = std::move(tmp);
    E n(std::move(m));
    EXPECT_TRUE(e != n);
  }

  // Comparisons work.
  {
    // MSVC emits error 2718 when using EXPECT_EQ(e, x)
    //  * actual parameter with __declspec(align('#')) won't be aligned
    E a(q);
    E b(q);

    EXPECT_TRUE(a != e);
    EXPECT_TRUE(a == b);

    a();
    EXPECT_TRUE(a != b);
  }

  // e.seed(s) not supported.

  // [rand.req.eng] specifies the parameter as 'unsigned long long'
  // e.discard(unsigned long long) is supported.
  unsigned long long z = 1;  // NOLINT(runtime/int)
  e.discard(z);
}

TEST(NonsecureURBGBase, SeedSeqConstructorIsValid) {
  std::seed_seq seq;
  ExampleNonsecureURBG rbg(seq);
}

TEST(NonsecureURBGBase, CompatibleWithDistributionUtils) {
  ExampleNonsecureURBG rbg;

  absl::Uniform(rbg, 0, 100);
  absl::Uniform(rbg, 0.5, 0.7);
  absl::Poisson<uint32_t>(rbg);
  absl::Exponential<float>(rbg);
}

TEST(NonsecureURBGBase, CompatibleWithStdDistributions) {
  ExampleNonsecureURBG rbg;

  // Cast to void to suppress [[nodiscard]] warnings
  static_cast<void>(std::uniform_int_distribution<uint32_t>(0, 100)(rbg));
  static_cast<void>(std::uniform_real_distribution<float>()(rbg));
  static_cast<void>(std::bernoulli_distribution(0.2)(rbg));
}

TEST(NonsecureURBGBase, ConsecutiveDefaultInstancesYieldUniqueVariates) {
  const size_t kNumSamples = 128;

  ExampleNonsecureURBG rbg1;
  ExampleNonsecureURBG rbg2;

  for (size_t i = 0; i < kNumSamples; i++) {
    EXPECT_NE(rbg1(), rbg2());
  }
}

TEST(NonsecureURBGBase, EqualSeedSequencesYieldEqualVariates) {
  std::seed_seq seq;

  ExampleNonsecureURBG rbg1(seq);
  ExampleNonsecureURBG rbg2(seq);

  // ExampleNonsecureURBG rbg3({1, 2, 3});  // Should not compile.

  for (uint32_t i = 0; i < 1000; i++) {
    EXPECT_EQ(rbg1(), rbg2());
  }

  rbg1.discard(100);
  rbg2.discard(100);

  // The sequences should continue after discarding
  for (uint32_t i = 0; i < 1000; i++) {
    EXPECT_EQ(rbg1(), rbg2());
  }
}

TEST(NonsecureURBGBase, DistinctSequencesPerThread) {
  constexpr int kNumThreads = 12;
  constexpr size_t kValuesPerThread = 32;
  using result_type = absl::BitGen::result_type;

  // Acquire initial sequences from multiple threads.
  std::vector<std::vector<result_type>> data;
  {
    absl::Mutex mu;
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; i++) {
      threads.emplace_back([&]() {
        absl::BitGen gen;

        std::vector<result_type> v(kValuesPerThread);
        absl::c_generate(v, [&]() { return gen(); });
        absl::MutexLock l(mu);
        data.push_back(std::move(v));
      });
    }
    for (auto& t : threads) t.join();
  }

  EXPECT_EQ(data.size(), kNumThreads);

  // There should be essentially no duplicates in the sequences.
  size_t expected_size = 0;
  absl::flat_hash_set<result_type> seen;
  for (const auto& v : data) {
    expected_size += v.size();
    for (result_type x : v) seen.insert(x);
  }
  EXPECT_GE(seen.size(), expected_size - 1);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForU32) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  uint32_t state[2] = {0, 0};
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForU64) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  uint64_t state[2] = {0, 0};
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
  EXPECT_FALSE((state[0] >> 32) == 0 && (state[1] >> 32) == 0);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForS32) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  int32_t state[2] = {0, 0};
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForVector) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  std::vector<uint32_t> state(2);
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
}

TEST(RandenPoolSeedSeqTest, SizeIsConstAndZero) {
  const absl::random_internal::RandenPoolSeedSeq const_seeder;
  EXPECT_EQ(const_seeder.size(), 0u);
}

namespace {
struct AssignableSeedSink {
  static constexpr uint64_t kHeadCanary = 0x1122334455667788ull;
  static constexpr uint64_t kTailCanary = 0x99aabbccddeeff00ull;

  uint64_t head_canary = kHeadCanary;
  uint32_t value = 0;
  int assignments = 0;
  uint64_t tail_canary = kTailCanary;

  AssignableSeedSink& operator=(uint32_t v) {
    value = v;
    ++assignments;
    return *this;
  }
};
}  // namespace

TEST(RandenPoolSeedSeqTest, ContiguousIteratorTraitGuardsInvalidTypes) {
  using absl::random_internal::IsRandenPoolContiguousIterator;

  static_assert(IsRandenPoolContiguousIterator<uint8_t*>::value);
  static_assert(IsRandenPoolContiguousIterator<uint16_t*>::value);
  static_assert(IsRandenPoolContiguousIterator<uint32_t*>::value);
  static_assert(IsRandenPoolContiguousIterator<uint64_t*>::value);
  static_assert(IsRandenPoolContiguousIterator<int32_t*>::value);
  static_assert(IsRandenPoolContiguousIterator<int64_t*>::value);
  static_assert(
      IsRandenPoolContiguousIterator<std::vector<uint32_t>::iterator>::value);
  static_assert(
      IsRandenPoolContiguousIterator<std::vector<uint64_t>::iterator>::value);
  static_assert(
      IsRandenPoolContiguousIterator<std::vector<int32_t>::iterator>::value);

  // Exclude bool (trap representations), non-integral types (arbitrary object
  // representations), const/volatile pointees, non-contiguous iterators, and
  // non-iterators.
  static_assert(!IsRandenPoolContiguousIterator<bool*>::value);
  static_assert(!IsRandenPoolContiguousIterator<const bool*>::value);
  static_assert(
      !IsRandenPoolContiguousIterator<std::vector<bool>::iterator>::value);
  static_assert(!IsRandenPoolContiguousIterator<const uint32_t*>::value);
  static_assert(!IsRandenPoolContiguousIterator<volatile uint32_t*>::value);
  static_assert(!IsRandenPoolContiguousIterator<
                std::vector<uint32_t>::const_iterator>::value);
  static_assert(!IsRandenPoolContiguousIterator<float*>::value);
  static_assert(!IsRandenPoolContiguousIterator<double*>::value);
  static_assert(
      !IsRandenPoolContiguousIterator<std::vector<double>::iterator>::value);
  static_assert(!IsRandenPoolContiguousIterator<AssignableSeedSink*>::value);
  static_assert(!IsRandenPoolContiguousIterator<
                std::vector<AssignableSeedSink>::iterator>::value);
  static_assert(
      !IsRandenPoolContiguousIterator<std::deque<uint32_t>::iterator>::value);
  static_assert(!IsRandenPoolContiguousIterator<void*>::value);
  static_assert(!IsRandenPoolContiguousIterator<const void*>::value);
  static_assert(!IsRandenPoolContiguousIterator<void (*)()>::value);
  static_assert(!IsRandenPoolContiguousIterator<int>::value);

  static_assert(absl::random_internal::IsDirectEntropySeedSeq<
                absl::random_internal::RandenPoolSeedSeq>::value);
  static_assert(
      !absl::random_internal::IsDirectEntropySeedSeq<std::seed_seq>::value);
}

TEST(RandenPoolSeedSeqTest, SeederAvoidsTrapRepresentationsOnBool) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  bool bool_array[64] = {};
  seeder.generate(std::begin(bool_array), std::end(bool_array));
  int true_count = 0;
  for (const bool& b : bool_array) {
    unsigned char raw = 0xff;
    static_assert(sizeof(bool) == sizeof(unsigned char));
    std::memcpy(&raw, &b, sizeof(raw));
    EXPECT_LE(raw, 1u);
    if (b) ++true_count;
  }
  EXPECT_GT(true_count, 0);

  std::vector<bool> bool_vec(16, false);
  seeder.generate(bool_vec.begin(), bool_vec.end());
  EXPECT_THAT(bool_vec, testing::Contains(true));
}

TEST(RandenPoolSeedSeqTest, SeederUsesAssignmentForNonIntegralAndBufferPaths) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  // Size 12 exceeds InlinedVector<uint32_t, 8>'s inline capacity to exercise
  // heap-backed BufferTag generation and scrubbing as well.
  AssignableSeedSink sink_array[12];
  seeder.generate(std::begin(sink_array), std::end(sink_array));
  bool any_nonzero = false;
  for (const auto& s : sink_array) {
    EXPECT_EQ(s.head_canary, AssignableSeedSink::kHeadCanary);
    EXPECT_EQ(s.tail_canary, AssignableSeedSink::kTailCanary);
    EXPECT_EQ(s.assignments, 1);
    any_nonzero = any_nonzero || (s.value != 0);
  }
  EXPECT_TRUE(any_nonzero);

  std::vector<AssignableSeedSink> sink_vec(6);
  seeder.generate(sink_vec.begin(), sink_vec.end());
  for (const auto& s : sink_vec) {
    EXPECT_EQ(s.head_canary, AssignableSeedSink::kHeadCanary);
    EXPECT_EQ(s.tail_canary, AssignableSeedSink::kTailCanary);
    EXPECT_EQ(s.assignments, 1);
  }

  double fp_array[8] = {};
  seeder.generate(std::begin(fp_array), std::end(fp_array));
  for (double d : fp_array) {
    EXPECT_TRUE(std::isfinite(d));
    EXPECT_GE(d, 0.0);
    EXPECT_LE(d, static_cast<double>(UINT32_MAX));
  }

  volatile uint32_t vol_array[4] = {};
  seeder.generate(std::begin(vol_array), std::end(vol_array));
  EXPECT_FALSE(vol_array[0] == 0 && vol_array[1] == 0 && vol_array[2] == 0 &&
               vol_array[3] == 0);

  std::deque<uint32_t> dq(12, 0);
  seeder.generate(dq.begin(), dq.end());
  EXPECT_THAT(dq, testing::Contains(testing::Ne(0u)));
}

TEST(NonsecureURBGBase, LvalueAndRvalueSaltedSeedSeqMatch) {
  absl::SeedSeq s1{1, 2, 3, 4};
  absl::SeedSeq s2{1, 2, 3, 4};

  absl::BitGen gen_lvalue(s1);
  absl::BitGen gen_rvalue(std::move(s2));
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(gen_lvalue(), gen_rvalue());
  }
}

namespace {
template <typename URBG>
std::array<typename URBG::result_type, 16> GenerateSequence(URBG& gen) {
  std::array<typename URBG::result_type, 16> out;
  for (auto& value : out) {
    value = gen();
  }
  return out;
}

template <typename ResultT, bool kConstGenerate = true, uint32_t kStart = 1,
          bool kConstSize = true, bool kHasSize = true>
struct TestSeedSeq {
  using result_type = ResultT;
  size_t count = 4;
  uint32_t start = kStart;

  explicit TestSeedSeq(size_t n = 4) : count(n) {}

  template <bool H = kHasSize, bool C = kConstSize,
            std::enable_if_t<H && C, int> = 0>
  size_t size() const {
    return count;
  }
  template <bool H = kHasSize, bool C = kConstSize,
            std::enable_if_t<H && !C, int> = 0>
  size_t size() {
    return count;
  }

  template <typename OutIt, bool C = kConstGenerate,
            std::enable_if_t<C, int> = 0>
  void generate(OutIt begin, OutIt end) const {
    uint32_t v = start;
    for (OutIt it = begin; it != end; ++it) *it = v++;
  }
  template <typename OutIt, bool C = kConstGenerate,
            std::enable_if_t<!C, int> = 0>
  void generate(OutIt begin, OutIt end) {
    uint32_t v = start;
    for (OutIt it = begin; it != end; ++it) *it = v++;
  }
};

struct NoResultTypeSeedSeq {
  size_t size() const { return 2; }
  template <typename OutIt>
  void generate(OutIt begin, OutIt end) const {
    uint32_t v = 1;
    for (OutIt it = begin; it != end; ++it) *it = v++;
  }
};

template <bool kConstGenerate>
struct ZeroSizeCounterSeedSeq {
  using result_type = uint32_t;
  mutable uint32_t counter = 1;
  size_t size() const { return 0; }
  template <typename OutIt>
  void param(OutIt) const {}
  template <typename OutIt, bool C = kConstGenerate,
            std::enable_if_t<C, int> = 0>
  void generate(OutIt begin, OutIt end) const {
    for (OutIt it = begin; it != end; ++it) *it = counter++;
  }
  template <typename OutIt, bool C = kConstGenerate,
            std::enable_if_t<!C, int> = 0>
  void generate(OutIt begin, OutIt end) {
    for (OutIt it = begin; it != end; ++it) *it = counter++;
  }
};

template <bool kConstGenerate, bool kHasSize>
struct TestParamSeedSeq
    : TestSeedSeq<uint32_t, kConstGenerate, 1, /*kConstSize=*/false, kHasSize> {
  std::vector<uint32_t> values;
  TestParamSeedSeq() = default;
  template <typename It>
  TestParamSeedSeq(It begin, It end) : values(begin, end) {
    this->count = values.size();
    this->start = values.empty() ? 1u : values[0];
  }
  template <typename OutIt>
  void param(OutIt out) const {
    std::copy(values.begin(), values.end(), out);
  }
};

using StatefulZeroSizeSeedSeq = ZeroSizeCounterSeedSeq<false>;
using ConstGenerateZeroSizeSeedSeq = ZeroSizeCounterSeedSeq<true>;
using SeedSeq64 = TestSeedSeq<uint64_t, /*kConstGenerate=*/false, 0xdeadbeefu>;
using SeedSeq32 = TestSeedSeq<uint32_t, /*kConstGenerate=*/false, 0xdeadbeefu>;
using SignedResultTypeSeedSeq = TestSeedSeq<int32_t>;
using NarrowResultTypeSeedSeq = TestSeedSeq<uint16_t>;
using VoidResultTypeSeedSeq = TestSeedSeq<void>;
using ConstGenerateNonConstSizeSeedSeq =
    TestSeedSeq<uint32_t, /*kConstGenerate=*/true, 1, /*kConstSize=*/false>;
using ConstGenerateNoSizeSeedSeq =
    TestSeedSeq<uint32_t, /*kConstGenerate=*/true, 1, /*kConstSize=*/true,
                /*kHasSize=*/false>;
using ConstGenerateNonConstSizeWithParamSeedSeq = TestParamSeedSeq<true, true>;
using ConstParamNoSizeSeedSeq = TestParamSeedSeq<false, false>;
}  // namespace

TEST(NonsecureURBGBase, SeedSeqConstructibilityTraits) {
  static_assert(std::is_constructible_v<absl::BitGen, absl::SeedSeq&>);
  static_assert(std::is_constructible_v<absl::BitGen, absl::SeedSeq&&>);
  static_assert(std::is_constructible_v<absl::BitGen, const absl::SeedSeq&>);
  static_assert(std::is_constructible_v<absl::BitGen, const absl::SeedSeq&&>);
  static_assert(std::is_constructible_v<absl::BitGen, std::seed_seq&>);
  static_assert(std::is_constructible_v<absl::BitGen, std::seed_seq&&>);
  static_assert(std::is_constructible_v<absl::BitGen, const std::seed_seq&>);
  static_assert(std::is_constructible_v<absl::BitGen, const std::seed_seq&&>);
  static_assert(
      std::is_constructible_v<absl::BitGen, StatefulZeroSizeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, const StatefulZeroSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::BitGen,
                                        const ConstGenerateZeroSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::BitGen, SeedSeq64&>);
  static_assert(std::is_constructible_v<absl::BitGen, SeedSeq64&&>);
  static_assert(!std::is_constructible_v<absl::BitGen, NoResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, const NoResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, SignedResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, NarrowResultTypeSeedSeq&>);
  static_assert(!std::is_constructible_v<absl::BitGen, VoidResultTypeSeedSeq&>);
  static_assert(
      std::is_constructible_v<absl::BitGen, ConstGenerateNonConstSizeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen,
                               const ConstGenerateNonConstSizeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, ConstGenerateNoSizeSeedSeq&>);
  static_assert(!std::is_constructible_v<absl::BitGen,
                                         const ConstGenerateNoSizeSeedSeq&>);
  static_assert(
      std::is_constructible_v<
          absl::BitGen, const ConstGenerateNonConstSizeWithParamSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, const ConstParamNoSizeSeedSeq&>);

  static_assert(
      std::is_constructible_v<absl::InsecureBitGen, const absl::SeedSeq&>);
  static_assert(
      std::is_constructible_v<absl::InsecureBitGen, const std::seed_seq&>);
  static_assert(!std::is_constructible_v<absl::InsecureBitGen,
                                         const StatefulZeroSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::InsecureBitGen,
                                        const ConstGenerateZeroSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::InsecureBitGen, SeedSeq64&>);
  static_assert(
      !std::is_constructible_v<absl::InsecureBitGen, NoResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::InsecureBitGen, SignedResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::InsecureBitGen, NarrowResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::InsecureBitGen, VoidResultTypeSeedSeq&>);
  static_assert(
      std::is_constructible_v<absl::InsecureBitGen,
                              const ConstGenerateNonConstSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::InsecureBitGen,
                                        const ConstGenerateNoSizeSeedSeq&>);
  static_assert(std::is_constructible_v<
                absl::InsecureBitGen,
                const ConstGenerateNonConstSizeWithParamSeedSeq&>);
  static_assert(std::is_constructible_v<absl::InsecureBitGen,
                                        const ConstParamNoSizeSeedSeq&>);

  static_assert(std::is_constructible_v<ExampleNonsecureURBG, SeedSeq64&>);
  static_assert(
      !std::is_constructible_v<ExampleNonsecureURBG, NoResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<ExampleNonsecureURBG, SignedResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<ExampleNonsecureURBG, NarrowResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<ExampleNonsecureURBG, VoidResultTypeSeedSeq&>);
  static_assert(
      std::is_constructible_v<ExampleNonsecureURBG,
                              const ConstGenerateNonConstSizeSeedSeq&>);
  static_assert(std::is_constructible_v<ExampleNonsecureURBG,
                                        const ConstGenerateNoSizeSeedSeq&>);
}

TEST(NonsecureURBGBase, ConstSaltedSeedSeqMatchesMutable) {
  absl::SeedSeq mutable_lvalue{1, 2, 3, 4};
  absl::SeedSeq mutable_rvalue{1, 2, 3, 4};
  const absl::SeedSeq const_lvalue{1, 2, 3, 4};
  const absl::SeedSeq const_rvalue{1, 2, 3, 4};

  absl::BitGen bitgen_mutable_lvalue(mutable_lvalue);
  absl::BitGen bitgen_mutable_rvalue(std::move(mutable_rvalue));
  absl::BitGen bitgen_const_lvalue(const_lvalue);
  absl::BitGen bitgen_const_rvalue(std::move(const_rvalue));
  const auto expected_bitgen = GenerateSequence(bitgen_mutable_lvalue);
  EXPECT_THAT(GenerateSequence(bitgen_mutable_rvalue),
              testing::Eq(expected_bitgen));
  EXPECT_THAT(GenerateSequence(bitgen_const_lvalue),
              testing::Eq(expected_bitgen));
  EXPECT_THAT(GenerateSequence(bitgen_const_rvalue),
              testing::Eq(expected_bitgen));

  absl::InsecureBitGen insecure_mutable(mutable_lvalue);
  absl::InsecureBitGen insecure_const(const_lvalue);
  EXPECT_THAT(GenerateSequence(insecure_const),
              testing::Eq(GenerateSequence(insecure_mutable)));
}

TEST(NonsecureURBGBase, ConstStdSeedSeqMatchesMutable) {
  std::seed_seq mutable_lvalue{1, 2, 3, 4};
  std::seed_seq mutable_rvalue{1, 2, 3, 4};
  const std::seed_seq const_lvalue{1, 2, 3, 4};
  const std::seed_seq const_rvalue{1, 2, 3, 4};

  absl::BitGen bitgen_mutable_lvalue(mutable_lvalue);
  absl::BitGen bitgen_mutable_rvalue(std::move(mutable_rvalue));
  absl::BitGen bitgen_const_lvalue(const_lvalue);
  absl::BitGen bitgen_const_rvalue(std::move(const_rvalue));
  const auto expected_bitgen = GenerateSequence(bitgen_mutable_lvalue);
  EXPECT_THAT(GenerateSequence(bitgen_mutable_rvalue),
              testing::Eq(expected_bitgen));
  EXPECT_THAT(GenerateSequence(bitgen_const_lvalue),
              testing::Eq(expected_bitgen));
  EXPECT_THAT(GenerateSequence(bitgen_const_rvalue),
              testing::Eq(expected_bitgen));

  absl::InsecureBitGen insecure_mutable(mutable_lvalue);
  absl::InsecureBitGen insecure_const(const_lvalue);
  EXPECT_THAT(GenerateSequence(insecure_const),
              testing::Eq(GenerateSequence(insecure_mutable)));
}

TEST(NonsecureURBGBase, StatefulZeroSizeSeedSeqPreservesEntropy) {
  StatefulZeroSizeSeedSeq seeder;
  absl::BitGen gen1(seeder);
  absl::BitGen gen2(seeder);
  EXPECT_THAT(GenerateSequence(gen1), testing::Ne(GenerateSequence(gen2)));
}

TEST(NonsecureURBGBase, ConstGenerateZeroSizeSeedSeqPreservesEntropy) {
  const ConstGenerateZeroSizeSeedSeq seeder;
  absl::BitGen gen1(seeder);
  absl::BitGen gen2(seeder);
  EXPECT_THAT(GenerateSequence(gen1), testing::Ne(GenerateSequence(gen2)));
}

TEST(NonsecureURBGBase, SeedSequenceWith64BitResultTypeMatches32Bit) {
  SeedSeq64 seq64;
  SeedSeq32 seq32;

  absl::BitGen bitgen64(seq64);
  absl::BitGen bitgen32(seq32);
  EXPECT_THAT(GenerateSequence(bitgen64),
              testing::Eq(GenerateSequence(bitgen32)));

  absl::InsecureBitGen insecure64(seq64);
  absl::InsecureBitGen insecure32(seq32);
  EXPECT_THAT(GenerateSequence(insecure64),
              testing::Eq(GenerateSequence(insecure32)));

  ExampleNonsecureURBG example64(seq64);
  ExampleNonsecureURBG example32(seq32);
  EXPECT_THAT(GenerateSequence(example64),
              testing::Eq(GenerateSequence(example32)));
}

TEST(NonsecureURBGBase, ConstFallbackViaParamMatchesDirectSeeding) {
  std::array<uint32_t, 3> init = {10, 20, 30};
  ConstGenerateNonConstSizeWithParamSeedSeq mutable_with_param(init.begin(),
                                                               init.end());
  const ConstGenerateNonConstSizeWithParamSeedSeq const_with_param(init.begin(),
                                                                   init.end());
  absl::BitGen bitgen_from_mutable_param(mutable_with_param);
  absl::BitGen bitgen_from_const_param(const_with_param);
  EXPECT_THAT(GenerateSequence(bitgen_from_const_param),
              testing::Eq(GenerateSequence(bitgen_from_mutable_param)));
}

TEST(NonsecureURBGBase, NonConstSizeAndNoSizeSeedSeqs) {
  ConstGenerateNonConstSizeSeedSeq mutable_non_const_size;
  absl::BitGen bitgen_mutable(mutable_non_const_size);
  EXPECT_THAT(GenerateSequence(bitgen_mutable),
              testing::Ne(std::array<absl::BitGen::result_type, 16>{}));

  const ConstGenerateNonConstSizeSeedSeq const_non_const_size;
  absl::InsecureBitGen insecure_non_const_size(const_non_const_size);
  ExampleNonsecureURBG example_non_const_size(const_non_const_size);
  EXPECT_THAT(GenerateSequence(insecure_non_const_size),
              testing::Ne(std::array<absl::InsecureBitGen::result_type, 16>{}));
  EXPECT_THAT(GenerateSequence(example_non_const_size),
              testing::Ne(std::array<ExampleNonsecureURBG::result_type, 16>{}));

  const ConstGenerateNoSizeSeedSeq const_no_size;
  absl::InsecureBitGen insecure_no_size(const_no_size);
  ExampleNonsecureURBG example_no_size(const_no_size);
  EXPECT_THAT(GenerateSequence(insecure_no_size),
              testing::Ne(std::array<absl::InsecureBitGen::result_type, 16>{}));
  EXPECT_THAT(GenerateSequence(example_no_size),
              testing::Ne(std::array<ExampleNonsecureURBG::result_type, 16>{}));

  std::array<uint32_t, 3> init = {10, 20, 30};
  const ConstParamNoSizeSeedSeq const_param_no_size(init.begin(), init.end());
  absl::InsecureBitGen insecure_param_no_size(const_param_no_size);
  EXPECT_THAT(GenerateSequence(insecure_param_no_size),
              testing::Ne(std::array<absl::InsecureBitGen::result_type, 16>{}));
}

TEST(NonsecureURBGBase, SaltedSeedingSeparatesStdSeedSeqCollisionTuples) {
  if (!absl::random_internal::GetSaltMaterial().has_value()) {
    return;
  }

  // s = 2 collision pair at n = 2 (affects BitGen(s=2) and pcg32(SeedSeq)).
  constexpr uint32_t kS2A[2] = {0x00000003u, 0xef243a0au};
  constexpr uint32_t kS2B[2] = {0x0b8bb54eu, 0xfaafef55u};
  {
    absl::SeedSeq salted_a(std::begin(kS2A), std::end(kS2A));
    absl::SeedSeq salted_b(std::begin(kS2B), std::end(kS2B));
    absl::random_internal::pcg32_2018_engine pcg_a(salted_a);
    absl::random_internal::pcg32_2018_engine pcg_b(salted_b);
    EXPECT_THAT(GenerateSequence(pcg_a), testing::Ne(GenerateSequence(pcg_b)));

    absl::BitGen bitgen_a(salted_a);
    absl::BitGen bitgen_b(salted_b);
    EXPECT_THAT(GenerateSequence(bitgen_a),
                testing::Ne(GenerateSequence(bitgen_b)));

    std::seed_seq std_a(std::begin(kS2A), std::end(kS2A));
    std::seed_seq std_b(std::begin(kS2B), std::end(kS2B));
    absl::BitGen bitgen_std_a(std_a);
    absl::BitGen bitgen_std_b(std_b);
    EXPECT_THAT(GenerateSequence(bitgen_std_a),
                testing::Ne(GenerateSequence(bitgen_std_b)));
  }

  // s = 3 collision pair at n = 3 (affects BitGen(s=3)).
  constexpr uint32_t kS3A[3] = {0x0000269du, 0x4cc2d2d4u, 0x507fd03cu};
  constexpr uint32_t kS3B[3] = {0x00033c9bu, 0xa08a4e6cu, 0x5082e63au};
  {
    std::seed_seq std_a(std::begin(kS3A), std::end(kS3A));
    std::seed_seq std_b(std::begin(kS3B), std::end(kS3B));
    absl::BitGen bitgen_a(std_a);
    absl::BitGen bitgen_b(std_b);
    EXPECT_THAT(GenerateSequence(bitgen_a),
                testing::Ne(GenerateSequence(bitgen_b)));
  }

  // s = 4 collision pair at n = 4 (affects InsecureBitGen and BitGen(s=4)).
  constexpr uint32_t kS4A[4] = {0x00010253u, 0x4c1127d0u, 0xc406ad4cu,
                                0x13a2704du};
  constexpr uint32_t kS4B[4] = {0x000135d7u, 0xc89ecda0u, 0x24996d88u,
                                0x13a2a3d1u};
  {
    absl::SeedSeq salted_a(std::begin(kS4A), std::end(kS4A));
    absl::SeedSeq salted_b(std::begin(kS4B), std::end(kS4B));
    absl::InsecureBitGen insecure_a(salted_a);
    absl::InsecureBitGen insecure_b(salted_b);
    EXPECT_THAT(GenerateSequence(insecure_a),
                testing::Ne(GenerateSequence(insecure_b)));

    absl::BitGen bitgen_a(salted_a);
    absl::BitGen bitgen_b(salted_b);
    EXPECT_THAT(GenerateSequence(bitgen_a),
                testing::Ne(GenerateSequence(bitgen_b)));
  }

  // s = 8 collision pairs at n = 8 (BitGen) and n = 4 (InsecureBitGen).
  constexpr uint32_t kS8N8A[8] = {0x7496df94u, 0x2f701878u, 0xf374430bu,
                                  0x33e4d291u, 0x8e5b51a3u, 0x0f511665u,
                                  0xd44a6ec8u, 0xbb00fd0au};
  constexpr uint32_t kS8N8B[8] = {0x64e949bau, 0x47fb3261u, 0xdeaae914u,
                                  0xfdd32ba0u, 0xa4d1fd8du, 0x357706e1u,
                                  0xc49cd8eeu, 0xbb01dbdfu};
  {
    std::seed_seq std_a(std::begin(kS8N8A), std::end(kS8N8A));
    std::seed_seq std_b(std::begin(kS8N8B), std::end(kS8N8B));
    absl::BitGen bitgen_a(std_a);
    absl::BitGen bitgen_b(std_b);
    EXPECT_THAT(GenerateSequence(bitgen_a),
                testing::Ne(GenerateSequence(bitgen_b)));
  }

  constexpr uint32_t kS8N4A[8] = {0x00000001u, 0x00000002u, 0x00000003u,
                                  0x00000004u, 0x0000bf91u, 0x67c0a593u,
                                  0x313389e7u, 0xee5c8e5cu};
  constexpr uint32_t kS8N4B[8] = {0x00000001u, 0x00000002u, 0x00000003u,
                                  0x00000004u, 0x00011835u, 0xa5168553u,
                                  0xcee80a7au, 0xee5ce700u};
  {
    absl::SeedSeq salted_a(std::begin(kS8N4A), std::end(kS8N4A));
    absl::SeedSeq salted_b(std::begin(kS8N4B), std::end(kS8N4B));
    absl::InsecureBitGen insecure_a(salted_a);
    absl::InsecureBitGen insecure_b(salted_b);
    EXPECT_THAT(GenerateSequence(insecure_a),
                testing::Ne(GenerateSequence(insecure_b)));
  }
}
