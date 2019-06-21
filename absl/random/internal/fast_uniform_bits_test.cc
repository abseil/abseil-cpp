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

#include "absl/random/internal/fast_uniform_bits.h"

#include <random>

#include "gtest/gtest.h"

namespace {

template <typename IntType>
class FastUniformBitsTypedTest : public ::testing::Test {};

using IntTypes = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;

TYPED_TEST_SUITE(FastUniformBitsTypedTest, IntTypes);

TYPED_TEST(FastUniformBitsTypedTest, BasicTest) {
  using Limits = std::numeric_limits<TypeParam>;
  using FastBits = absl::random_internal::FastUniformBits<TypeParam>;

  EXPECT_EQ(0, FastBits::min());
  EXPECT_EQ(Limits::max(), FastBits::max());

  constexpr int kIters = 10000;
  std::random_device rd;
  std::mt19937 gen(rd());
  FastBits fast;
  for (int i = 0; i < kIters; i++) {
    const auto v = fast(gen);
    EXPECT_LE(v, FastBits::max());
    EXPECT_GE(v, FastBits::min());
  }
}

TEST(FastUniformBitsTest, TypeBoundaries32) {
  // Tests that FastUniformBits can adapt to 32-bit boundaries.
  absl::random_internal::FastUniformBits<uint32_t, 1> a;
  absl::random_internal::FastUniformBits<uint32_t, 31> b;
  absl::random_internal::FastUniformBits<uint32_t, 32> c;

  {
    std::mt19937 gen;  // 32-bit
    a(gen);
    b(gen);
    c(gen);
  }

  {
    std::mt19937_64 gen;  // 64-bit
    a(gen);
    b(gen);
    c(gen);
  }
}

TEST(FastUniformBitsTest, TypeBoundaries64) {
  // Tests that FastUniformBits can adapt to 64-bit boundaries.
  absl::random_internal::FastUniformBits<uint64_t, 1> a;
  absl::random_internal::FastUniformBits<uint64_t, 31> b;
  absl::random_internal::FastUniformBits<uint64_t, 32> c;
  absl::random_internal::FastUniformBits<uint64_t, 33> d;
  absl::random_internal::FastUniformBits<uint64_t, 63> e;
  absl::random_internal::FastUniformBits<uint64_t, 64> f;

  {
    std::mt19937 gen;  // 32-bit
    a(gen);
    b(gen);
    c(gen);
    d(gen);
    e(gen);
    f(gen);
  }

  {
    std::mt19937_64 gen;  // 64-bit
    a(gen);
    b(gen);
    c(gen);
    d(gen);
    e(gen);
    f(gen);
  }
}

class UrngOddbits {
 public:
  using result_type = uint8_t;
  static constexpr result_type min() { return 1; }
  static constexpr result_type max() { return 0xfe; }
  result_type operator()() { return 2; }
};

class Urng4bits {
 public:
  using result_type = uint8_t;
  static constexpr result_type min() { return 1; }
  static constexpr result_type max() { return 0xf + 1; }
  result_type operator()() { return 2; }
};

class Urng32bits {
 public:
  using result_type = uint32_t;
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() { return 0xffffffff; }
  result_type operator()() { return 1; }
};

// Compile-time test to validate the helper classes used by FastUniformBits
TEST(FastUniformBitsTest, FastUniformBitsDetails) {
  using absl::random_internal::FastUniformBitsLoopingConstants;
  using absl::random_internal::FastUniformBitsURBGConstants;

  // 4-bit URBG
  {
    using constants = FastUniformBitsURBGConstants<Urng4bits>;
    static_assert(constants::kPowerOfTwo == true,
                  "constants::kPowerOfTwo == false");
    static_assert(constants::kRange == 16, "constants::kRange == false");
    static_assert(constants::kRangeBits == 4, "constants::kRangeBits == false");
    static_assert(constants::kRangeMask == 0x0f,
                  "constants::kRangeMask == false");
  }
  {
    using looping = FastUniformBitsLoopingConstants<uint32_t, 31, Urng4bits>;
    // To get 31 bits from a 4-bit generator, issue 8 calls and extract 4 bits
    // per call on all except the first.
    static_assert(looping::kN0 == 1, "looping::kN0");
    static_assert(looping::kW0 == 3, "looping::kW0");
    static_assert(looping::kM0 == 0x7, "looping::kM0");
    // (The second set of calls, kN1, will not do anything.)
    static_assert(looping::kN1 == 8, "looping::kN1");
    static_assert(looping::kW1 == 4, "looping::kW1");
    static_assert(looping::kM1 == 0xf, "looping::kM1");
  }

  // ~7-bit URBG
  {
    using constants = FastUniformBitsURBGConstants<UrngOddbits>;
    static_assert(constants::kPowerOfTwo == false,
                  "constants::kPowerOfTwo == false");
    static_assert(constants::kRange == 0xfe, "constants::kRange == 0xfe");
    static_assert(constants::kRangeBits == 7, "constants::kRangeBits == 7");
    static_assert(constants::kRangeMask == 0x7f,
                  "constants::kRangeMask == 0x7f");
  }
  {
    using looping = FastUniformBitsLoopingConstants<uint64_t, 60, UrngOddbits>;
    // To get 60 bits from a 7-bit generator, issue 10 calls and extract 6 bits
    // per call, discarding the excess entropy.
    static_assert(looping::kN0 == 10, "looping::kN0");
    static_assert(looping::kW0 == 6, "looping::kW0");
    static_assert(looping::kM0 == 0x3f, "looping::kM0");
    // (The second set of calls, kN1, will not do anything.)
    static_assert(looping::kN1 == 10, "looping::kN1");
    static_assert(looping::kW1 == 7, "looping::kW1");
    static_assert(looping::kM1 == 0x7f, "looping::kM1");
  }
  {
    using looping = FastUniformBitsLoopingConstants<uint64_t, 63, UrngOddbits>;
    // To get 63 bits from a 7-bit generator, issue 10 calls--the same as we
    // would issue for 60 bits--however this time we use two groups.  The first
    // group (kN0) will issue 7 calls, extracting 6 bits per call.
    static_assert(looping::kN0 == 7, "looping::kN0");
    static_assert(looping::kW0 == 6, "looping::kW0");
    static_assert(looping::kM0 == 0x3f, "looping::kM0");
    // The second group (kN1) will issue 3 calls, extracting 7 bits per call.
    static_assert(looping::kN1 == 10, "looping::kN1");
    static_assert(looping::kW1 == 7, "looping::kW1");
    static_assert(looping::kM1 == 0x7f, "looping::kM1");
  }
}

TEST(FastUniformBitsTest, Urng4_VariousOutputs) {
  // Tests that how values are composed; the single-bit deltas should be spread
  // across each invocation.
  Urng4bits urng4;
  Urng32bits urng32;

  // 8-bit types
  {
    absl::random_internal::FastUniformBits<uint8_t, 1> fast1;
    EXPECT_EQ(0x1, fast1(urng4));
    EXPECT_EQ(0x1, fast1(urng32));
  }
  {
    absl::random_internal::FastUniformBits<uint8_t, 2> fast2;
    EXPECT_EQ(0x1, fast2(urng4));
    EXPECT_EQ(0x1, fast2(urng32));
  }

  {
    absl::random_internal::FastUniformBits<uint8_t, 4> fast4;
    EXPECT_EQ(0x1, fast4(urng4));
    EXPECT_EQ(0x1, fast4(urng32));
  }
  {
    absl::random_internal::FastUniformBits<uint8_t, 6> fast6;
    EXPECT_EQ(0x9, fast6(urng4));  // b001001 (2x3)
    EXPECT_EQ(0x1, fast6(urng32));
  }
  {
    absl::random_internal::FastUniformBits<uint8_t, 6> fast7;
    EXPECT_EQ(0x9, fast7(urng4));  // b00001001 (1x4 + 1x3)
    EXPECT_EQ(0x1, fast7(urng32));
  }

  {
    absl::random_internal::FastUniformBits<uint8_t> fast8;
    EXPECT_EQ(0x11, fast8(urng4));
    EXPECT_EQ(0x1, fast8(urng32));
  }

  // 16-bit types
  {
    absl::random_internal::FastUniformBits<uint16_t, 10> fast10;
    EXPECT_EQ(0x91, fast10(urng4));  // b 0010010001 (2x3 + 1x4)
    EXPECT_EQ(0x1, fast10(urng32));
  }
  {
    absl::random_internal::FastUniformBits<uint16_t, 11> fast11;
    EXPECT_EQ(0x111, fast11(urng4));
    EXPECT_EQ(0x1, fast11(urng32));
  }
  {
    absl::random_internal::FastUniformBits<uint16_t, 12> fast12;
    EXPECT_EQ(0x111, fast12(urng4));
    EXPECT_EQ(0x1, fast12(urng32));
  }

  {
    absl::random_internal::FastUniformBits<uint16_t> fast16;
    EXPECT_EQ(0x1111, fast16(urng4));
    EXPECT_EQ(0x1, fast16(urng32));
  }

  // 32-bit types
  {
    absl::random_internal::FastUniformBits<uint32_t, 21> fast21;
    EXPECT_EQ(0x49111, fast21(urng4));  // b 001001001 000100010001 (3x3 + 3x4)
    EXPECT_EQ(0x1, fast21(urng32));
  }
  {
    absl::random_internal::FastUniformBits<uint32_t, 24> fast24;
    EXPECT_EQ(0x111111, fast24(urng4));
    EXPECT_EQ(0x1, fast24(urng32));
  }

  {
    absl::random_internal::FastUniformBits<uint32_t> fast32;
    EXPECT_EQ(0x11111111, fast32(urng4));
    EXPECT_EQ(0x1, fast32(urng32));
  }

  // 64-bit types
  {
    absl::random_internal::FastUniformBits<uint64_t, 5> fast5;
    EXPECT_EQ(0x9, fast5(urng4));
    EXPECT_EQ(0x1, fast5(urng32));
  }

  {
    absl::random_internal::FastUniformBits<uint64_t, 48> fast48;
    EXPECT_EQ(0x111111111111, fast48(urng4));
    // computes in 2 steps, should be 24 << 24
    EXPECT_EQ(0x000001000001, fast48(urng32));
  }

  {
    absl::random_internal::FastUniformBits<uint64_t> fast64;
    EXPECT_EQ(0x1111111111111111, fast64(urng4));
    EXPECT_EQ(0x0000000100000001, fast64(urng32));
  }
}

}  // namespace
