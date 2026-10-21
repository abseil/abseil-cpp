//
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
//
// -----------------------------------------------------------------------------
// File: str_cat.h
// -----------------------------------------------------------------------------
//
// This package contains functions for efficiently concatenating and appending
// strings: `StrCat()` and `StrAppend()`. Most of the work within these routines
// is actually handled through use of a special AlphaNum type, which was
// designed to be used as a parameter type that efficiently manages conversion
// to strings and avoids copies in the above operations.
//
// Any routine accepting either a string or a number may accept `AlphaNum`.
// The basic idea is that by accepting a `const AlphaNum &` as an argument
// to your function, your callers will automagically convert bools, integers,
// and floating point values to strings for you.
//
// NOTE: Use of `AlphaNum` outside of the //absl/strings package is unsupported
// except for the specific case of function parameters of type `AlphaNum` or
// `const AlphaNum &`. In particular, instantiating `AlphaNum` directly as a
// stack variable is not supported.
//
// Conversion from 8-bit values is not accepted because, if it were, then an
// attempt to pass ':' instead of ":" might result in a 58 ending up in your
// result.
//
// Bools convert to "0" or "1". Pointers to types other than `char *` are not
// valid inputs. No output is generated for null `char *` pointers.
//
// Floating point numbers are formatted with six-digit precision, which is
// the default for "std::cout <<" or printf "%g" (the same as "%.6g").
//
// Floating point values can also be converted to a string which, if passed to
// `strtod()`, would produce the exact same original double (except in case of
// NaN; all NaNs are considered the same value) by passing the number to
// absl::HighPrecision. HighPrecision tries to keep the string short but
// it's not guaranteed to be as short as possible.
// See http://go/faster-double-strcat
//
// You can convert to hexadecimal output rather than decimal output using the
// `Hex` type contained here. To do so, pass `Hex(my_int)` as a parameter to
// `StrCat()` or `StrAppend()`. You may specify a minimum hex field width using
// a `PadSpec` enum.
//
// User-defined types can be formatted with the `AbslStringify()` customization
// point. The API relies on detecting an overload in the user-defined type's
// namespace of a free (non-member) `AbslStringify()` function as a definition
// (typically declared as a friend and implemented in-line.
// with the following signature:
//
// class MyClass { ... };
//
// template <typename Sink>
// void AbslStringify(Sink& sink, const MyClass& value);
//
// An `AbslStringify()` overload for a type should only be declared in the same
// file and namespace as said type.
//
// Note that `AbslStringify()` also supports use with `absl::StrFormat()` and
// `absl::Substitute()`.
//
// Example:
//
// struct Point {
//   // To add formatting support to `Point`, we simply need to add a free
//   // (non-member) function `AbslStringify()`. This method specifies how
//   // Point should be printed when absl::StrCat() is called on it. You can add
//   // such a free function using a friend declaration within the body of the
//   // class. The sink parameter is a templated type to avoid requiring
//   // dependencies.
//   template <typename Sink> friend void AbslStringify(Sink&
//   sink, const Point& p) {
//     absl::Format(&sink, "(%v, %v)", p.x, p.y);
//   }
//
//   int x;
//   int y;
// };
// -----------------------------------------------------------------------------

#ifndef ABSL_STRINGS_STR_CAT_H_
#define ABSL_STRINGS_STR_CAT_H_

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/has_absl_stringify.h"
#include "absl/strings/internal/stringify_sink.h"
#include "absl/strings/numbers.h"
#include "absl/strings/resize_and_overwrite.h"
#include "absl/strings/string_view.h"

#if !defined(ABSL_USES_STD_STRING_VIEW)
#include <string_view>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN

struct Hex;
struct Dec;

