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
#include <type_traits>

#include "absl/base/casts.h"
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

using HashForKeyFn = size_t (*)(const void* hasher, size_t seed);

// Returns true if `key` is passed by value inside the `const void*` argument of
// `HashForKeyFn`, which avoids spilling it to the stack at the call site.
// Only uniquely represented types qualify: types with padding or otherwise
// uninitialized bits (e.g. a disengaged `std::optional`) would pass those bits
// as a function argument, which MemorySanitizer reports as a use of an
// uninitialized value.
template <class T>
constexpr bool PassKeyByValueInVoidPtr() {
  // The `sizeof(uintptr_t)` check is not redundant on 32-bit platforms.
  return is_uniquely_represented<T>::value && FitsIn64BitsPowerOf2<T>::value &&
         sizeof(T) <= sizeof(uintptr_t);
}

// Type erased function to apply `Hasher` (the result of `CanonicalHasher<T>()`)
// to the key encoded by `CanonicalHashKey::data()`.
template <class Hasher>
size_t TypeErasedApplyAbslHashToKeyFn(const void* key, size_t seed) {
  using T = typename Hasher::KeyType;
  if constexpr (PassKeyByValueInVoidPtr<T>()) {
    const auto val =
        static_cast<UintOfSizeT<sizeof(T)>>(reinterpret_cast<uintptr_t>(key));
    return HashWithSeed().hash(Hasher(), absl::bit_cast<T>(val), seed);
  } else {
    return HashWithSeed().hash(Hasher(), *static_cast<const T*>(key), seed);
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
  const void* data() const {
    if constexpr (PassKeyByValueInVoidPtr<CanonicalKey>()) {
      // Small keys are encoded by value inside the pointer, other keys by
      // address. The decision is made on `CanonicalKey` so that it matches the
      // decoding in `TypeErasedApplyAbslHashToKeyFn`.
      static_assert(sizeof(NonConstK) == sizeof(CanonicalKey));
      return reinterpret_cast<const void*>(
          uintptr_t{absl::bit_cast<UintOfSizeT<sizeof(CanonicalKey)>>(key)});
    } else {
      return &key;
    }
  }
  static constexpr HashForKeyFn get_hash_fn() {
    return &TypeErasedApplyAbslHashToKeyFn<Canonical>;
  }

  // Small keys are stored by value so that inlined caller keys stay in
  // registers instead of becoming address-taken via `data()`.
  type_traits_internal::AddConstRefIfExpensiveToCopy<Key> key;
};

}  // namespace hash_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_HASH_INTERNAL_CANONICAL_HASH_H_
