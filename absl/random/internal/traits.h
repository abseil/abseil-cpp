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

#ifndef ABSL_RANDOM_INTERNAL_TRAITS_H_
#define ABSL_RANDOM_INTERNAL_TRAITS_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/meta/type_traits.h"
#include "absl/numeric/bits.h"
#include "absl/numeric/int128.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// is_urbg<URBG>
//
// Indicates whether a type URBG is a Uniform Random Bit Generator.
template <typename URBG, typename = void, typename = void, typename = void>
struct is_urbg : std::false_type {};

template <typename URBG>
struct is_urbg<
    URBG,
    std::enable_if_t<std::is_same_v<typename URBG::result_type,
                                    std::decay_t<decltype((URBG::min)())>>>,
    std::enable_if_t<std::is_same_v<typename URBG::result_type,
                                    std::decay_t<decltype((URBG::max)())>>>,
    std::enable_if_t<
        std::is_same_v<typename URBG::result_type,
                       std::decay_t<decltype(std::declval<URBG>()())>>>>
    : std::true_type {};

// random_internal::is_widening_convertible<A, B>
//
// Returns whether a type A is widening-convertible to a type B.
//
// A is widening-convertible to B means:
//   A a = <any number>;
//   B b = a;
//   A c = b;
//   EXPECT_EQ(a, c);
template <typename A, typename B>
class is_widening_convertible {
  // As long as there are enough bits in the exact part of a number:
  // - unsigned can fit in float, signed, unsigned
  // - signed can fit in float, signed
  // - float can fit in float
  // So we define rank to be:
  // - rank(float) -> 2
  // - rank(signed) -> 1
  // - rank(unsigned) -> 0
  template <class T>
  static constexpr int rank() {
    return !std::numeric_limits<T>::is_integer +
           std::numeric_limits<T>::is_signed;
  }

 public:
  // If an arithmetic-type B can represent at least as many digits as a type A,
  // and B belongs to a rank no lower than A, then A can be safely represented
  // by B through a widening-conversion.
  static constexpr bool value =
      std::numeric_limits<A>::digits <= std::numeric_limits<B>::digits &&
      rank<A>() <= rank<B>();
};

template <typename T>
struct IsIntegral : std::is_integral<T> {};
template <>
struct IsIntegral<absl::int128> : std::true_type {};
template <>
struct IsIntegral<absl::uint128> : std::true_type {};

template <typename T>
struct MakeUnsigned : std::make_unsigned<T> {};
template <>
struct MakeUnsigned<absl::int128> {
  using type = absl::uint128;
};
template <>
struct MakeUnsigned<absl::uint128> {
  using type = absl::uint128;
};

template <typename T>
struct IsUnsigned : std::is_unsigned<T> {};
template <>
struct IsUnsigned<absl::int128> : std::false_type {};
template <>
struct IsUnsigned<absl::uint128> : std::true_type {};

// unsigned_bits<N>::type returns the unsigned int type with the indicated
// number of bits.
template <size_t N>
struct unsigned_bits;

template <>
struct unsigned_bits<8> {
  using type = uint8_t;
};
template <>
struct unsigned_bits<16> {
  using type = uint16_t;
};
template <>
struct unsigned_bits<32> {
  using type = uint32_t;
};
template <>
struct unsigned_bits<64> {
  using type = uint64_t;
};

template <>
struct unsigned_bits<128> {
  using type = absl::uint128;
};

// 256-bit wrapper for wide multiplications.
struct U256 {
  uint128 hi;
  uint128 lo;
};
template <>
struct unsigned_bits<256> {
  using type = U256;
};

template <typename IntType>
struct make_unsigned_bits {
  using type = typename unsigned_bits<
      std::numeric_limits<typename MakeUnsigned<IntType>::type>::digits>::type;
};

template <typename T>
int BitWidth(T v) {
  // Workaround for bit_width not supporting int128.
  // Don't hardcode `64` to make sure this code does not trigger compiler
  // warnings in smaller types.
  constexpr int half_bits = sizeof(T) * 8 / 2;
  if (sizeof(T) == 16 && (v >> half_bits) != 0) {
    return bit_width(static_cast<uint64_t>(v >> half_bits)) + half_bits;
  } else {
    return bit_width(static_cast<uint64_t>(v));
  }
}

// Detects whether `T` is a valid `[rand.req.seedseq]` `result_type`
// (an unqualified unsigned integer type of at least 32 bits).
template <typename T, typename = void>
struct IsValidSeedSeqResultType : std::false_type {};

template <typename T>
struct IsValidSeedSeqResultType<
    T, std::enable_if_t<std::is_same_v<T, absl::remove_cvref_t<T>> &&
                        std::is_integral_v<T> && std::is_unsigned_v<T>>>
    : std::bool_constant<(std::numeric_limits<T>::digits >= 32)> {};

// Detects whether `std::remove_reference_t<SSeq>` has a valid `result_type`
// and supports `seq.generate(uint32_t*, uint32_t*)` on an lvalue (preserving
// `const`).
template <typename SSeq, typename = void>
struct HasSeedSeqGenerate : std::false_type {};

template <typename SSeq>
struct HasSeedSeqGenerate<
    SSeq,
    std::enable_if_t<
        IsValidSeedSeqResultType<
            typename std::remove_reference_t<SSeq>::result_type>::value,
        std::void_t<
            decltype(std::declval<std::remove_reference_t<SSeq>&>().generate(
                std::declval<uint32_t*>(), std::declval<uint32_t*>()))>>>
    : std::true_type {};

// Detects whether `std::remove_reference_t<SSeq>` supports `seq.size()`
// convertible to `size_t` on an lvalue (preserving `const`).
template <typename SSeq, typename = void>
struct HasSeedSeqSize : std::false_type {};

template <typename SSeq>
struct HasSeedSeqSize<
    SSeq, std::enable_if_t<std::is_convertible_v<
              decltype(std::declval<std::remove_reference_t<SSeq>&>().size()),
              size_t>>> : std::true_type {};

// Marks internal SeedSequence types (such as `RandenPoolSeedSeq`) that yield
// full-entropy words directly from an internal entropy pool and can seed
// `randen_engine` with a fixed 256-bit (8-word) draw rather than expanding
// through general `[rand.req.seedseq]` mixing.
template <typename SSeq>
struct IsDirectEntropySeedSeq : std::false_type {};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_TRAITS_H_