namespace strings_internal {
// AlphaNumBuffer allows a way to pass a string to StrCat without having to do
// memory allocation.  It is simply a pair of a fixed-size character array, and
// a size.  Please don't use outside of absl, yet.
template <size_t max_size>
struct AlphaNumBuffer {
  std::array<char, max_size> data;
  size_t size;
};

inline constexpr char kSmallNegDigits[19] = "-9-8-7-6-5-4-3-2-1";
inline constexpr char kSmallNonNegDigits[201] =
    "0001020304050607080910111213141516171819"
    "2021222324252627282930313233343536373839"
    "4041424344454647484950515253545556575859"
    "6061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

template <typename T, size_t N>
ABSL_ATTRIBUTE_ALWAYS_INLINE absl::string_view FormatIntToPiece(
    T x, char (&buf)[N]) {
#if ABSL_HAVE_BUILTIN(__builtin_constant_p)
  if (__builtin_constant_p(x)) {
    if constexpr (std::is_signed_v<T>) {
      if (x >= -9 && x < 0) {
        return absl::string_view(
            &kSmallNegDigits[static_cast<size_t>(x + 9) * 2], 2);
      }
      if (x >= -999 && x < -9) {
        const size_t u =
            static_cast<size_t>(uint64_t{0} - static_cast<uint64_t>(x));
        buf[0] = '-';
        if (u < 100) {
          buf[1] = kSmallNonNegDigits[u * 2];
          buf[2] = kSmallNonNegDigits[u * 2 + 1];
          return absl::string_view(buf, 3);
        }
        buf[1] = static_cast<char>('0' + (u / 100));
        buf[2] = kSmallNonNegDigits[(u % 100) * 2];
        buf[3] = kSmallNonNegDigits[(u % 100) * 2 + 1];
        return absl::string_view(buf, 4);
      }
      if (x >= 0 && x <= 9999) {
        const size_t u = static_cast<size_t>(x);
        if (u <= 99) {
          const size_t idx = u * 2;
          return u < 10 ? absl::string_view(&kSmallNonNegDigits[idx + 1], 1)
                        : absl::string_view(&kSmallNonNegDigits[idx], 2);
        }
        if (u < 1000) {
          buf[0] = static_cast<char>('0' + (u / 100));
          buf[1] = kSmallNonNegDigits[(u % 100) * 2];
          buf[2] = kSmallNonNegDigits[(u % 100) * 2 + 1];
          return absl::string_view(buf, 3);
        }
        buf[0] = kSmallNonNegDigits[(u / 100) * 2];
        buf[1] = kSmallNonNegDigits[(u / 100) * 2 + 1];
        buf[2] = kSmallNonNegDigits[(u % 100) * 2];
        buf[3] = kSmallNonNegDigits[(u % 100) * 2 + 1];
        return absl::string_view(buf, 4);
      }
    } else {
      if (x <= 9999) {
        const size_t u = static_cast<size_t>(x);
        if (u <= 99) {
          const size_t idx = u * 2;
          return u < 10 ? absl::string_view(&kSmallNonNegDigits[idx + 1], 1)
                        : absl::string_view(&kSmallNonNegDigits[idx], 2);
        }
        if (u < 1000) {
          buf[0] = static_cast<char>('0' + (u / 100));
          buf[1] = kSmallNonNegDigits[(u % 100) * 2];
          buf[2] = kSmallNonNegDigits[(u % 100) * 2 + 1];
          return absl::string_view(buf, 3);
        }
        buf[0] = kSmallNonNegDigits[(u / 100) * 2];
        buf[1] = kSmallNonNegDigits[(u / 100) * 2 + 1];
        buf[2] = kSmallNonNegDigits[(u % 100) * 2];
        buf[3] = kSmallNonNegDigits[(u % 100) * 2 + 1];
        return absl::string_view(buf, 4);
      }
    }
  }
#endif
  const char* end = numbers_internal::FastIntToBuffer(x, buf);
  const size_t len = static_cast<size_t>(end - buf);
  ABSL_ASSUME(len <= N);
  return absl::string_view(buf, len);
}

ABSL_ATTRIBUTE_NOINLINE absl::string_view FormatHex(Hex hex,
                                                    char* absl_nonnull end);
ABSL_ATTRIBUTE_NOINLINE absl::string_view FormatDec(Dec dec,
                                                    char* absl_nonnull end);

}  // namespace strings_internal

