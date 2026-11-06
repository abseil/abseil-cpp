// Copyright 2026 The Abseil Authors
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

#include "absl/hash/internal/canonical_hash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace hash_internal {
namespace {

TEST(CanonicalHasherTest, UniquelyRepresentedTypesShareHasher) {
  enum class Enum8 : uint8_t { kA = 1 };
  enum class Enum16 : uint16_t { kA = 1 };
  enum class Enum32 : uint32_t { kA = 1 };
  enum class SignedEnum32 : int32_t { kA = -1 };
  enum class Enum64 : uint64_t { kA = 1 };

  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<int8_t>()),
                              absl::Hash<uint8_t>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<uint8_t>()),
                              absl::Hash<uint8_t>>));
  EXPECT_TRUE((
      std::is_same_v<decltype(CanonicalHasher<Enum8>()), absl::Hash<uint8_t>>));

  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<int16_t>()),
                              absl::Hash<uint16_t>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<uint16_t>()),
                              absl::Hash<uint16_t>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<Enum16>()),
                              absl::Hash<uint16_t>>));

  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<int32_t>()),
                              absl::Hash<uint32_t>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<uint32_t>()),
                              absl::Hash<uint32_t>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<Enum32>()),
                              absl::Hash<uint32_t>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<SignedEnum32>()),
                              absl::Hash<uint32_t>>));

  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<int64_t>()),
                              absl::Hash<uint64_t>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<uint64_t>()),
                              absl::Hash<uint64_t>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<Enum64>()),
                              absl::Hash<uint64_t>>));

  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<int*>()),
                              decltype(CanonicalHasher<uintptr_t>())>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<double*>()),
                              decltype(CanonicalHasher<uintptr_t>())>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<const int*>()),
                              decltype(CanonicalHasher<uintptr_t>())>));

  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<std::string>()),
                              absl::Hash<std::string>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<std::array<char, 3>>()),
                              absl::Hash<std::array<char, 3>>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<std::array<char, 5>>()),
                              absl::Hash<std::array<char, 5>>>));
}

TEST(CanonicalHashKeyTest, UniquelyRepresentedAndPointerTypesShareHashFn) {
  enum class MyEnum : uint32_t { kOne = 1, kTwo = 2 };
  using HashKeyInt32 = CanonicalHashKey<int32_t>;
  using HashKeyEnum = CanonicalHashKey<MyEnum>;
  using HashKeyIntPtr = CanonicalHashKey<int*>;
  EXPECT_EQ(HashKeyInt32::get_hash_fn(), HashKeyEnum::get_hash_fn());
  EXPECT_NE(HashKeyInt32::get_hash_fn(),
            CanonicalHashKey<int8_t>::get_hash_fn());
  EXPECT_EQ(HashKeyIntPtr::get_hash_fn(),
            CanonicalHashKey<uint32_t*>::get_hash_fn());
  EXPECT_EQ(HashKeyIntPtr::get_hash_fn(),
            CanonicalHashKey<uintptr_t>::get_hash_fn());

  const size_t seed = 100;
  const MyEnum c = MyEnum::kTwo;
  HashKeyEnum hk{c};
  EXPECT_EQ(HashKeyEnum::get_hash_fn()(hk.data(), seed),
            HashWithSeed().hash(absl::Hash<MyEnum>{}, c, seed));
  int a = 1;
  int* p = &a;
  HashKeyIntPtr hk_p{p};
  EXPECT_EQ(HashKeyIntPtr::get_hash_fn()(hk_p.data(), seed),
            HashWithSeed().hash(absl::Hash<int*>{}, p, seed));
}

TEST(CanonicalHashKeyTest, DataPointerForSmallAndLargeKeys) {
  // Small keys are encoded by value.
  int32_t a = 42;
  CanonicalHashKey<int32_t> hk_int32{a};
  EXPECT_EQ(reinterpret_cast<uintptr_t>(hk_int32.data()), 42u);

  // Cheap to copy keys are stored by value.
  absl::string_view sv = "hello";
  CanonicalHashKey<absl::string_view> hk_sv{sv};
  EXPECT_EQ(hk_sv.data(), &hk_sv.key);
  EXPECT_NE(hk_sv.data(), &sv);

  // Expensive to copy keys are stored by reference.
  std::string s = "hello";
  CanonicalHashKey<std::string> hk_str{s};
  EXPECT_EQ(hk_str.data(), &s);
}

}  // namespace
}  // namespace hash_internal
ABSL_NAMESPACE_END
}  // namespace absl
