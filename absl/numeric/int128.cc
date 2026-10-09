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

#include "absl/numeric/int128.h"

#include <stddef.h>

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <limits>
#include <ostream>  // NOLINT(readability/streams)
#include <sstream>
#include <string>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/numeric/bits.h"

#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64) && \
    !defined(_M_ARM64EC) && _MSC_VER >= 1920
// The _udiv128 intrinsic is available starting in Visual Studio 2019 RTM.
// https://learn.microsoft.com/en-us/cpp/intrinsics/udiv128?view=msvc-170
#include <immintrin.h>
#pragma intrinsic(_udiv128)
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace {

// If the result of dividing a uint128 by a uint64 fits within 64 bits,
// the division can be implemented efficiently using an intrinsic instruction.
// Expects the quotient to fit in 64 bits.
inline void DivModImpl(uint128 dividend, uint64_t divisor,
                       uint64_t* quotient_ret, uint64_t* remainder_ret) {
  uint64_t high = Uint128High64(dividend);
  uint64_t low = Uint128Low64(dividend);
  ABSL_ASSUME(divisor != 0);
  ABSL_ASSUME(high < divisor);

#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
  uint64_t qt, rem;
  __asm__ __volatile__("divq %[v]"
                       : "=a"(qt), "=d"(rem)
                       : [v] "r"(divisor), "a"(low), "d"(high));
  *quotient_ret = qt;
  *remainder_ret = rem;
#elif defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64) && \
    !defined(_M_ARM64EC) && _MSC_VER >= 1920
  *quotient_ret = _udiv128(high, low, divisor, remainder_ret);
#else
  // Software fallback for targets without a 128-by-64 division instruction.
  // Uses Knuth's Algorithm D in base 2^32.

  if (high == 0) {
    *quotient_ret = low / divisor;
    *remainder_ret = low % divisor;
    return;
  }

  if ((divisor & (divisor - 1)) == 0) {
    // Power of two: shift and mask instead of dividing.
    const auto k = countr_zero(divisor);
    *quotient_ret = (((high << 1) << (63 - k)) | (low >> k));
    *remainder_ret = low & (divisor - 1);
    return;
  }

  const uint64_t v1 = divisor >> 32;
  if (v1 == 0) {
    // The divisor fits in 32 bits: two 64-bit divisions suffice.
    uint64_t dividend1 = (high << 32) | (low >> 32);
    uint64_t dividend2 = ((dividend1 % divisor) << 32) | (low & 0xffffffff);
    *quotient_ret = ((dividend1 / divisor) << 32) | (dividend2 / divisor);
    *remainder_ret = dividend2 % divisor;
    return;
  }

  const auto s = countl_zero(static_cast<uint32_t>(v1));
  const uint64_t dnorm = divisor << s;
  const uint32_t v1n = dnorm >> 32;
  const uint64_t v0n = dnorm & 0xffffffff;

  // Divides a 3-half-word value by the 2-half-word dnorm (Knuth steps D3-D4).
  auto div3by2 = [dnorm, v1n, v0n](uint64_t top, uint64_t a0, uint64_t& rem) {
    uint64_t qhat = top / v1n;
    const uint64_t rhat = top % v1n;
    // qhat overestimates by at most 2, so one correction suffices.
    const uint64_t c1 = qhat * v0n;
    const uint64_t c2 = (rhat << 32) + a0;
    if (c1 > c2) qhat -= (c1 - c2 > dnorm) ? 2 : 1;
    rem = ((top << 32) + a0) - qhat * dnorm;
    return qhat;
  };

  const uint64_t nh = (high << s) | ((low >> 1) >> (63 - s));
  const uint64_t nl = low << s;
  uint64_t rem;
  if (high < v1) {
    // Since high < v1, the quotient is less than 2^32,
    // so only one quotient digit is needed.
    *quotient_ret = div3by2((nh << 32) | (nl >> 32), nl & 0xffffffff, rem);
  } else {
    const uint64_t q1 = div3by2(nh, nl >> 32, rem);
    const uint64_t q0 = div3by2(rem, nl & 0xffffffff, rem);
    *quotient_ret = (q1 << 32) | q0;
  }
  *remainder_ret = rem >> s;
#endif
}

// Long division/modulo for uint128.
inline void DivModImpl(uint128 dividend, uint128 divisor, uint128* quotient_ret,
                       uint128* remainder_ret) {
  uint64_t dividend_high = Uint128High64(dividend);
  uint64_t dividend_low = Uint128Low64(dividend);
  uint64_t divisor_high = Uint128High64(divisor);
  uint64_t divisor_low = Uint128Low64(divisor);
  assert(divisor_high != 0 || divisor_low != 0);

  if (divisor_high == 0) {
    if (dividend_high >= divisor_low) {
      // Long division/modulo
      uint64_t qt_high = dividend_high / divisor_low;
      uint64_t rem_tmp = dividend_high % divisor_low;
      uint64_t qt_low;
      uint64_t rem_result;
      DivModImpl(MakeUint128(rem_tmp, dividend_low), divisor_low, &qt_low,
                 &rem_result);
      *quotient_ret = MakeUint128(qt_high, qt_low);
      *remainder_ret = MakeUint128(0, rem_result);

    } else {
      // If the quotient fits within 64 bits
      uint64_t qt_result;
      uint64_t rem_result;
      DivModImpl(MakeUint128(dividend_high, dividend_low), divisor_low,
                 &qt_result, &rem_result);
      *quotient_ret = MakeUint128(0, qt_result);
      *remainder_ret = MakeUint128(0, rem_result);
    }
  } else if (dividend_high >= divisor_high) {
    int shift = countl_zero(divisor_high);
    uint64_t xhigh = (dividend_high >> 1) >> (63 - shift);
    uint64_t xlow =
        (dividend_high << shift) | ((dividend_low >> 1) >> (63 - shift));
    uint64_t yhigh =
        (divisor_high << shift) | ((divisor_low >> 1) >> (63 - shift));
    uint64_t ylow = divisor_low << shift;
    uint64_t threshold = (uint64_t(2) << shift) - 1;

    uint64_t qt;
    uint64_t rem;
    DivModImpl(MakeUint128(xhigh, xlow), yhigh, &qt, &rem);
    // The estimate is off by at most one, and only when rem is tiny.
    if (rem <= threshold) {
      uint128 tmp = int128_internal::Mul64x64(qt, ylow);
      uint64_t thigh = Uint128High64(tmp);
      uint64_t tlow = Uint128Low64(tmp);
      qt -= (rem < thigh || (rem == thigh && (dividend_low << shift) < tlow));
    }

    *quotient_ret = MakeUint128(0, qt);
    // remainder = dividend - qt * divisor, subtracted in 64-bit halves.
    uint128 floored = int128_internal::Mul64x64(qt, divisor_low);
    uint64_t floored_high = Uint128High64(floored);
    uint64_t floored_low = Uint128Low64(floored);
    uint64_t rem_low = dividend_low - floored_low;
    uint64_t rem_high = dividend_high - floored_high - divisor_high * qt -
                        (dividend_low < floored_low);
    *remainder_ret = MakeUint128(rem_high, rem_low);
  } else {
    // dividend < divisor
    *quotient_ret = 0;
    *remainder_ret = MakeUint128(dividend_high, dividend_low);
  }
}

template <typename T>
uint128 MakeUint128FromFloat(T v) {
  static_assert(std::is_floating_point_v<T>);

  // Rounding behavior is towards zero, same as for built-in types.

  // Undefined behavior if v is NaN or cannot fit into uint128.
  assert(std::isfinite(v) && v > -1 &&
         (std::numeric_limits<T>::max_exponent <= 128 ||
          v < std::ldexp(static_cast<T>(1), 128)));

  if (v >= std::ldexp(static_cast<T>(1), 64)) {
    uint64_t hi = static_cast<uint64_t>(std::ldexp(v, -64));
    uint64_t lo = static_cast<uint64_t>(v - std::ldexp(static_cast<T>(hi), 64));
    return MakeUint128(hi, lo);
  }

  return MakeUint128(0, static_cast<uint64_t>(v));
}

#if defined(__clang__) && (__clang_major__ < 9) && !defined(__SSE3__)
// Workaround for clang bug: https://bugs.llvm.org/show_bug.cgi?id=38289
// Casting from long double to uint64_t is miscompiled and drops bits.
// It is more work, so only use when we need the workaround.
uint128 MakeUint128FromFloat(long double v) {
  // Go 50 bits at a time, that fits in a double
  static_assert(std::numeric_limits<double>::digits >= 50);
  static_assert(std::numeric_limits<long double>::digits <= 150);
  // Undefined behavior if v is not finite or cannot fit into uint128.
  assert(std::isfinite(v) && v > -1 && v < std::ldexp(1.0L, 128));

  v = std::ldexp(v, -100);
  uint64_t w0 = static_cast<uint64_t>(static_cast<double>(std::trunc(v)));
  v = std::ldexp(v - static_cast<double>(w0), 50);
  uint64_t w1 = static_cast<uint64_t>(static_cast<double>(std::trunc(v)));
  v = std::ldexp(v - static_cast<double>(w1), 50);
  uint64_t w2 = static_cast<uint64_t>(static_cast<double>(std::trunc(v)));
  return (static_cast<uint128>(w0) << 100) | (static_cast<uint128>(w1) << 50) |
         static_cast<uint128>(w2);
}
#endif  // __clang__ && (__clang_major__ < 9) && !__SSE3__
}  // namespace