// Enum that specifies the number of significant digits to return in a `Hex` or
// `Dec` conversion and fill character to use. A `kZeroPad2` value, for example,
// would produce hexadecimal strings such as "0a","0f" and a 'kSpacePad5' value
// would produce hexadecimal strings such as "    a","    f".
enum PadSpec : uint8_t {
  kNoPad = 1,
  kZeroPad2,
  kZeroPad3,
  kZeroPad4,
  kZeroPad5,
  kZeroPad6,
  kZeroPad7,
  kZeroPad8,
  kZeroPad9,
  kZeroPad10,
  kZeroPad11,
  kZeroPad12,
  kZeroPad13,
  kZeroPad14,
  kZeroPad15,
  kZeroPad16,
  kZeroPad17,
  kZeroPad18,
  kZeroPad19,
  kZeroPad20,

  kSpacePad2 = kZeroPad2 + 64,
  kSpacePad3,
  kSpacePad4,
  kSpacePad5,
  kSpacePad6,
  kSpacePad7,
  kSpacePad8,
  kSpacePad9,
  kSpacePad10,
  kSpacePad11,
  kSpacePad12,
  kSpacePad13,
  kSpacePad14,
  kSpacePad15,
  kSpacePad16,
  kSpacePad17,
  kSpacePad18,
  kSpacePad19,
  kSpacePad20,
};

// -----------------------------------------------------------------------------
// Hex
// -----------------------------------------------------------------------------
//
// `Hex` stores a set of hexadecimal string conversion parameters for use
// within `AlphaNum` string conversions.
struct Hex {
  uint64_t value;
  uint8_t width;
  char fill;

  template <typename Int>
  explicit Hex(Int v, PadSpec spec = absl::kNoPad,
               std::enable_if_t<sizeof(Int) == 1 && !std::is_pointer_v<Int>,
                                bool> = true)
      : Hex(spec, static_cast<uint8_t>(v)) {}
  template <typename Int>
  explicit Hex(Int v, PadSpec spec = absl::kNoPad,
               std::enable_if_t<sizeof(Int) == 2 && !std::is_pointer_v<Int>,
                                bool> = true)
      : Hex(spec, static_cast<uint16_t>(v)) {}
  template <typename Int>
  explicit Hex(Int v, PadSpec spec = absl::kNoPad,
               std::enable_if_t<sizeof(Int) == 4 && !std::is_pointer_v<Int>,
                                bool> = true)
      : Hex(spec, static_cast<uint32_t>(v)) {}
  template <typename Int>
  explicit Hex(Int v, PadSpec spec = absl::kNoPad,
               std::enable_if_t<sizeof(Int) == 8 && !std::is_pointer_v<Int>,
                                bool> = true)
      : Hex(spec, static_cast<uint64_t>(v)) {}
  template <typename Pointee>
  explicit Hex(Pointee* absl_nullable v, PadSpec spec = absl::kNoPad)
      : Hex(spec, reinterpret_cast<uintptr_t>(v)) {}

  template <typename S>
  friend void AbslStringify(S& sink, Hex hex) {
    char buffer[numbers_internal::kFastToBufferSize];
    sink.Append(strings_internal::FormatHex(
        hex, &buffer[numbers_internal::kFastToBufferSize]));
  }

 private:
  Hex(PadSpec spec, uint64_t v)
      : value(v),
        width(spec == absl::kNoPad
                  ? 1
                  : spec >= absl::kSpacePad2 ? spec - absl::kSpacePad2 + 2
                                             : spec - absl::kZeroPad2 + 2),
        fill(spec >= absl::kSpacePad2 ? ' ' : '0') {}
};

// -----------------------------------------------------------------------------
// Dec
// -----------------------------------------------------------------------------
//
// `Dec` stores a set of decimal string conversion parameters for use
// within `AlphaNum` string conversions.  Dec is slower than the default
// integer conversion, so use it only if you need padding.
struct Dec {
  uint64_t value;
  uint8_t width;
  char fill;
  bool neg;

  template <typename Int>
  explicit Dec(Int v, PadSpec spec = absl::kNoPad,
               std::enable_if_t<sizeof(Int) <= 8, bool> = true)
      : value(v >= 0 ? static_cast<uint64_t>(v)
                     : uint64_t{0} - static_cast<uint64_t>(v)),
        width(spec == absl::kNoPad       ? 1
              : spec >= absl::kSpacePad2 ? spec - absl::kSpacePad2 + 2
                                         : spec - absl::kZeroPad2 + 2),
        fill(spec >= absl::kSpacePad2 ? ' ' : '0'),
        neg(v < 0) {}

