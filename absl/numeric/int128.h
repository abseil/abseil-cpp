//
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
// -----------------------------------------------------------------------------
// File: int128.h
// -----------------------------------------------------------------------------
//
// This header file defines 128-bit integer types. Currently, this file defines
// `uint128`, an unsigned 128-bit integer; a signed 128-bit integer is
// forthcoming.

#ifndef ABSL_NUMERIC_INT128_H_
#define ABSL_NUMERIC_INT128_H_

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <limits>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/base/port.h"

namespace absl {

// uint128
//
// An unsigned 128-bit integer type. The API is meant to mimic an intrinsic type
// as closely as is practical, including exhibiting undefined behavior in
// analogous cases (e.g. division by zero). This type is intended to be a
// drop-in replacement once C++ supports an intrinsic `uint128_t` type; when
// that occurs, existing uses of `uint128` will continue to work using that new
// type.
//
// Note: code written with this type will continue to compile once `uint128_t`
// is introduced, provided the replacement helper functions
// `Uint128(Low|High)64()` and `MakeUint128()` are made.
//
// A `uint128` supports the following:
//
//   * Implicit construction from integral types
//   * Explicit conversion to integral types
//
// Additionally, if your compiler supports `__int128`, `uint128` is
// interoperable with that type. (Abseil checks for this compatibility through
// the `ABSL_HAVE_INTRINSIC_INT128` macro.)
//
// However, a `uint128` differs from intrinsic integral types in the following
// ways:
//
//   * Errors on implicit conversions that does not preserve value (such as
//     loss of precision when converting to float values).
//   * Requires explicit construction from and conversion to floating point
//     types.
//   * Conversion to integral types requires an explicit static_cast() to
//     mimic use of the `-Wnarrowing` compiler flag.
//
// Example:
//
//     float y = kuint128max; // Error. uint128 cannot be implicitly converted
//                            // to float.
//
//     uint128 v;
//     uint64_t i = v                          // Error
//     uint64_t i = static_cast<uint64_t>(v)   // OK
//
class alignas(16) uint128 {
 public:
  uint128() = default;

  // Constructors from arithmetic types
  constexpr uint128(int v);                 // NOLINT(runtime/explicit)
  constexpr uint128(unsigned int v);        // NOLINT(runtime/explicit)
  constexpr uint128(long v);                // NOLINT(runtime/int)
  constexpr uint128(unsigned long v);       // NOLINT(runtime/int)
  constexpr uint128(long long v);           // NOLINT(runtime/int)
  constexpr uint128(unsigned long long v);  // NOLINT(runtime/int)
#ifdef ABSL_HAVE_INTRINSIC_INT128
  constexpr uint128(__int128 v);           // NOLINT(runtime/explicit)
  constexpr uint128(unsigned __int128 v);  // NOLINT(runtime/explicit)
#endif  // ABSL_HAVE_INTRINSIC_INT128
  explicit uint128(float v);        // NOLINT(runtime/explicit)
  explicit uint128(double v);       // NOLINT(runtime/explicit)
  explicit uint128(long double v);  // NOLINT(runtime/explicit)

  // Assignment operators from arithmetic types
  uint128& operator=(int v);
  uint128& operator=(unsigned int v);
  uint128& operator=(long v);                // NOLINT(runtime/int)
  uint128& operator=(unsigned long v);       // NOLINT(runtime/int)
  uint128& operator=(long long v);           // NOLINT(runtime/int)
  uint128& operator=(unsigned long long v);  // NOLINT(runtime/int)
#ifdef ABSL_HAVE_INTRINSIC_INT128
  uint128& operator=(__int128 v);
  uint128& operator=(unsigned __int128 v);
#endif  // ABSL_HAVE_INTRINSIC_INT128

  // Conversion operators to other arithmetic types
  constexpr explicit operator bool() const;
  constexpr explicit operator char() const;
  constexpr explicit operator signed char() const;
  constexpr explicit operator unsigned char() const;
  constexpr explicit operator char16_t() const;
  constexpr explicit operator char32_t() const;
  constexpr explicit operator wchar_t() const;
  constexpr explicit operator short() const;  // NOLINT(runtime/int)
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator unsigned short() const;
  constexpr explicit operator int() const;
  constexpr explicit operator unsigned int() const;
  constexpr explicit operator long() const;  // NOLINT(runtime/int)
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator unsigned long() const;
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator long long() const;
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator unsigned long long() const;
#ifdef ABSL_HAVE_INTRINSIC_INT128
  constexpr explicit operator __int128() const;
  constexpr explicit operator unsigned __int128() const;
#endif  // ABSL_HAVE_INTRINSIC_INT128
  explicit operator float() const;
  explicit operator double() const;
  explicit operator long double() const;

