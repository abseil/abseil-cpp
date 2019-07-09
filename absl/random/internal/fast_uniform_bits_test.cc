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
}

TEST(FastUniformBitsTest, Urng4_VariousOutputs) {
  // Tests that how values are composed; the single-bit deltas should be spread
  // across each invocation.
  Urng4bits urng4;
  Urng32bits urng32;

  // 8-bit types
  {
    absl::random_internal::FastUniformBits<uint8_t> fast8;
    EXPECT_EQ(0x11, fast8(urng4));
    EXPECT_EQ(0x1, fast8(urng32));
  }

  // 16-bit types
  {
    absl::random_internal::FastUniformBits<uint16_t> fast16;
    EXPECT_EQ(0x1111, fast16(urng4));
    EXPECT_EQ(0x1, fast16(urng32));
  }

  // 32-bit types
  {
    absl::random_internal::FastUniformBits<uint32_t> fast32;
    EXPECT_EQ(0x11111111, fast32(urng4));
    EXPECT_EQ(0x1, fast32(urng32));
  }

  // 64-bit types
  {
    absl::random_internal::FastUniformBits<uint64_t> fast64;
    EXPECT_EQ(0x1111111111111111, fast64(urng4));
    EXPECT_EQ(0x0000000100000001, fast64(urng32));
  }
}

}  // namespace
