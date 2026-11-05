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

#ifndef ABSL_HASH_INTERNAL_CANONICAL_HASH_H_
#define ABSL_HASH_INTERNAL_CANONICAL_HASH_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/hash/hash.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace hash_internal {

// Unsigned integer type of size `N` if `N` is 1, 2, 4, or 8, `void` otherwise.
template <size_t N>
using UintOfSizeT = std::conditional_t<
    N == 1, uint8_t,
    std::conditional_t<
        N == 2, uint16_t,
        std::conditional_t<N == 4, uint32_t,
                           std::conditional_t<N == 8, uint64_t, void>>>>;

// Returns a canonical `absl::Hash` for `T`. Types with the exact same hash
// implementation (such as `is_uniquely_represented` types of the same size)
// return the same hasher type, which is useful for type erasure.
template <typename T>
constexpr auto CanonicalHasher() {
  if constexpr (is_uniquely_represented<T>::value &&
                FitsIn64BitsPowerOf2<T>::value) {
    return Hash<UintOfSizeT<sizeof(T)>>();
  } else {
    return Hash<T>();
  }
}

// Type-erased key argument passed by value in a single register on common ABIs.
struct EncodedHashKey {
  template <class T>
  static EncodedHashKey FromValue(const T& v) {
    static_assert(sizeof(T) <= sizeof(bytes));
    static_assert(std::is_trivially_copyable_v<T>);
    EncodedHashKey res{};
    std::memcpy(res.bytes, &v, sizeof(T));
    return res;
  }

  template <class T>
  static EncodedHashKey FromPtr(const T* ptr) {
    return FromValue(ptr);
  }

  template <class T>
  const T* ToPtr() const {
    const T* ptr;
    std::memcpy(&ptr, bytes, sizeof(ptr));
    return ptr;
  }

  // Byte array (rather than `const void*`) avoids UB and MSan `noundef` errors
  // when a small key contains padding or uninitialized bytes.
  alignas(void*) unsigned char bytes[sizeof(void*)];
};

using HashForKeyFn = size_t (*)(EncodedHashKey hasher, size_t seed);

// Returns true if `key` is passed by value inside `EncodedHashKey`, avoiding
// spilling it to the stack at the call site.
template <class T>
constexpr bool PassKeyByValueInEncodedHashKey() {
  return FitsIn64BitsPowerOf2<T>::value &&
         // Not redundant on 32-bit platforms.
         sizeof(T) <= sizeof(EncodedHashKey) &&
         alignof(T) <= alignof(EncodedHashKey) &&
         // Required to copy `key` by value in `CanonicalHashKey` (false for
         // move-only types).
         std::is_trivially_copy_constructible_v<T> &&
         // Required for `memcpy` (false for types with non-trivial `operator=`
         // or destructor, such as `std::pair`).
         std::is_trivially_copyable_v<T>;
}

// Type erased function to apply `Hasher` (the result of `CanonicalHasher<T>()`)
// to the key encoded by `CanonicalHashKey::data()`.
template <class Hasher>
size_t TypeErasedApplyAbslHashToKeyFn(EncodedHashKey key, size_t seed) {
  using T = typename Hasher::KeyType;
  if constexpr (PassKeyByValueInEncodedHashKey<T>()) {
    alignas(T) unsigned char buf[sizeof(T)];
    std::memcpy(buf, key.bytes, sizeof(T));
    return HashWithSeed().hash(
        Hasher(), *std::launder(reinterpret_cast<const T*>(buf)), seed);
  } else {
    return HashWithSeed().hash(Hasher(), *key.ToPtr<T>(), seed);
  }
}

// No arguments hash function for a specific key that uses
// `CanonicalHasher<Key>`, so that `get_hash_fn()` is shared across all key
// types with the same canonical hasher.
template <class Key>
struct CanonicalHashKey {
  using NonConstK = absl::remove_cvref_t<Key>;
  using Canonical = decltype(CanonicalHasher<NonConstK>());
  using CanonicalKey = typename Canonical::KeyType;

  explicit CanonicalHashKey(const NonConstK& k) : key(k) {}

  // `data()` and `get_hash_fn()` are passed to type erased functions.
  EncodedHashKey data() const {
    // Check `CanonicalKey` to match `TypeErasedApplyAbslHashToKeyFn`.
    if constexpr (PassKeyByValueInEncodedHashKey<CanonicalKey>()) {
      static_assert(sizeof(NonConstK) == sizeof(CanonicalKey));
      return EncodedHashKey::FromValue(key);
    } else {
      return EncodedHashKey::FromPtr(&key);
    }
  }
  static constexpr HashForKeyFn get_hash_fn() {
    return &TypeErasedApplyAbslHashToKeyFn<Canonical>;
  }

  // Store small keys by value so inlined caller keys are not address-taken, and
  // larger keys by reference to avoid a stack copy before taking `&key`.
  std::conditional_t<PassKeyByValueInEncodedHashKey<CanonicalKey>(), NonConstK,
                     const NonConstK&>
      key;
};

}  // namespace hash_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_HASH_INTERNAL_CANONICAL_HASH_H_
