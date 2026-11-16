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
#include <functional>
#include <ios>
#include <iterator>
#include <ostream>
#include <random>
#include <set>
#include <thread>  // NOLINT
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/random/distributions.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/seed_material.h"
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
// Arbitrary nothing-up-my-sleeve test seed constants.
constexpr std::array<uint32_t, 4> kTestSeedWords = {1, 2, 3, 4};
constexpr std::array<uint32_t, 3> kParamSeedWords = {10, 20, 30};
constexpr uint32_t kTestSeedStart = 0xdeadbeefu;

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

void PrintTo(const AssignableSeedSink& s, std::ostream* os) {
  *os << "{head_canary=" << std::hex << s.head_canary
      << ", tail_canary=" << s.tail_canary << std::dec
      << ", assignments=" << s.assignments << ", value=" << s.value << "}";
}

MATCHER(HasValidBoolRepresentation, "has raw byte representation 0 or 1") {
  unsigned char raw = 0xff;
  static_assert(sizeof(bool) == sizeof(unsigned char));
  std::memcpy(&raw, &arg, sizeof(raw));
  return raw <= 1u;
}

MATCHER(IsAssignedOnceKeepingCanaries,
        "is assigned once without overwriting surrounding canary fields") {
  return arg.head_canary == AssignableSeedSink::kHeadCanary &&
         arg.tail_canary == AssignableSeedSink::kTailCanary &&
         arg.assignments == 1;
}

MATCHER(IsFiniteInUint32Range, "is finite and in [0, UINT32_MAX]") {
  return std::isfinite(arg) && arg >= 0.0 &&
         arg <= static_cast<double>(UINT32_MAX);
}
}  // namespace

TEST(RandenPoolSeedSeqTest, BoolArrayGenerateWritesValidZeroOrOneBytes) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  bool bool_array[64] = {};
  seeder.generate(std::begin(bool_array), std::end(bool_array));
  EXPECT_THAT(bool_array, testing::Each(HasValidBoolRepresentation()));
  EXPECT_THAT(bool_array, testing::Contains(true));
}

TEST(RandenPoolSeedSeqTest, VectorBoolGeneratePopulatesElements) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  std::vector<bool> bool_vec(16, false);
  seeder.generate(bool_vec.begin(), bool_vec.end());
  EXPECT_THAT(bool_vec, testing::Contains(true));
}

TEST(RandenPoolSeedSeqTest, NarrowIntegralArraysAndDequeGenerate) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  uint8_t bytes[16] = {};
  seeder.generate(std::begin(bytes), std::end(bytes));
  EXPECT_THAT(bytes, testing::Contains(testing::Ne(uint8_t{0})));

  uint16_t shorts[16] = {};
  seeder.generate(std::begin(shorts), std::end(shorts));
  EXPECT_THAT(shorts, testing::Contains(testing::Ne(uint16_t{0})));

  std::deque<uint32_t> dq(16, 0u);
  seeder.generate(dq.begin(), dq.end());
  EXPECT_THAT(dq, testing::Contains(testing::Ne(uint32_t{0})));
}

namespace {
class VolatileValueTypeIterator {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using value_type = volatile uint32_t;
  using difference_type = ptrdiff_t;
  using pointer = volatile uint32_t*;
  using reference = volatile uint32_t&;

  explicit VolatileValueTypeIterator(volatile uint32_t* p) : p_(p) {}

  reference operator*() const { return *p_; }
  VolatileValueTypeIterator& operator++() {
    ++p_;
    return *this;
  }
  VolatileValueTypeIterator& operator+=(difference_type n) {
    p_ += n;
    return *this;
  }
  friend difference_type operator-(VolatileValueTypeIterator a,
                                   VolatileValueTypeIterator b) {
    return a.p_ - b.p_;
  }
  friend bool operator==(VolatileValueTypeIterator a,
                         VolatileValueTypeIterator b) {
    return a.p_ == b.p_;
  }
  friend bool operator!=(VolatileValueTypeIterator a,
                         VolatileValueTypeIterator b) {
    return a.p_ != b.p_;
  }

 private:
  volatile uint32_t* p_;
};
}  // namespace