uint128::uint128(float v) : uint128(MakeUint128FromFloat(v)) {}
uint128::uint128(double v) : uint128(MakeUint128FromFloat(v)) {}
uint128::uint128(long double v) : uint128(MakeUint128FromFloat(v)) {}

#if !defined(ABSL_HAVE_INTRINSIC_INT128)
uint128 operator/(uint128 lhs, uint128 rhs) {
  uint128 quotient = 0;
  uint128 remainder = 0;
  DivModImpl(lhs, rhs, &quotient, &remainder);
  return quotient;
}

uint128 operator%(uint128 lhs, uint128 rhs) {
  uint128 quotient = 0;
  uint128 remainder = 0;
  DivModImpl(lhs, rhs, &quotient, &remainder);
  return remainder;
}
#endif  // !defined(ABSL_HAVE_INTRINSIC_INT128)

namespace {

std::string Uint128ToFormattedString(uint128 v, std::ios_base::fmtflags flags) {
  // Select a divisor which is the largest power of the base < 2^64.
  uint128 div;
  int div_base_log;
  switch (flags & std::ios::basefield) {
    case std::ios::hex:
      div = 0x1000000000000000;  // 16^15
      div_base_log = 15;
      break;
    case std::ios::oct:
      div = 01000000000000000000000;  // 8^21
      div_base_log = 21;
      break;
    default:  // std::ios::dec
      div = 10000000000000000000u;  // 10^19
      div_base_log = 19;
      break;
  }

  // Now piece together the uint128 representation from three chunks of the
  // original value, each less than "div" and therefore representable as a
  // uint64_t.
  std::ostringstream os;
  std::ios_base::fmtflags copy_mask =
      std::ios::basefield | std::ios::showbase | std::ios::uppercase;
  os.setf(flags & copy_mask, copy_mask);
  uint128 high = v;
  uint128 low;
  DivModImpl(high, div, &high, &low);
  uint128 mid;
  DivModImpl(high, div, &high, &mid);
  if (Uint128Low64(high) != 0) {
    os << Uint128Low64(high);
    os << std::noshowbase << std::setfill('0') << std::setw(div_base_log);
    os << Uint128Low64(mid);
    os << std::setw(div_base_log);
  } else if (Uint128Low64(mid) != 0) {
    os << Uint128Low64(mid);
    os << std::noshowbase << std::setfill('0') << std::setw(div_base_log);
  }
  os << Uint128Low64(low);
  return os.str();
}

}  // namespace

