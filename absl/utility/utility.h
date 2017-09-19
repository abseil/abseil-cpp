// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// absl/utility:utility is an extension of the <utility> header
//
// It provides stand-ins for C++14's std::integer_sequence and
// related utilities. They are intended to be exactly equivalent.
//   - integer_sequence<T, Ints...>  == std::integer_sequence<T, Ints...>
//   - index_sequence<Ints...>       == std::index_sequence<Ints...>
//   - make_integer_sequence<T, N>   == std::make_integer_sequence<T, N>
//   - make_index_sequence<N>        == std::make_index_sequence<N>
//   - index_sequence_for<Ts...>     == std::index_sequence_for<Ts...>
//
// It also provides the tag types in_place_t, in_place_type_t, and
// in_place_index_t, as well as the constant in_place, and constexpr std::move
// and std::forward impolementations in C++11.
//
// References:
//  http://en.cppreference.com/w/cpp/utility/integer_sequence
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3658.html
//
// Example:
//   // Unpack a tuple for use as a function call's argument list.
//
//   template <typename F, typename Tup, size_t... Is>
//   auto Impl(F f, const Tup& tup, index_sequence<Is...>)
//       -> decltype(f(std::get<Is>(tup) ...)) {
//     return f(std::get<Is>(tup) ...);
//   }
//
//   template <typename Tup>
//   using TupIdxSeq = make_index_sequence<std::tuple_size<Tup>::value>;
//
//   template <typename F, typename Tup>
//   auto ApplyFromTuple(F f, const Tup& tup)
//       -> decltype(Impl(f, tup, TupIdxSeq<Tup>{})) {
//     return Impl(f, tup, TupIdxSeq<Tup>{});
//   }
#ifndef ABSL_UTILITY_UTILITY_H_
#define ABSL_UTILITY_UTILITY_H_

#include <cstddef>
#include <cstdlib>
#include <utility>

#include "absl/base/config.h"
#include "absl/meta/type_traits.h"

namespace absl {

// Equivalent to std::integer_sequence.
//
// Function templates can deduce compile-time integer sequences by accepting
// an argument of integer_sequence<T, Ints...>. This is a common need when
// working with C++11 variadic templates.
//
// T       - the integer type of the sequence elements
// ...Ints - a parameter pack of T values representing the sequence
template <typename T, T... Ints>
struct integer_sequence {
  using value_type = T;
  static constexpr size_t size() noexcept { return sizeof...(Ints); }
};

// Equivalent to std::index_sequence.
//
// Alias for an integer_sequence of size_t.
template <size_t... Ints>
using index_sequence = integer_sequence<size_t, Ints...>;

namespace utility_internal {

template <typename Seq, size_t SeqSize, size_t Rem>
struct Extend;

// Note that SeqSize == sizeof...(Ints). It's passed explicitly for efficiency.
template <typename T, T... Ints, size_t SeqSize>
struct Extend<integer_sequence<T, Ints...>, SeqSize, 0> {
  using type = integer_sequence<T, Ints..., (Ints + SeqSize)...>;
};

template <typename T, T... Ints, size_t SeqSize>
struct Extend<integer_sequence<T, Ints...>, SeqSize, 1> {
  using type = integer_sequence<T, Ints..., (Ints + SeqSize)..., 2 * SeqSize>;
};

// Recursion helper for 'make_integer_sequence<T, N>'.
// 'Gen<T, N>::type' is an alias for 'integer_sequence<T, 0, 1, ... N-1>'.
template <typename T, size_t N>
struct Gen {
  using type =
      typename Extend<typename Gen<T, N / 2>::type, N / 2, N % 2>::type;
};

template <typename T>
struct Gen<T, 0> {
  using type = integer_sequence<T>;
};

}  // namespace utility_internal

// Compile-time sequences of integers

// Equivalent to std::make_integer_sequence.
//
// make_integer_sequence<int, N> is integer_sequence<int, 0, 1, ..., N-1>;
template <typename T, T N>
using make_integer_sequence = typename utility_internal::Gen<T, N>::type;

// Equivalent to std::make_index_sequence.
//
// make_index_sequence<N> is index_sequence<0, 1, ..., N-1>;
template <size_t N>
using make_index_sequence = make_integer_sequence<size_t, N>;

// Equivalent to std::index_sequence_for.
//
// Convert a typename pack into an index sequence of the same length.
template <typename... Ts>
using index_sequence_for = make_index_sequence<sizeof...(Ts)>;

// Tag types

#ifdef ABSL_HAVE_STD_OPTIONAL

using std::in_place_t;
using std::in_place;

#else  // ABSL_HAVE_STD_OPTIONAL

// Tag type used in order to specify in-place construction, such as with
// absl::optional.
struct in_place_t {};
extern const in_place_t in_place;

#endif  // ABSL_HAVE_STD_OPTIONAL

#ifdef ABSL_HAVE_STD_ANY
using std::in_place_type_t;
#else
// Tag types used for in-place construction when the type to construct needs to
// be specified, such as with absl::variant and absl::any.
template <typename T>
struct in_place_type_t {};
#endif  // ABSL_HAVE_STD_ANY

template <size_t I>
struct in_place_index_t {};

// Constexpr move and forward

template <typename T>
constexpr absl::remove_reference_t<T>&& move(T&& t) noexcept {
  return static_cast<absl::remove_reference_t<T>&&>(t);
}

template <typename T>
constexpr T&& forward(
    absl::remove_reference_t<T>& t) noexcept {  // NOLINT(runtime/references)
  return static_cast<T&&>(t);
}

}  // namespace absl

#endif  // ABSL_UTILITY_UTILITY_H_