  // Trivial copy constructor, assignment operator and destructor.

  // Arithmetic operators.
  uint128& operator+=(const uint128& other);
  uint128& operator-=(const uint128& other);
  uint128& operator*=(const uint128& other);
  // Long division/modulo for uint128.
  uint128& operator/=(const uint128& other);
  uint128& operator%=(const uint128& other);
  uint128 operator++(int);
  uint128 operator--(int);
  uint128& operator<<=(int);
  uint128& operator>>=(int);
  uint128& operator&=(const uint128& other);
  uint128& operator|=(const uint128& other);
  uint128& operator^=(const uint128& other);
  uint128& operator++();
  uint128& operator--();

  // Uint128Low64()
  //
  // Returns the lower 64-bit value of a `uint128` value.
  friend uint64_t Uint128Low64(const uint128& v);

  // Uint128High64()
  //
  // Returns the higher 64-bit value of a `uint128` value.
  friend uint64_t Uint128High64(const uint128& v);

  // MakeUInt128()
  //
  // Constructs a `uint128` numeric value from two 64-bit unsigned integers.
  // Note that this factory function is the only way to construct a `uint128`
  // from integer values greater than 2^64.
  //
  // Example:
  //
  //   absl::uint128 big = absl::MakeUint128(1, 0);
  friend constexpr uint128 MakeUint128(uint64_t top, uint64_t bottom);

 private:
  constexpr uint128(uint64_t top, uint64_t bottom);