  template <typename S>
  friend void AbslStringify(S& sink, Dec dec) {
    char buffer[numbers_internal::kFastToBufferSize];
    sink.Append(strings_internal::FormatDec(
        dec, &buffer[numbers_internal::kFastToBufferSize]));
  }
};

// -----------------------------------------------------------------------------
// HighPrecision
// -----------------------------------------------------------------------------
//
// Converts floating point values to a string which, if passed to
// `absl::SimpleAtof`/`absl::SimpleAtod`, would produce the exact same original
// floating point value (except in case of NaN; all NaNs are considered the same
// value). Tries to keep the string short but it's not guaranteed to be as short
// as possible.
//
// HighPrecision is conisderably slower than the default formatting, so only use
// it if you need the string to convert back to the same floating-point value.

ABSL_ATTRIBUTE_NOINLINE
strings_internal::AlphaNumBuffer<numbers_internal::kFastToBufferSize>
HighPrecision(float f);

ABSL_ATTRIBUTE_NOINLINE
strings_internal::AlphaNumBuffer<numbers_internal::kFastToBufferSize>
HighPrecision(double d);

// -----------------------------------------------------------------------------
// AlphaNum
// -----------------------------------------------------------------------------
//
// The `AlphaNum` class acts as the main parameter type for `StrCat()` and
// `StrAppend()`, providing efficient conversion of numeric, boolean, decimal,
// and hexadecimal values (through the `Dec` and `Hex` types) into strings.
// `AlphaNum` should only be used as a function parameter. Do not instantiate
//  `AlphaNum` directly as a stack variable.

class AlphaNum {
 public:
  // Templated bool constructor avoids converting incoming pointers to bool.
  template <typename T, std::enable_if_t<std::is_same_v<T, bool>, int> = 0>
  AlphaNum(T b)  // NOLINT(*)
      : piece_(&"01"[static_cast<size_t>(b)], 1) {}

  // Prevent brace initialization
  template <typename T>
  AlphaNum(std::initializer_list<T>) = delete;  // NOLINT(runtime/explicit)

  AlphaNum(int x)  // NOLINT(runtime/explicit)
      : piece_(strings_internal::FormatIntToPiece(x, digits_)) {}
  AlphaNum(unsigned int x)  // NOLINT(runtime/explicit)
      : piece_(strings_internal::FormatIntToPiece(x, digits_)) {}
  AlphaNum(long x)  // NOLINT(*)
      : piece_(strings_internal::FormatIntToPiece(x, digits_)) {}
  AlphaNum(unsigned long x)  // NOLINT(*)
      : piece_(strings_internal::FormatIntToPiece(x, digits_)) {}
  AlphaNum(long long x)  // NOLINT(*)
      : piece_(strings_internal::FormatIntToPiece(x, digits_)) {}
  AlphaNum(unsigned long long x)  // NOLINT(*)
      : piece_(strings_internal::FormatIntToPiece(x, digits_)) {}

  AlphaNum(float f)  // NOLINT(runtime/explicit)
      : piece_(MakePieceWithLength(numbers_internal::SixDigitsToBuffer(
            static_cast<double>(f), digits_))) {}
  AlphaNum(double f)  // NOLINT(runtime/explicit)
      : piece_(MakePieceWithLength(
            numbers_internal::SixDigitsToBuffer(f, digits_))) {}

