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

#include "absl/random/internal/entropy_pool.h"

#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "gtest/gtest.h"

namespace {

using ::absl::random_internal::GetEntropyFromRandenPool;

// This validates that sequences are independent.
TEST(EntropyPoolTest, VerifySequences) {
  using result_type = uint32_t;
  constexpr int kNumOutputs = 16;
  result_type a[kNumOutputs];
  result_type b[kNumOutputs];

  GetEntropyFromRandenPool(&a, sizeof(a[0]) * kNumOutputs);
  GetEntropyFromRandenPool(&b, sizeof(b[0]) * kNumOutputs);

  // Compare the two sequences, counting the number of bits that are different,
  // then verify using a normal-approximation of the binomial distribution.
  size_t changed_bits = 0;
  size_t total_set = 0;
  size_t equal_count = 0;
  size_t zero_count = 0;
  for (size_t i = 0; i < kNumOutputs; ++i) {
    std::bitset<sizeof(result_type) * 8> changed_set(a[i] ^ b[i]);
    changed_bits += changed_set.count();

    std::bitset<sizeof(result_type) * 8> a_set(a[i]);
    std::bitset<sizeof(result_type) * 8> b_set(b[i]);
    total_set += a_set.count() + b_set.count();

    equal_count += (a[i] == b[i]) ? 1 : 0;

    zero_count += (a[i] == 0) ? 1 : 0;
    zero_count += (b[i] == 0) ? 1 : 0;
  }

  constexpr size_t kNBits = kNumOutputs * sizeof(result_type) * 8;

  // This should be a binomial distribution with:
  //    p = 0.5
  //    n = kNBits
  //    sigma =~ 11.3 (sqrt(n * 0.5 * 0.5))
  // So we expect the number of changed bits to be within 5 standard deviations
  // of the mean; this should fail less than one in 3 million times.
  EXPECT_NEAR(changed_bits, kNBits * 0.5, 5 * std::sqrt(kNBits))
      << "@" << changed_bits / static_cast<double>(kNBits);

  // Verify that the number of set bits is also within the expected range;
  // Note that this is summed over the two sequences, so the number of trials
  // is twice the number of bits.
  EXPECT_NEAR(total_set, kNBits, 5 * std::sqrt(2 * kNBits))
      << "@" << total_set / static_cast<double>(2 * kNBits);

  // A[i] == B[i] with probability ~= 16 * 1/2^32; certainly less than 1.
  EXPECT_LE(equal_count, 1);

  // Zeros values must be rare; 32 / 2^32 is certainly less than 1.
  EXPECT_LE(zero_count, 1);
}

}  // namespace