  // TODO(strel) Update implementation to use __int128 once all users of
  // uint128 are fixed to not depend on alignof(uint128) == 8. Also add
  // alignas(16) to class definition to keep alignment consistent across
  // platforms.
#if defined(ABSL_IS_LITTLE_ENDIAN)
  uint64_t lo_;
  uint64_t hi_;
#elif defined(ABSL_IS_BIG_ENDIAN)
  uint64_t hi_;
  uint64_t lo_;
#else  // byte order
#error "Unsupported byte order: must be little-endian or big-endian."
#endif  // byte order
};

extern const uint128 kuint128max;

// allow uint128 to be logged
extern std::ostream& operator<<(std::ostream& o, const uint128& b);

// TODO(strel) add operator>>(std::istream&, uint128&)

// Methods to access low and high pieces of 128-bit value.
uint64_t Uint128Low64(const uint128& v);
uint64_t Uint128High64(const uint128& v);

// TODO(absl-team): Implement signed 128-bit type

// --------------------------------------------------------------------------
//                      Implementation details follow
// --------------------------------------------------------------------------

inline constexpr uint128 MakeUint128(uint64_t top, uint64_t bottom) {
  return uint128(top, bottom);
}

// Assignment from integer types.

inline uint128& uint128::operator=(int v) {
  return *this = uint128(v);
}

inline uint128& uint128::operator=(unsigned int v) {
  return *this = uint128(v);
}

inline uint128& uint128::operator=(long v) {  // NOLINT(runtime/int)
  return *this = uint128(v);
}

// NOLINTNEXTLINE(runtime/int)
inline uint128& uint128::operator=(unsigned long v) {
  return *this = uint128(v);
}

// NOLINTNEXTLINE(runtime/int)
inline uint128& uint128::operator=(long long v) {
  return *this = uint128(v);
}

// NOLINTNEXTLINE(runtime/int)
inline uint128& uint128::operator=(unsigned long long v) {
  return *this = uint128(v);
}

#ifdef ABSL_HAVE_INTRINSIC_INT128
inline uint128& uint128::operator=(__int128 v) {
  return *this = uint128(v);
}

inline uint128& uint128::operator=(unsigned __int128 v) {
  return *this = uint128(v);
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

// Shift and arithmetic operators.

inline uint128 operator<<(const uint128& lhs, int amount) {
  return uint128(lhs) <<= amount;
}

inline uint128 operator>>(const uint128& lhs, int amount) {
  return uint128(lhs) >>= amount;
}

inline uint128 operator+(const uint128& lhs, const uint128& rhs) {
  return uint128(lhs) += rhs;
}

inline uint128 operator-(const uint128& lhs, const uint128& rhs) {
  return uint128(lhs) -= rhs;
}

inline uint128 operator*(const uint128& lhs, const uint128& rhs) {
  return uint128(lhs) *= rhs;
}

inline uint128 operator/(const uint128& lhs, const uint128& rhs) {
  return uint128(lhs) /= rhs;
}

inline uint128 operator%(const uint128& lhs, const uint128& rhs) {
  return uint128(lhs) %= rhs;
}

inline uint64_t Uint128Low64(const uint128& v) { return v.lo_; }

inline uint64_t Uint128High64(const uint128& v) { return v.hi_; }

// Constructors from integer types.

#if defined(ABSL_IS_LITTLE_ENDIAN)

inline constexpr uint128::uint128(uint64_t top, uint64_t bottom)
    : lo_(bottom), hi_(top) {}

inline constexpr uint128::uint128(int v)
    : lo_(v), hi_(v < 0 ? std::numeric_limits<uint64_t>::max() : 0) {}
inline constexpr uint128::uint128(long v)  // NOLINT(runtime/int)
    : lo_(v), hi_(v < 0 ? std::numeric_limits<uint64_t>::max() : 0) {}
inline constexpr uint128::uint128(long long v)  // NOLINT(runtime/int)
    : lo_(v), hi_(v < 0 ? std::numeric_limits<uint64_t>::max() : 0) {}

inline constexpr uint128::uint128(unsigned int v) : lo_(v), hi_(0) {}
// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::uint128(unsigned long v) : lo_(v), hi_(0) {}
// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::uint128(unsigned long long v)
    : lo_(v), hi_(0) {}

#ifdef ABSL_HAVE_INTRINSIC_INT128
inline constexpr uint128::uint128(__int128 v)
    : lo_(static_cast<uint64_t>(v & ~uint64_t{0})),
      hi_(static_cast<uint64_t>(static_cast<unsigned __int128>(v) >> 64)) {}
inline constexpr uint128::uint128(unsigned __int128 v)
    : lo_(static_cast<uint64_t>(v & ~uint64_t{0})),
      hi_(static_cast<uint64_t>(v >> 64)) {}
#endif  // ABSL_HAVE_INTRINSIC_INT128

#elif defined(ABSL_IS_BIG_ENDIAN)

inline constexpr uint128::uint128(uint64_t top, uint64_t bottom)
    : hi_(top), lo_(bottom) {}

inline constexpr uint128::uint128(int v)
    : hi_(v < 0 ? std::numeric_limits<uint64_t>::max() : 0), lo_(v) {}
inline constexpr uint128::uint128(long v)  // NOLINT(runtime/int)
    : hi_(v < 0 ? std::numeric_limits<uint64_t>::max() : 0), lo_(v) {}
inline constexpr uint128::uint128(long long v)  // NOLINT(runtime/int)
    : hi_(v < 0 ? std::numeric_limits<uint64_t>::max() : 0), lo_(v) {}

inline constexpr uint128::uint128(unsigned int v) : hi_(0), lo_(v) {}
// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::uint128(unsigned long v) : hi_(0), lo_(v) {}
// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::uint128(unsigned long long v)
    : hi_(0), lo_(v) {}

#ifdef ABSL_HAVE_INTRINSIC_INT128
inline constexpr uint128::uint128(__int128 v)
    : hi_(static_cast<uint64_t>(static_cast<unsigned __int128>(v) >> 64)),
      lo_(static_cast<uint64_t>(v & ~uint64_t{0})) {}
inline constexpr uint128::uint128(unsigned __int128 v)
    : hi_(static_cast<uint64_t>(v >> 64)),
      lo_(static_cast<uint64_t>(v & ~uint64_t{0})) {}
#endif  // ABSL_HAVE_INTRINSIC_INT128

#else  // byte order
#error "Unsupported byte order: must be little-endian or big-endian."
#endif  // byte order

// Conversion operators to integer types.

inline constexpr uint128::operator bool() const {
  return lo_ || hi_;
}

inline constexpr uint128::operator char() const {
  return static_cast<char>(lo_);
}

inline constexpr uint128::operator signed char() const {
  return static_cast<signed char>(lo_);
}

inline constexpr uint128::operator unsigned char() const {
  return static_cast<unsigned char>(lo_);
}

inline constexpr uint128::operator char16_t() const {
  return static_cast<char16_t>(lo_);
}

inline constexpr uint128::operator char32_t() const {
  return static_cast<char32_t>(lo_);
}

inline constexpr uint128::operator wchar_t() const {
  return static_cast<wchar_t>(lo_);
}

// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::operator short() const {
  return static_cast<short>(lo_);  // NOLINT(runtime/int)
}

// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::operator unsigned short() const {
  return static_cast<unsigned short>(lo_);  // NOLINT(runtime/int)
}