TEST(RandenPoolSeedSeqTest, VolatileValueTypeCustomIteratorGenerate) {
  absl::random_internal::RandenPoolSeedSeq seeder;
  volatile uint32_t buf[8] = {};
  seeder.generate(VolatileValueTypeIterator(buf),
                  VolatileValueTypeIterator(buf + 8));
  std::array<uint32_t, 8> copy{};
  for (size_t i = 0; i < copy.size(); ++i) {
    copy[i] = buf[i];
  }
  EXPECT_THAT(copy, testing::Contains(testing::Ne(uint32_t{0})));
}

TEST(RandenPoolSeedSeqTest,
     GenerateAssignsCustomSinkWithoutOverwritingAdjacentFields) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  // Size 12 exceeds InlinedVector<uint32_t, 8>'s inline capacity to exercise
  // heap-backed BufferTag generation and scrubbing as well.
  AssignableSeedSink sink_array[12];
  seeder.generate(std::begin(sink_array), std::end(sink_array));
  EXPECT_THAT(sink_array, testing::Each(IsAssignedOnceKeepingCanaries()));
  EXPECT_THAT(sink_array, testing::Contains(testing::Field(
                              &AssignableSeedSink::value, testing::Ne(0u))));

  std::vector<AssignableSeedSink> sink_vec(6);
  seeder.generate(sink_vec.begin(), sink_vec.end());
  EXPECT_THAT(sink_vec, testing::Each(IsAssignedOnceKeepingCanaries()));
}

TEST(RandenPoolSeedSeqTest,
     GeneratePopulatesFloatingPointElementsInUint32Range) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  double fp_array[8] = {};
  seeder.generate(std::begin(fp_array), std::end(fp_array));
  EXPECT_THAT(fp_array, testing::Each(IsFiniteInUint32Range()));
}

TEST(RandenPoolSeedSeqTest, GeneratePopulatesVolatileUint32Array) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  volatile uint32_t vol_array[4] = {};
  seeder.generate(std::begin(vol_array), std::end(vol_array));
  uint32_t snapshot[4] = {};
  std::copy(std::begin(vol_array), std::end(vol_array), std::begin(snapshot));
  EXPECT_THAT(snapshot, testing::Contains(testing::Ne(0u)));
}