std::string uint128::ToString() const {
  return Uint128ToFormattedString(*this, std::ios_base::dec);
}

std::ostream& operator<<(std::ostream& os, uint128 v) {
  std::ios_base::fmtflags flags = os.flags();
  std::string rep = Uint128ToFormattedString(v, flags);

  // Add the requisite padding.
  std::streamsize width = os.width(0);
  if (static_cast<size_t>(width) > rep.size()) {
    const size_t count = static_cast<size_t>(width) - rep.size();
    std::ios::fmtflags adjustfield = flags & std::ios::adjustfield;
    if (adjustfield == std::ios::left) {
      rep.append(count, os.fill());
    } else if (adjustfield == std::ios::internal &&
               (flags & std::ios::showbase) &&
               (flags & std::ios::basefield) == std::ios::hex && v != 0) {
      rep.insert(size_t{2}, count, os.fill());
    } else {
      rep.insert(size_t{0}, count, os.fill());
    }
  }

  return os << rep;
}

namespace {

uint128 UnsignedAbsoluteValue(int128 v) {
  // Cast to uint128 before possibly negating because -Int128Min() is undefined.
  return Int128High64(v) < 0 ? -uint128(v) : uint128(v);
}

}  // namespace

#if !defined(ABSL_HAVE_INTRINSIC_INT128)
namespace {

template <typename T>
int128 MakeInt128FromFloat(T v) {
  // Conversion when v is NaN or cannot fit into int128 would be undefined
  // behavior if using an intrinsic 128-bit integer.
  assert(std::isfinite(v) && (std::numeric_limits<T>::max_exponent <= 127 ||
                              (v >= -std::ldexp(static_cast<T>(1), 127) &&
                               v < std::ldexp(static_cast<T>(1), 127))));

  // We must convert the absolute value and then negate as needed, because
  // floating point types are typically sign-magnitude. Otherwise, the
  // difference between the high and low 64 bits when interpreted as two's
  // complement overwhelms the precision of the mantissa.
  uint128 result = v < 0 ? -MakeUint128FromFloat(-v) : MakeUint128FromFloat(v);
  return MakeInt128(int128_internal::BitCastToSigned(Uint128High64(result)),
                    Uint128Low64(result));
}

}  // namespace