inline constexpr uint128::operator int() const {
  return static_cast<int>(lo_);
}

inline constexpr uint128::operator unsigned int() const {
  return static_cast<unsigned int>(lo_);
}

// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::operator long() const {
  return static_cast<long>(lo_);  // NOLINT(runtime/int)
}

// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::operator unsigned long() const {
  return static_cast<unsigned long>(lo_);  // NOLINT(runtime/int)
}

// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::operator long long() const {
  return static_cast<long long>(lo_);  // NOLINT(runtime/int)
}

// NOLINTNEXTLINE(runtime/int)
inline constexpr uint128::operator unsigned long long() const {
  return static_cast<unsigned long long>(lo_);  // NOLINT(runtime/int)
}

#ifdef ABSL_HAVE_INTRINSIC_INT128
inline constexpr uint128::operator __int128() const {
  return (static_cast<__int128>(hi_) << 64) + lo_;
}

inline constexpr uint128::operator unsigned __int128() const {
  return (static_cast<unsigned __int128>(hi_) << 64) + lo_;
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

// Conversion operators to floating point types.

inline uint128::operator float() const {
  return static_cast<float>(lo_) + std::ldexp(static_cast<float>(hi_), 64);
}

inline uint128::operator double() const {
  return static_cast<double>(lo_) + std::ldexp(static_cast<double>(hi_), 64);
}

inline uint128::operator long double() const {
  return static_cast<long double>(lo_) +
         std::ldexp(static_cast<long double>(hi_), 64);
}

// Comparison operators.

inline bool operator==(const uint128& lhs, const uint128& rhs) {
  return (Uint128Low64(lhs) == Uint128Low64(rhs) &&
          Uint128High64(lhs) == Uint128High64(rhs));
}

inline bool operator!=(const uint128& lhs, const uint128& rhs) {
  return !(lhs == rhs);
}

inline bool operator<(const uint128& lhs, const uint128& rhs) {
  return (Uint128High64(lhs) == Uint128High64(rhs))
             ? (Uint128Low64(lhs) < Uint128Low64(rhs))
             : (Uint128High64(lhs) < Uint128High64(rhs));
}

inline bool operator>(const uint128& lhs, const uint128& rhs) {
  return (Uint128High64(lhs) == Uint128High64(rhs))
             ? (Uint128Low64(lhs) > Uint128Low64(rhs))
             : (Uint128High64(lhs) > Uint128High64(rhs));
}

inline bool operator<=(const uint128& lhs, const uint128& rhs) {
  return (Uint128High64(lhs) == Uint128High64(rhs))
             ? (Uint128Low64(lhs) <= Uint128Low64(rhs))
             : (Uint128High64(lhs) <= Uint128High64(rhs));
}

inline bool operator>=(const uint128& lhs, const uint128& rhs) {
  return (Uint128High64(lhs) == Uint128High64(rhs))
             ? (Uint128Low64(lhs) >= Uint128Low64(rhs))
             : (Uint128High64(lhs) >= Uint128High64(rhs));
}

// Unary operators.

inline uint128 operator-(const uint128& val) {
  const uint64_t hi_flip = ~Uint128High64(val);
  const uint64_t lo_flip = ~Uint128Low64(val);
  const uint64_t lo_add = lo_flip + 1;
  if (lo_add < lo_flip) {
    return MakeUint128(hi_flip + 1, lo_add);
  }
  return MakeUint128(hi_flip, lo_add);
}

inline bool operator!(const uint128& val) {
  return !Uint128High64(val) && !Uint128Low64(val);
}

// Logical operators.

inline uint128 operator~(const uint128& val) {
  return MakeUint128(~Uint128High64(val), ~Uint128Low64(val));
}

inline uint128 operator|(const uint128& lhs, const uint128& rhs) {
  return MakeUint128(Uint128High64(lhs) | Uint128High64(rhs),
                           Uint128Low64(lhs) | Uint128Low64(rhs));
}

inline uint128 operator&(const uint128& lhs, const uint128& rhs) {
  return MakeUint128(Uint128High64(lhs) & Uint128High64(rhs),
                           Uint128Low64(lhs) & Uint128Low64(rhs));
}

inline uint128 operator^(const uint128& lhs, const uint128& rhs) {
  return MakeUint128(Uint128High64(lhs) ^ Uint128High64(rhs),
                           Uint128Low64(lhs) ^ Uint128Low64(rhs));
}

inline uint128& uint128::operator|=(const uint128& other) {
  hi_ |= other.hi_;
  lo_ |= other.lo_;
  return *this;
}

inline uint128& uint128::operator&=(const uint128& other) {
  hi_ &= other.hi_;
  lo_ &= other.lo_;
  return *this;
}

inline uint128& uint128::operator^=(const uint128& other) {
  hi_ ^= other.hi_;
  lo_ ^= other.lo_;
  return *this;
}

// Shift and arithmetic assign operators.

inline uint128& uint128::operator<<=(int amount) {
  // Shifts of >= 128 are undefined.
  assert(amount < 128);

  // uint64_t shifts of >= 64 are undefined, so we will need some
  // special-casing.
  if (amount < 64) {
    if (amount != 0) {
      hi_ = (hi_ << amount) | (lo_ >> (64 - amount));
      lo_ = lo_ << amount;
    }
  } else {
    hi_ = lo_ << (amount - 64);
    lo_ = 0;
  }
  return *this;
}

inline uint128& uint128::operator>>=(int amount) {
  // Shifts of >= 128 are undefined.
  assert(amount < 128);

  // uint64_t shifts of >= 64 are undefined, so we will need some
  // special-casing.
  if (amount < 64) {
    if (amount != 0) {
      lo_ = (lo_ >> amount) | (hi_ << (64 - amount));
      hi_ = hi_ >> amount;
    }
  } else {
    lo_ = hi_ >> (amount - 64);
    hi_ = 0;
  }
  return *this;
}

inline uint128& uint128::operator+=(const uint128& other) {
  hi_ += other.hi_;
  uint64_t lolo = lo_ + other.lo_;
  if (lolo < lo_)
    ++hi_;
  lo_ = lolo;
  return *this;
}

inline uint128& uint128::operator-=(const uint128& other) {
  hi_ -= other.hi_;
  if (other.lo_ > lo_) --hi_;
  lo_ -= other.lo_;
  return *this;
}

inline uint128& uint128::operator*=(const uint128& other) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  // TODO(strel) Remove once alignment issues are resolved and unsigned __int128
  // can be used for uint128 storage.
  *this = static_cast<unsigned __int128>(*this) *
          static_cast<unsigned __int128>(other);
  return *this;
#else   // ABSL_HAVE_INTRINSIC128
  uint64_t a96 = hi_ >> 32;
  uint64_t a64 = hi_ & 0xffffffff;
  uint64_t a32 = lo_ >> 32;
  uint64_t a00 = lo_ & 0xffffffff;
  uint64_t b96 = other.hi_ >> 32;
  uint64_t b64 = other.hi_ & 0xffffffff;
  uint64_t b32 = other.lo_ >> 32;
  uint64_t b00 = other.lo_ & 0xffffffff;
  // multiply [a96 .. a00] x [b96 .. b00]
  // terms higher than c96 disappear off the high side
  // terms c96 and c64 are safe to ignore carry bit
  uint64_t c96 = a96 * b00 + a64 * b32 + a32 * b64 + a00 * b96;
  uint64_t c64 = a64 * b00 + a32 * b32 + a00 * b64;
  this->hi_ = (c96 << 32) + c64;
  this->lo_ = 0;
  // add terms after this one at a time to capture carry
  *this += uint128(a32 * b00) << 32;
  *this += uint128(a00 * b32) << 32;
  *this += a00 * b00;
  return *this;
#endif  // ABSL_HAVE_INTRINSIC128
}

// Increment/decrement operators.

inline uint128 uint128::operator++(int) {
  uint128 tmp(*this);
  *this += 1;
  return tmp;
}

inline uint128 uint128::operator--(int) {
  uint128 tmp(*this);
  *this -= 1;
  return tmp;
}

inline uint128& uint128::operator++() {
  *this += 1;
  return *this;
}

inline uint128& uint128::operator--() {
  *this -= 1;
  return *this;
}

}  // namespace absl

#endif  // ABSL_NUMERIC_INT128_H_