  template <size_t size>
  AlphaNum(  // NOLINT(runtime/explicit)
      const strings_internal::AlphaNumBuffer<size>& buf
          ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : piece_(MakePieceFromBuffer(buf)) {}

  AlphaNum(const char* absl_nullable c_str  // NOLINT(runtime/explicit)
               ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : piece_(NullSafeStringView(c_str)) {}
  AlphaNum(absl::string_view pc  // NOLINT(runtime/explicit)
               ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : piece_(pc) {}

#if !defined(ABSL_USES_STD_STRING_VIEW)
  AlphaNum(std::string_view pc  // NOLINT(runtime/explicit)
               ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : piece_(pc.data(), pc.size()) {}
#endif  // !ABSL_USES_STD_STRING_VIEW

  template <typename T, typename = std::enable_if_t<HasAbslStringify<T>::value>>
  AlphaNum(  // NOLINT(runtime/explicit)
      const T& v ABSL_ATTRIBUTE_LIFETIME_BOUND,
      strings_internal::StringifySink&& sink ABSL_ATTRIBUTE_LIFETIME_BOUND = {})
      : piece_(strings_internal::ExtractStringification(sink, v)) {}

  template <typename Allocator>
  AlphaNum(  // NOLINT(runtime/explicit)
      const std::basic_string<char, std::char_traits<char>, Allocator>& str
          ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : piece_(str) {}

  // Use string literals ":" instead of character literals ':'.
  AlphaNum(char c) = delete;  // NOLINT(runtime/explicit)

  AlphaNum(const AlphaNum&) = delete;
  AlphaNum& operator=(const AlphaNum&) = delete;

  absl::string_view::size_type size() const { return piece_.size(); }
  const char* absl_nullable data() const { return piece_.data(); }
  absl::string_view Piece() const { return piece_; }

  // Match unscoped enums.  Use integral promotion so that a `char`-backed
  // enum becomes a wider integral type AlphaNum will accept.
  template <typename T,
            typename = std::enable_if_t<std::is_enum<T>{} &&
                                        std::is_convertible<T, int>{} &&
                                        !HasAbslStringify<T>::value>>
  AlphaNum(T e)  // NOLINT(runtime/explicit)
      : AlphaNum(+e) {}

  // This overload matches scoped enums.  We must explicitly cast to the
  // underlying type, but use integral promotion for the same reason as above.
  template <typename T, std::enable_if_t<std::is_enum<T>{} &&
                                             !std::is_convertible<T, int>{} &&
                                             !HasAbslStringify<T>::value,
                                         char*> = nullptr>
  AlphaNum(T e)  // NOLINT(runtime/explicit)
      : AlphaNum(+static_cast<std::underlying_type_t<T>>(e)) {}

  // vector<bool>::reference and const_reference require special help to
  // convert to `AlphaNum` because it requires two user defined conversions.
  template <
      typename T,
      std::enable_if_t<
          std::is_class_v<T> &&
          (std::is_same_v<T, std::vector<bool>::reference> ||
           std::is_same_v<T, std::vector<bool>::const_reference>)>* = nullptr>
  AlphaNum(T e) : AlphaNum(static_cast<bool>(e)) {}  // NOLINT(runtime/explicit)

 private:
  absl::string_view MakePieceWithLength(size_t len) const {
    ABSL_ASSUME(len <= sizeof(digits_));
    return absl::string_view(digits_, len);
  }
  template <size_t max_size>
  static absl::string_view MakePieceFromBuffer(
      const strings_internal::AlphaNumBuffer<max_size>& buf) {
    const size_t len = buf.size;
    ABSL_ASSUME(len <= max_size);
    return absl::string_view(&buf.data[0], len);
  }

  absl::string_view piece_;
  char digits_[numbers_internal::kFastToBufferSize];
};

// -----------------------------------------------------------------------------
// StrCat()
// -----------------------------------------------------------------------------
//
// Merges given strings or numbers, using no delimiter(s), returning the merged
// result as a string.
//
// `StrCat()` is designed to be the fastest possible way to construct a string
// out of a mix of raw C strings, string_views, strings, bool values,
// and numeric values.
//
// Don't use `StrCat()` for user-visible strings. The localization process
// works poorly on strings built up out of fragments.
//
// For clarity and performance, don't use `StrCat()` when appending to a
// string. Use `StrAppend()` instead. In particular, avoid using any of these
// (anti-)patterns:
//
//   str.append(StrCat(...))
//   str += StrCat(...)
//   str = StrCat(str, ...)
//
// The last case is the worst, with a potential to change a loop
// from a linear time operation with O(1) dynamic allocations into a
// quadratic time operation with O(n) dynamic allocations.
//
// See `StrAppend()` below for more information.

namespace strings_internal {

// Do not call directly - this is not part of the public API.
ABSL_ATTRIBUTE_NOINLINE std::string CatPiece(absl::string_view a);
ABSL_ATTRIBUTE_NOINLINE std::string CatPieces(absl::string_view a,
                                              absl::string_view b);
ABSL_ATTRIBUTE_NOINLINE std::string CatPieces(absl::string_view a,
                                              absl::string_view b,
                                              absl::string_view c);
ABSL_ATTRIBUTE_NOINLINE std::string CatPieces(
    std::initializer_list<absl::string_view> pieces);

ABSL_ATTRIBUTE_NOINLINE void AppendPieces(std::string* absl_nonnull dest,
                                          absl::string_view a);
ABSL_ATTRIBUTE_NOINLINE void AppendPieces(std::string* absl_nonnull dest,
                                          absl::string_view a,
                                          absl::string_view b);
ABSL_ATTRIBUTE_NOINLINE void AppendPieces(std::string* absl_nonnull dest,
                                          absl::string_view a,
                                          absl::string_view b,
                                          absl::string_view c);
ABSL_ATTRIBUTE_NOINLINE void AppendPieces(
    std::string* absl_nonnull dest,
    std::initializer_list<absl::string_view> pieces);

ABSL_ATTRIBUTE_NOINLINE std::string JoinStringViews(
    const absl::string_view* absl_nullable data, size_t count,
    absl::string_view sep);
ABSL_ATTRIBUTE_NOINLINE std::string JoinStrings(
    const std::string* absl_nullable data, size_t count, absl::string_view sep);

// `SingleArgStrCat` overloads take built-in `int`, `long` and `long long` types
// (signed / unsigned) to avoid ambiguity on the call side. If we used int32_t
// and int64_t, then at least one of the three (`int` / `long` / `long long`)
// would have been ambiguous when passed to `SingleArgStrCat`.
ABSL_ATTRIBUTE_NOINLINE std::string SingleArgStrCat(bool x);
ABSL_ATTRIBUTE_NOINLINE std::string SingleArgStrCat(int x);
ABSL_ATTRIBUTE_NOINLINE std::string SingleArgStrCat(unsigned int x);
// NOLINTNEXTLINE
ABSL_ATTRIBUTE_NOINLINE std::string SingleArgStrCat(long x);
// NOLINTNEXTLINE
ABSL_ATTRIBUTE_NOINLINE std::string SingleArgStrCat(unsigned long x);
// NOLINTNEXTLINE
ABSL_ATTRIBUTE_NOINLINE std::string SingleArgStrCat(long long x);
// NOLINTNEXTLINE
ABSL_ATTRIBUTE_NOINLINE std::string SingleArgStrCat(unsigned long long x);
ABSL_ATTRIBUTE_NOINLINE std::string SingleArgStrCat(float x);
ABSL_ATTRIBUTE_NOINLINE std::string SingleArgStrCat(double x);

// As of September 2023, the SingleArgStrCat() optimization is only enabled for
// libc++. The reasons for this are:
// 1) The SSO size for libc++ is 23, while libstdc++ and MSSTL have an SSO size
// of 15. Since IntegerToString unconditionally resizes the string to 22 bytes,
// this causes both libstdc++ and MSSTL to allocate.
// 2) strings_internal::STLStringResizeUninitialized() only has an
// implementation that avoids initialization when using libc++. This isn't as
// relevant as (1), and the cost should be benchmarked if (1) ever changes on
// libstc++ or MSSTL.
#ifdef _LIBCPP_VERSION
#define ABSL_INTERNAL_STRCAT_ENABLE_FAST_CASE true
#else
#define ABSL_INTERNAL_STRCAT_ENABLE_FAST_CASE false
#endif

template <typename T, typename = std::enable_if_t<
                          ABSL_INTERNAL_STRCAT_ENABLE_FAST_CASE &&
                          std::is_arithmetic<T>{} && !std::is_same<T, char>{}>>
using EnableIfFastCase = T;

#undef ABSL_INTERNAL_STRCAT_ENABLE_FAST_CASE

}  // namespace strings_internal

[[nodiscard]] inline std::string StrCat() { return std::string(); }

template <typename T>
[[nodiscard]] inline std::string StrCat(
    strings_internal::EnableIfFastCase<T> a) {
  return strings_internal::SingleArgStrCat(a);
}
[[nodiscard]] inline std::string StrCat(const AlphaNum& a) {
  return strings_internal::CatPiece(a.Piece());
}
[[nodiscard]] inline std::string StrCat(const AlphaNum& a, const AlphaNum& b) {
  return strings_internal::CatPieces(a.Piece(), b.Piece());
}
[[nodiscard]] inline std::string StrCat(const AlphaNum& a, const AlphaNum& b,
                                        const AlphaNum& c) {
  return strings_internal::CatPieces(a.Piece(), b.Piece(), c.Piece());
}
[[nodiscard]] inline std::string StrCat(const AlphaNum& a, const AlphaNum& b,
                                        const AlphaNum& c, const AlphaNum& d) {
  return strings_internal::CatPieces(
      {a.Piece(), b.Piece(), c.Piece(), d.Piece()});
}

// Support 5 or more arguments
template <typename... AV>
[[nodiscard]] inline std::string StrCat(const AlphaNum& a, const AlphaNum& b,
                                        const AlphaNum& c, const AlphaNum& d,
                                        const AlphaNum& e, const AV&... args) {
  return strings_internal::CatPieces(
      {a.Piece(), b.Piece(), c.Piece(), d.Piece(), e.Piece(),
       static_cast<const AlphaNum&>(args).Piece()...});
}

// -----------------------------------------------------------------------------
// StrAppend()
// -----------------------------------------------------------------------------
//
// Appends a string or set of strings to an existing string, in a similar
// fashion to `StrCat()`.
//
// WARNING: `StrAppend(&str, a, b, c, ...)` requires that none of the
// a, b, c, parameters be a reference into str. For speed, `StrAppend()` does
// not try to check each of its input arguments to be sure that they are not
// a subset of the string being appended to. That is, while this will work:
//
//   std::string s = "foo";
//   s += s;
//
// This output is undefined:
//
//   std::string s = "foo";
//   StrAppend(&s, s);
//
// This output is undefined as well, since `absl::string_view` does not own its
// data:
//
//   std::string s = "foobar";
//   absl::string_view p = s;
//   StrAppend(&s, p);

inline void StrAppend(std::string* absl_nonnull) {}

inline void StrAppend(std::string* absl_nonnull dest, const AlphaNum& a) {
  strings_internal::AppendPieces(dest, a.Piece());
}
inline void StrAppend(std::string* absl_nonnull dest, const AlphaNum& a,
                      const AlphaNum& b) {
  strings_internal::AppendPieces(dest, a.Piece(), b.Piece());
}
inline void StrAppend(std::string* absl_nonnull dest, const AlphaNum& a,
                      const AlphaNum& b, const AlphaNum& c) {
  strings_internal::AppendPieces(dest, a.Piece(), b.Piece(), c.Piece());
}
inline void StrAppend(std::string* absl_nonnull dest, const AlphaNum& a,
                      const AlphaNum& b, const AlphaNum& c, const AlphaNum& d) {
  strings_internal::AppendPieces(dest,
                                 {a.Piece(), b.Piece(), c.Piece(), d.Piece()});
}

// Support 5 or more arguments
template <typename... AV>
inline void StrAppend(std::string* absl_nonnull dest, const AlphaNum& a,
                      const AlphaNum& b, const AlphaNum& c, const AlphaNum& d,
                      const AlphaNum& e, const AV&... args) {
  strings_internal::AppendPieces(
      dest, {a.Piece(), b.Piece(), c.Piece(), d.Piece(), e.Piece(),
             static_cast<const AlphaNum&>(args).Piece()...});
}

// Helper function for the future StrCat default floating-point format, %.6g
// This is fast.
ABSL_ATTRIBUTE_NOINLINE
strings_internal::AlphaNumBuffer<numbers_internal::kSixDigitsToBufferSize>
SixDigits(double d);

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_STR_CAT_H_