int128::int128(float v) : int128(MakeInt128FromFloat(v)) {}
int128::int128(double v) : int128(MakeInt128FromFloat(v)) {}
int128::int128(long double v) : int128(MakeInt128FromFloat(v)) {}

int128 operator/(int128 lhs, int128 rhs) {
  assert(lhs != Int128Min() || rhs != -1);  // UB on two's complement.

  uint128 quotient = 0;
  uint128 remainder = 0;
  DivModImpl(UnsignedAbsoluteValue(lhs), UnsignedAbsoluteValue(rhs),
             &quotient, &remainder);
  if ((Int128High64(lhs) < 0) != (Int128High64(rhs) < 0)) quotient = -quotient;
  return MakeInt128(int128_internal::BitCastToSigned(Uint128High64(quotient)),
                    Uint128Low64(quotient));
}

int128 operator%(int128 lhs, int128 rhs) {
  assert(lhs != Int128Min() || rhs != -1);  // UB on two's complement.

  uint128 quotient = 0;
  uint128 remainder = 0;
  DivModImpl(UnsignedAbsoluteValue(lhs), UnsignedAbsoluteValue(rhs),
             &quotient, &remainder);
  if (Int128High64(lhs) < 0) remainder = -remainder;
  return MakeInt128(int128_internal::BitCastToSigned(Uint128High64(remainder)),
                    Uint128Low64(remainder));
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

std::string int128::ToString() const {
  std::string rep;
  if (Int128High64(*this) < 0) rep = "-";
  rep.append(Uint128ToFormattedString(UnsignedAbsoluteValue(*this),
                                      std::ios_base::dec));
  return rep;
}

std::ostream& operator<<(std::ostream& os, int128 v) {
  std::ios_base::fmtflags flags = os.flags();
  std::string rep;

  // Add the sign if needed.
  bool print_as_decimal =
      (flags & std::ios::basefield) == std::ios::dec ||
      (flags & std::ios::basefield) == std::ios_base::fmtflags();
  if (print_as_decimal) {
    if (Int128High64(v) < 0) {
      rep = "-";
    } else if (flags & std::ios::showpos) {
      rep = "+";
    }
  }

  rep.append(Uint128ToFormattedString(
      print_as_decimal ? UnsignedAbsoluteValue(v) : uint128(v), os.flags()));

  // Add the requisite padding.
  std::streamsize width = os.width(0);
  if (static_cast<size_t>(width) > rep.size()) {
    const size_t count = static_cast<size_t>(width) - rep.size();
    switch (flags & std::ios::adjustfield) {
      case std::ios::left:
        rep.append(count, os.fill());
        break;
      case std::ios::internal:
        if (print_as_decimal && (rep[0] == '+' || rep[0] == '-')) {
          rep.insert(size_t{1}, count, os.fill());
        } else if ((flags & std::ios::basefield) == std::ios::hex &&
                   (flags & std::ios::showbase) && v != 0) {
          rep.insert(size_t{2}, count, os.fill());
        } else {
          rep.insert(size_t{0}, count, os.fill());
        }
        break;
      default:  // std::ios::right
        rep.insert(size_t{0}, count, os.fill());
        break;
    }
  }

  return os << rep;
}

ABSL_NAMESPACE_END
}  // namespace absl
