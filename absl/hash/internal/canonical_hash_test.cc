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
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

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

  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<std::string>()),
                              absl::Hash<std::string>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<std::array<char, 3>>()),
                              absl::Hash<std::array<char, 3>>>));
  EXPECT_TRUE((std::is_same_v<decltype(CanonicalHasher<std::array<char, 5>>()),
                              absl::Hash<std::array<char, 5>>>));
}

TEST(CanonicalHashKeyTest, UniquelyRepresentedTypesShareHashFn) {
  enum class MyEnum : uint32_t { kOne = 1, kTwo = 2 };
  using HashKeyInt32 = CanonicalHashKey<int32_t>;
  using HashKeyEnum = CanonicalHashKey<MyEnum>;
  EXPECT_EQ(HashKeyInt32::get_hash_fn(), HashKeyEnum::get_hash_fn());
  EXPECT_NE(HashKeyInt32::get_hash_fn(),
            CanonicalHashKey<int8_t>::get_hash_fn());

  const MyEnum c = MyEnum::kTwo;
  const size_t seed = 100;
  HashKeyEnum hk{c};
  EXPECT_EQ(HashKeyEnum::get_hash_fn()(hk.data(), seed),
            HashWithSeed().hash(absl::Hash<MyEnum>{}, c, seed));
}

TEST(CanonicalHashKeyTest, DataPointerForSmallAndLargeKeys) {
  // Small keys are encoded by value.
  int32_t a = 42;
  CanonicalHashKey<int32_t> hk_int32{a};
  int32_t decoded = 0;
  std::memcpy(&decoded, hk_int32.data().bytes, sizeof(decoded));
  EXPECT_EQ(decoded, 42);

  // Keys larger than EncodedHashKey are stored by reference and encoded by
  // address.
  absl::string_view sv = "hello";
  CanonicalHashKey<absl::string_view> hk_sv{sv};
  EXPECT_EQ(&hk_sv.key, &sv);
  EXPECT_EQ(hk_sv.data().ToPtr<absl::string_view>(), &sv);

  std::string s = "hello";
  CanonicalHashKey<std::string> hk_str{s};
  EXPECT_EQ(&hk_str.key, &s);
  EXPECT_EQ(hk_str.data().ToPtr<std::string>(), &s);
}

struct PaddedSmallKey {
  char c;
  int16_t i;
  template <typename H>
  friend H AbslHashValue(H h, const PaddedSmallKey& k) {
    return H::combine(std::move(h), k.c, k.i);
  }
};

struct NonDefaultConstructibleSmallKey {
  explicit NonDefaultConstructibleSmallKey(int32_t v) : val(v) {}
  template <typename H>
  friend H AbslHashValue(H h, const NonDefaultConstructibleSmallKey& k) {
    return H::combine(std::move(h), k.val);
  }
  int32_t val;
};

TEST(CanonicalHashKeyTest, PaddedAndPartiallyInitializedSmallKeys) {
  const size_t seed = 100;

  // Small trivially copyable key with padding bytes.
  EXPECT_TRUE(PassKeyByValueInEncodedHashKey<PaddedSmallKey>());
  PaddedSmallKey padded{'a', 7};
  CanonicalHashKey<PaddedSmallKey> hk_padded{padded};
  HashForKeyFn volatile fn_padded =
      CanonicalHashKey<PaddedSmallKey>::get_hash_fn();
  EXPECT_EQ(fn_padded(hk_padded.data(), seed),
            HashWithSeed().hash(absl::Hash<PaddedSmallKey>{}, padded, seed));

  // Disengaged std::optional has uninitialized payload and padding bytes.
  EXPECT_TRUE(PassKeyByValueInEncodedHashKey<std::optional<int16_t>>());
  std::optional<int16_t> disengaged = std::nullopt;
  CanonicalHashKey<std::optional<int16_t>> hk_opt{disengaged};
  HashForKeyFn volatile fn_opt =
      CanonicalHashKey<std::optional<int16_t>>::get_hash_fn();
  EXPECT_EQ(fn_opt(hk_opt.data(), seed),
            HashWithSeed().hash(absl::Hash<std::optional<int16_t>>{},
                                disengaged, seed));

  // Small trivially copyable key that is not default-constructible.
  EXPECT_TRUE(
      PassKeyByValueInEncodedHashKey<NonDefaultConstructibleSmallKey>());
  NonDefaultConstructibleSmallKey non_def{123};
  CanonicalHashKey<NonDefaultConstructibleSmallKey> hk_non_def{non_def};
  HashForKeyFn volatile fn_non_def =
      CanonicalHashKey<NonDefaultConstructibleSmallKey>::get_hash_fn();
  EXPECT_EQ(fn_non_def(hk_non_def.data(), seed),
            HashWithSeed().hash(absl::Hash<NonDefaultConstructibleSmallKey>{},
                                non_def, seed));
}

}  // namespace
}  // namespace hash_internal
ABSL_NAMESPACE_END
}  // namespace absl