namespace {
template <typename URBG>
std::array<typename URBG::result_type, 16> GenerateSequence(URBG& gen) {
  std::array<typename URBG::result_type, 16> out;
  std::generate(out.begin(), out.end(), std::ref(gen));
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
using SeedSeq64 =
    TestSeedSeq<uint64_t, /*kConstGenerate=*/false, kTestSeedStart>;
using SeedSeq32 =
    TestSeedSeq<uint32_t, /*kConstGenerate=*/false, kTestSeedStart>;
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
// SeedSequence constructibility traits for NonsecureURBGBase engines.
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
static_assert(
    std::is_constructible_v<absl::BitGen, const ConstGenerateZeroSizeSeedSeq&>);
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
static_assert(!std::is_constructible_v<
              absl::BitGen, const ConstGenerateNonConstSizeSeedSeq&>);
static_assert(
    !std::is_constructible_v<absl::BitGen, ConstGenerateNoSizeSeedSeq&>);
static_assert(
    !std::is_constructible_v<absl::BitGen, const ConstGenerateNoSizeSeedSeq&>);
static_assert(
    std::is_constructible_v<absl::BitGen,
                            const ConstGenerateNonConstSizeWithParamSeedSeq&>);
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
static_assert(
    std::is_constructible_v<absl::InsecureBitGen,
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
}  // namespace

TEST(NonsecureURBGBase, ConstSaltedSeedSeqMatchesMutable) {
  absl::SeedSeq mutable_lvalue(kTestSeedWords.begin(), kTestSeedWords.end());
  absl::SeedSeq mutable_rvalue(kTestSeedWords.begin(), kTestSeedWords.end());
  const absl::SeedSeq const_lvalue(kTestSeedWords.begin(),
                                   kTestSeedWords.end());
  const absl::SeedSeq const_rvalue(kTestSeedWords.begin(),
                                   kTestSeedWords.end());

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

TEST(NonsecureURBGBase, BitGenAcceptsMutableNonConstSizeSeedSeq) {
  ConstGenerateNonConstSizeSeedSeq mutable_non_const_size;
  absl::BitGen bitgen_mutable(mutable_non_const_size);
  EXPECT_THAT(GenerateSequence(bitgen_mutable),
              testing::Ne(std::array<absl::BitGen::result_type, 16>{}));
}

TEST(NonsecureURBGBase, InsecureAndExampleAcceptConstNonConstSizeSeedSeq) {
  const ConstGenerateNonConstSizeSeedSeq const_non_const_size;
  absl::InsecureBitGen insecure_non_const_size(const_non_const_size);
  ExampleNonsecureURBG example_non_const_size(const_non_const_size);
  EXPECT_THAT(GenerateSequence(insecure_non_const_size),
              testing::Ne(std::array<absl::InsecureBitGen::result_type, 16>{}));
  EXPECT_THAT(GenerateSequence(example_non_const_size),
              testing::Ne(std::array<ExampleNonsecureURBG::result_type, 16>{}));
}

TEST(NonsecureURBGBase, InsecureAndExampleAcceptNoSizeSeedSeq) {
  const ConstGenerateNoSizeSeedSeq const_no_size;
  absl::InsecureBitGen insecure_no_size(const_no_size);
  ExampleNonsecureURBG example_no_size(const_no_size);
  EXPECT_THAT(GenerateSequence(insecure_no_size),
              testing::Ne(std::array<absl::InsecureBitGen::result_type, 16>{}));
  EXPECT_THAT(GenerateSequence(example_no_size),
              testing::Ne(std::array<ExampleNonsecureURBG::result_type, 16>{}));
}

TEST(NonsecureURBGBase, InsecureBitGenAcceptsConstParamNoSizeSeedSeq) {
  const ConstParamNoSizeSeedSeq const_param_no_size(kParamSeedWords.begin(),
                                                    kParamSeedWords.end());
  absl::InsecureBitGen insecure_param_no_size(const_param_no_size);
  EXPECT_THAT(GenerateSequence(insecure_param_no_size),
              testing::Ne(std::array<absl::InsecureBitGen::result_type, 16>{}));
}

namespace {
// Known differential collision pairs for the C++ [rand.util.seedseq] mixing
// algorithm: each pair (A, B) of distinct s-word seeds produces bit-for-bit
// identical n-word outputs from raw std::seed_seq::generate().
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
}  // namespace

TEST(NonsecureURBGBase, SaltedSeedingSeparatesRawSeedSeqCollisions) {
  if (!absl::random_internal::GetSaltMaterial().has_value()) {
    GTEST_SKIP() << "Salt material unavailable";
  }

  using Pcg32Result = absl::random_internal::pcg32_2018_engine::result_type;
  std::set<std::array<absl::BitGen::result_type, 16>> bitgen_streams;
  std::set<std::array<Pcg32Result, 16>> pcg32_streams;
  for (const auto& seed : kFourWayTwoWordCollision) {
    std::seed_seq std_seq(seed.begin(), seed.end());
    absl::BitGen bitgen(std_seq);
    bitgen_streams.insert(GenerateSequence(bitgen));

    absl::SeedSeq salted_seq(seed.begin(), seed.end());
    absl::random_internal::pcg32_2018_engine pcg32(salted_seq);
    pcg32_streams.insert(GenerateSequence(pcg32));
  }
  EXPECT_THAT(bitgen_streams, testing::SizeIs(4u));
  EXPECT_THAT(pcg32_streams, testing::SizeIs(4u));

  // `s > n` XOF path on InsecureBitGen (4-word PCG64 seed from 8-word input).
  absl::SeedSeq salted_a(kCollisionS8N4A.begin(), kCollisionS8N4A.end());
  absl::SeedSeq salted_b(kCollisionS8N4B.begin(), kCollisionS8N4B.end());
  absl::InsecureBitGen insecure_a(salted_a);
  absl::InsecureBitGen insecure_b(salted_b);
  EXPECT_THAT(GenerateSequence(insecure_a),
              testing::Ne(GenerateSequence(insecure_b)));
}

namespace {
struct RecordingSeedSeq {
  using result_type = uint32_t;
  size_t last_requested = 0;
  size_t size() const { return 2u; }
  template <typename OutIt>
  void generate(OutIt begin, OutIt end) {
    last_requested = static_cast<size_t>(std::distance(begin, end));
    uint32_t v = 1u;
    for (OutIt it = begin; it != end; ++it) *it = v++;
  }
};
}  // namespace

TEST(NonsecureURBGBase, BitGenRequestsFull60WordRateFromCustomSeedSeq) {
  RecordingSeedSeq custom_seq;
  absl::BitGen bitgen(custom_seq);
  EXPECT_EQ(custom_seq.last_requested, 60u);
  EXPECT_NE(bitgen(), 0u);
}
