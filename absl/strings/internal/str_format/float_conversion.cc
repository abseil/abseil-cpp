#include "absl/strings/internal/str_format/float_conversion.h"

#include <string.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/internal/bits.h"
#include "absl/base/optimization.h"
#include "absl/meta/type_traits.h"
#include "absl/numeric/int128.h"
#include "absl/types/span.h"

namespace absl {
namespace str_format_internal {

namespace {

// Calculates `10 * (*v) + carry` and stores the result in `*v` and returns
// the carry.
template <typename Int>
inline Int MultiplyBy10WithCarry(Int *v, Int carry) {
  using NextInt = absl::conditional_t<sizeof(Int) == 4, uint64_t, uint128>;
  static_assert(sizeof(void *) >= sizeof(Int),
                "Don't want to use uint128 in 32-bit mode. It is too slow.");
  NextInt tmp = 10 * static_cast<NextInt>(*v) + carry;
  *v = static_cast<Int>(tmp);
  return static_cast<Int>(tmp >> (sizeof(Int) * 8));
}

// Calculates `(2^64 * carry + *v) / 10`.
// Stores the quotient in `*v` and returns the remainder.
// Requires: `0 <= carry <= 9`
inline uint64_t DivideBy10WithCarry(uint64_t *v, uint64_t carry) {
  constexpr uint64_t divisor = 10;
  // 2^64 / divisor = word_quotient + word_remainder / divisor
  constexpr uint64_t word_quotient = (uint64_t{1} << 63) / (divisor / 2);
  constexpr uint64_t word_remainder = uint64_t{} - word_quotient * divisor;

  const uint64_t mod = *v % divisor;
  const uint64_t next_carry = word_remainder * carry + mod;
  *v = *v / divisor + carry * word_quotient + next_carry / divisor;
  return next_carry % divisor;
}

int LeadingZeros(uint64_t v) { return base_internal::CountLeadingZeros64(v); }
int LeadingZeros(uint128 v) {
  auto high = static_cast<uint64_t>(v >> 64);
  auto low = static_cast<uint64_t>(v);
  return high != 0 ? base_internal::CountLeadingZeros64(high)
                   : 64 + base_internal::CountLeadingZeros64(low);
}

int TrailingZeros(uint64_t v) {
  return base_internal::CountTrailingZerosNonZero64(v);
}
int TrailingZeros(uint128 v) {
  auto high = static_cast<uint64_t>(v >> 64);
  auto low = static_cast<uint64_t>(v);
  return low == 0 ? 64 + base_internal::CountTrailingZerosNonZero64(high)
                  : base_internal::CountTrailingZerosNonZero64(low);
}

// The buffer must have an extra digit that is known to not need rounding.
// This is done below by having an extra '0' digit on the left.
void RoundUp(char *last_digit) {
  char *p = last_digit;
  while (*p == '9' || *p == '.') {
    if (*p == '9') *p = '0';
    --p;
  }
  ++*p;
}

void RoundToEven(char *last_digit) {
  char *p = last_digit;
  if (*p == '.') --p;
  if (*p % 2 == 1) RoundUp(p);
}

char *PrintIntegralDigitsFromRightDynamic(uint128 v, Span<uint32_t> array,
                                          int exp, char *p) {
  if (v == 0) {
    *--p = '0';
    return p;
  }

  int w = exp / 32;
  const int offset = exp % 32;
  // Left shift v by exp bits.
  array[w] = static_cast<uint32_t>(v << offset);
  for (v >>= (32 - offset); v; v >>= 32) array[++w] = static_cast<uint32_t>(v);

  // While we have more than one word available, go in chunks of 1e9.
  // We are guaranteed to have at least those many digits.
  // `w` holds the largest populated word, so keep it updated.
  while (w > 0) {
    uint32_t carry = 0;
    for (int i = w; i >= 0; --i) {
      uint64_t tmp = uint64_t{array[i]} + (uint64_t{carry} << 32);
      array[i] = tmp / uint64_t{1000000000};
      carry = tmp % uint64_t{1000000000};
    }
    // If the highest word is now empty, remove it from view.
    if (array[w] == 0) --w;

    for (int i = 0; i < 9; ++i, carry /= 10) {
      *--p = carry % 10 + '0';
    }
  }

  // Print the leftover of the last word.
  for (auto last = array[0]; last != 0; last /= 10) {
    *--p = last % 10 + '0';
  }

  return p;
}

struct FractionalResult {
  const char *end;
  int precision;
};

FractionalResult PrintFractionalDigitsDynamic(uint128 v, Span<uint32_t> array,
                                              char *p, int exp, int precision) {
  int w = exp / 32;
  const int offset = exp % 32;

  // Right shift `v` by `exp` bits.
  array[w] = static_cast<uint32_t>(v << (32 - offset));
  v >>= offset;
  // Make sure we don't overflow the array. We already calculated that non-zero
  // bits fit, so we might not have space for leading zero bits.
  for (int pos = w; v; v >>= 32) array[--pos] = static_cast<uint32_t>(v);

  // Multiply the whole sequence by 10.
  // On each iteration, the leftover carry word is the next digit.
  // `w` holds the largest populated word, so keep it updated.
  for (; w >= 0 && precision > 0; --precision) {
    uint32_t carry = 0;
    for (int i = w; i >= 0; --i) {
      carry = MultiplyBy10WithCarry(&array[i], carry);
    }
    // If the lowest word is now empty, remove it from view.
    if (array[w] == 0) --w;
    *p++ = carry + '0';
  }

  constexpr uint32_t threshold = 0x80000000;
  if (array[0] < threshold) {
    // We round down, so nothing to do.
  } else if (array[0] > threshold ||
             std::any_of(&array[1], &array[w + 1],
                         [](uint32_t word) { return word != 0; })) {
    RoundUp(p - 1);
  } else {
    RoundToEven(p - 1);
  }
  return {p, precision};
}

// Generic digit printer.
// `bits` determines how many bits of termporary space it needs for the
// calcualtions.
template <int bits, typename = void>
class DigitPrinter {
  static constexpr int kInts = (bits + 31) / 32;

 public:
  // Quick upper bound for the number of decimal digits we need.
  // This would be std::ceil(std::log10(std::pow(2, bits))), but that is not
  // constexpr.
  static constexpr int kDigits10 = 1 + (bits + 9) / 10 * 3 + bits / 900;
  using InputType = uint128;

  static char *PrintIntegralDigitsFromRight(InputType v, int exp, char *end) {
    std::array<uint32_t, kInts> array{};
    return PrintIntegralDigitsFromRightDynamic(v, absl::MakeSpan(array), exp,
                                               end);
  }

  static FractionalResult PrintFractionalDigits(InputType v, char *p, int exp,
                                                int precision) {
    std::array<uint32_t, kInts> array{};
    return PrintFractionalDigitsDynamic(v, absl::MakeSpan(array), p, exp,
                                        precision);
  }
};

// Specialiation for 64-bit working space.
// This is a performance optimization over the generic primary template.
// Only enabled in 64-bit platforms. The generic one is faster in 32-bit
// platforms.
template <int bits>
class DigitPrinter<bits, absl::enable_if_t<bits == 64 && (sizeof(void *) >=
                                                          sizeof(uint64_t))>> {
 public:
  static constexpr size_t kDigits10 = 20;
  using InputType = uint64_t;

  static char *PrintIntegralDigitsFromRight(uint64_t v, int exp, char *p) {
    v <<= exp;
    do {
      *--p = DivideBy10WithCarry(&v, 0) + '0';
    } while (v != 0);
    return p;
  }

  static FractionalResult PrintFractionalDigits(uint64_t v, char *p, int exp,
                                                int precision) {
    v <<= (64 - exp);
    while (precision > 0) {
      if (!v) return {p, precision};
      *p++ = MultiplyBy10WithCarry(&v, uint64_t{}) + '0';
      --precision;
    }

    // We need to round.
    if (v < 0x8000000000000000) {
      // We round down, so nothing to do.
    } else if (v > 0x8000000000000000) {
      // We round up.
      RoundUp(p - 1);
    } else {
      RoundToEven(p - 1);
    }

    assert(precision == 0);
    // Precision can only be zero here. Return a constant instead.
    return {p, 0};
  }
};

// Specialiation for 128-bit working space.
// This is a performance optimization over the generic primary template.
template <int bits>
class DigitPrinter<bits, absl::enable_if_t<bits == 128 && (sizeof(void *) >=
                                                           sizeof(uint64_t))>> {
 public:
  static constexpr size_t kDigits10 = 40;
  using InputType = uint128;

  static char *PrintIntegralDigitsFromRight(uint128 v, int exp, char *p) {
    v <<= exp;
    auto high = static_cast<uint64_t>(v >> 64);
    auto low = static_cast<uint64_t>(v);

    do {
      uint64_t carry = DivideBy10WithCarry(&high, 0);
      carry = DivideBy10WithCarry(&low, carry);
      *--p = carry + '0';
    } while (high != 0u);

    while (low != 0u) {
      *--p = DivideBy10WithCarry(&low, 0) + '0';
    }
    return p;
  }

  static FractionalResult PrintFractionalDigits(uint128 v, char *p, int exp,
                                                int precision) {
    v <<= (128 - exp);
    auto high = static_cast<uint64_t>(v >> 64);
    auto low = static_cast<uint64_t>(v);

    // While we have digits to print and `low` is not empty, do the long
    // multiplication.
    while (precision > 0 && low != 0) {
      uint64_t carry = MultiplyBy10WithCarry(&low, uint64_t{});
      carry = MultiplyBy10WithCarry(&high, carry);

      *p++ = carry + '0';
      --precision;
    }

    // Now `low` is empty, so use a faster approach for the rest of the digits.
    // This block is pretty much the same as the main loop for the 64-bit case
    // above.
    while (precision > 0) {
      if (!high) return {p, precision};
      *p++ = MultiplyBy10WithCarry(&high, uint64_t{}) + '0';
      --precision;
    }

    // We need to round.
    if (high < 0x8000000000000000) {
      // We round down, so nothing to do.
    } else if (high > 0x8000000000000000 || low != 0) {
      // We round up.
      RoundUp(p - 1);
    } else {
      RoundToEven(p - 1);
    }

    assert(precision == 0);
    // Precision can only be zero here. Return a constant instead.
    return {p, 0};
  }
};

struct FormatState {
  char sign_char;
  int precision;
  const ConversionSpec &conv;
  FormatSinkImpl *sink;
};

void FinalPrint(string_view data, int trailing_zeros,
                const FormatState &state) {
  if (state.conv.width() < 0) {
    // No width specified. Fast-path.
    if (state.sign_char != '\0') state.sink->Append(1, state.sign_char);
    state.sink->Append(data);
    state.sink->Append(trailing_zeros, '0');
    return;
  }

  int left_spaces = 0, zeros = 0, right_spaces = 0;
  int total_size = (state.sign_char != 0 ? 1 : 0) +
                   static_cast<int>(data.size()) + trailing_zeros;
  int missing_chars = std::max(state.conv.width() - total_size, 0);
  if (state.conv.flags().left) {
    right_spaces = missing_chars;
  } else if (state.conv.flags().zero) {
    zeros = missing_chars;
  } else {
    left_spaces = missing_chars;
  }

  state.sink->Append(left_spaces, ' ');
  if (state.sign_char != '\0') state.sink->Append(1, state.sign_char);
  state.sink->Append(zeros, '0');
  state.sink->Append(data);
  state.sink->Append(trailing_zeros, '0');
  state.sink->Append(right_spaces, ' ');
}

template <int num_bits, typename Int>
void FormatFPositiveExp(Int v, int exp, const FormatState &state) {
  using IntegralPrinter = DigitPrinter<num_bits>;
  char buffer[IntegralPrinter::kDigits10 + /* . */ 1];
  buffer[IntegralPrinter::kDigits10] = '.';

  const char *digits = IntegralPrinter::PrintIntegralDigitsFromRight(
      static_cast<typename IntegralPrinter::InputType>(v), exp,
      buffer + sizeof(buffer) - 1);
  size_t size = buffer + sizeof(buffer) - digits;

  // In `alt` mode (flag #) we keep the `.` even if there are no fractional
  // digits. In non-alt mode, we strip it.
  if (ABSL_PREDICT_FALSE(state.precision == 0 && !state.conv.flags().alt)) {
    --size;
  }

  FinalPrint(string_view(digits, size), state.precision, state);
}

template <int num_bits, typename Int>
void FormatFNegativeExp(Int v, int exp, const FormatState &state) {
  constexpr int input_bits = sizeof(Int) * 8;

  using IntegralPrinter = DigitPrinter<input_bits>;
  using FractionalPrinter = DigitPrinter<num_bits>;

  static constexpr size_t integral_size =
      1 + /* in case we need to round up an extra digit */
      IntegralPrinter::kDigits10 + 1;
  char buffer[integral_size + /* . */ 1 + num_bits];
  buffer[integral_size] = '.';
  char *const integral_digits_end = buffer + integral_size;
  char *integral_digits_start;
  char *const fractional_digits_start = buffer + integral_size + 1;

  if (exp < input_bits) {
    integral_digits_start = IntegralPrinter::PrintIntegralDigitsFromRight(
        v >> exp, 0, integral_digits_end);
  } else {
    integral_digits_start = integral_digits_end - 1;
    *integral_digits_start = '0';
  }

  // PrintFractionalDigits may pull a carried 1 all the way up through the
  // integral portion.
  integral_digits_start[-1] = '0';
  auto fractional_result = FractionalPrinter::PrintFractionalDigits(
      static_cast<typename FractionalPrinter::InputType>(v),
      fractional_digits_start, exp, state.precision);
  if (integral_digits_start[-1] != '0') --integral_digits_start;

  size_t size = fractional_result.end - integral_digits_start;

  // In `alt` mode (flag #) we keep the `.` even if there are no fractional
  // digits. In non-alt mode, we strip it.
  if (ABSL_PREDICT_FALSE(state.precision == 0 && !state.conv.flags().alt)) {
    --size;
  }
  FinalPrint(string_view(integral_digits_start, size),
             fractional_result.precision, state);
}

template <typename Int>
void FormatF(Int mantissa, int exp, const FormatState &state) {
  // Remove trailing zeros as they are not useful.
  // This helps use faster implementations/less stack space in some cases.
  if (mantissa != 0) {
    int trailing = TrailingZeros(mantissa);
    mantissa >>= trailing;
    exp += trailing;
  }

  // The table driven dispatch gives us two benefits: fast distpatch and
  // prevent inlining.
  // We must not inline any of the functions below (other than the ones for
  // 64-bit) to avoid blowing up this stack frame.

  if (exp >= 0) {
    // We will left shift the mantissa. Calculate how many bits we need.
    // Special case 64-bit as we will use a uint64_t for it. Use a table for the
    // rest and unconditionally use uint128.
    const int total_bits = sizeof(Int) * 8 - LeadingZeros(mantissa) + exp;

    if (total_bits <= 64) {
      return FormatFPositiveExp<64>(mantissa, exp, state);
    } else {
      using Formatter = void (*)(uint128, int, const FormatState &);
      static constexpr Formatter kFormatters[] = {
          FormatFPositiveExp<1 << 7>,  FormatFPositiveExp<1 << 8>,
          FormatFPositiveExp<1 << 9>,  FormatFPositiveExp<1 << 10>,
          FormatFPositiveExp<1 << 11>, FormatFPositiveExp<1 << 12>,
          FormatFPositiveExp<1 << 13>, FormatFPositiveExp<1 << 14>,
          FormatFPositiveExp<1 << 15>,
      };
      static constexpr int max_total_bits =
          sizeof(Int) * 8 + std::numeric_limits<long double>::max_exponent;
      assert(total_bits <= max_total_bits);
      static_assert(max_total_bits <= (1 << 15), "");
      const int log2 =
          64 - LeadingZeros((static_cast<uint64_t>(total_bits) - 1) / 128);
      assert(log2 < std::end(kFormatters) - std::begin(kFormatters));
      kFormatters[log2](mantissa, exp, state);
    }
  } else {
    exp = -exp;

    // We know we don't need more than Int itself for the integral part.
    // We need `precision` fractional digits, but there are at most `exp`
    // non-zero digits after the decimal point. The rest will be zeros.
    // Special case 64-bit as we will use a uint64_t for it. Use a table for the
    // rest and unconditionally use uint128.

    if (exp <= 64) {
      return FormatFNegativeExp<64>(mantissa, exp, state);
    } else {
      using Formatter = void (*)(uint128, int, const FormatState &);
      static constexpr Formatter kFormatters[] = {
          FormatFNegativeExp<1 << 7>,  FormatFNegativeExp<1 << 8>,
          FormatFNegativeExp<1 << 9>,  FormatFNegativeExp<1 << 10>,
          FormatFNegativeExp<1 << 11>, FormatFNegativeExp<1 << 12>,
          FormatFNegativeExp<1 << 13>, FormatFNegativeExp<1 << 14>};
      static_assert(
          -std::numeric_limits<long double>::min_exponent <= (1 << 14), "");
      const int log2 =
          64 - LeadingZeros((static_cast<uint64_t>(exp) - 1) / 128);
      assert(log2 < std::end(kFormatters) - std::begin(kFormatters));
      kFormatters[log2](mantissa, exp, state);
    }
  }
}

char *CopyStringTo(string_view v, char *out) {
  std::memcpy(out, v.data(), v.size());
  return out + v.size();
}

template <typename Float>
bool FallbackToSnprintf(const Float v, const ConversionSpec &conv,
                        FormatSinkImpl *sink) {
  int w = conv.width() >= 0 ? conv.width() : 0;
  int p = conv.precision() >= 0 ? conv.precision() : -1;
  char fmt[32];
  {
    char *fp = fmt;
    *fp++ = '%';
    fp = CopyStringTo(conv.flags().ToString(), fp);
    fp = CopyStringTo("*.*", fp);
    if (std::is_same<long double, Float>()) {
      *fp++ = 'L';
    }
    *fp++ = conv.conv().Char();
    *fp = 0;
    assert(fp < fmt + sizeof(fmt));
  }
  std::string space(512, '\0');
  string_view result;
  while (true) {
    int n = snprintf(&space[0], space.size(), fmt, w, p, v);
    if (n < 0) return false;
    if (static_cast<size_t>(n) < space.size()) {
      result = string_view(space.data(), n);
      break;
    }
    space.resize(n + 1);
  }
  sink->Append(result);
  return true;
}

// 128-bits in decimal: ceil(128*log(2)/log(10))
//   or std::numeric_limits<__uint128_t>::digits10
constexpr int kMaxFixedPrecision = 39;

constexpr int kBufferLength = /*sign*/ 1 +
                              /*integer*/ kMaxFixedPrecision +
                              /*point*/ 1 +
                              /*fraction*/ kMaxFixedPrecision +
                              /*exponent e+123*/ 5;

struct Buffer {
  void push_front(char c) {
    assert(begin > data);
    *--begin = c;
  }
  void push_back(char c) {
    assert(end < data + sizeof(data));
    *end++ = c;
  }
  void pop_back() {
    assert(begin < end);
    --end;
  }

  char &back() {
    assert(begin < end);
    return end[-1];
  }

  char last_digit() const { return end[-1] == '.' ? end[-2] : end[-1]; }

  int size() const { return static_cast<int>(end - begin); }

  char data[kBufferLength];
  char *begin;
  char *end;
};

enum class FormatStyle { Fixed, Precision };

// If the value is Inf or Nan, print it and return true.
// Otherwise, return false.
template <typename Float>
bool ConvertNonNumericFloats(char sign_char, Float v,
                             const ConversionSpec &conv, FormatSinkImpl *sink) {
  char text[4], *ptr = text;
  if (sign_char != '\0') *ptr++ = sign_char;
  if (std::isnan(v)) {
    ptr = std::copy_n(conv.conv().upper() ? "NAN" : "nan", 3, ptr);
  } else if (std::isinf(v)) {
    ptr = std::copy_n(conv.conv().upper() ? "INF" : "inf", 3, ptr);
  } else {
    return false;
  }

  return sink->PutPaddedString(string_view(text, ptr - text), conv.width(), -1,
                               conv.flags().left);
}

// Round up the last digit of the value.
// It will carry over and potentially overflow. 'exp' will be adjusted in that
// case.
template <FormatStyle mode>
void RoundUp(Buffer *buffer, int *exp) {
  char *p = &buffer->back();
  while (p >= buffer->begin && (*p == '9' || *p == '.')) {
    if (*p == '9') *p = '0';
    --p;
  }

  if (p < buffer->begin) {
    *p = '1';
    buffer->begin = p;
    if (mode == FormatStyle::Precision) {
      std::swap(p[1], p[2]);  // move the .
      ++*exp;
      buffer->pop_back();
    }
  } else {
    ++*p;
  }
}

void PrintExponent(int exp, char e, Buffer *out) {
  out->push_back(e);
  if (exp < 0) {
    out->push_back('-');
    exp = -exp;
  } else {
    out->push_back('+');
  }
  // Exponent digits.
  if (exp > 99) {
    out->push_back(exp / 100 + '0');
    out->push_back(exp / 10 % 10 + '0');
    out->push_back(exp % 10 + '0');
  } else {
    out->push_back(exp / 10 + '0');
    out->push_back(exp % 10 + '0');
  }
}

template <typename Float, typename Int>
constexpr bool CanFitMantissa() {
  return
#if defined(__clang__) && !defined(__SSE3__)
      // Workaround for clang bug: https://bugs.llvm.org/show_bug.cgi?id=38289
      // Casting from long double to uint64_t is miscompiled and drops bits.
      (!std::is_same<Float, long double>::value ||
       !std::is_same<Int, uint64_t>::value) &&
#endif
      std::numeric_limits<Float>::digits <= std::numeric_limits<Int>::digits;
}

template <typename Float>
struct Decomposed {
  using MantissaType =
      absl::conditional_t<std::is_same<long double, Float>::value, uint128,
                          uint64_t>;
  static_assert(std::numeric_limits<Float>::digits <= sizeof(MantissaType) * 8,
                "");
  MantissaType mantissa;
  int exponent;
};

// Decompose the double into an integer mantissa and an exponent.
template <typename Float>
Decomposed<Float> Decompose(Float v) {
  int exp;
  Float m = std::frexp(v, &exp);
  m = std::ldexp(m, std::numeric_limits<Float>::digits);
  exp -= std::numeric_limits<Float>::digits;

  return {static_cast<typename Decomposed<Float>::MantissaType>(m), exp};
}

// Print 'digits' as decimal.
// In Fixed mode, we add a '.' at the end.
// In Precision mode, we add a '.' after the first digit.
template <FormatStyle mode, typename Int>
int PrintIntegralDigits(Int digits, Buffer *out) {
  int printed = 0;
  if (digits) {
    for (; digits; digits /= 10) out->push_front(digits % 10 + '0');
    printed = out->size();
    if (mode == FormatStyle::Precision) {
      out->push_front(*out->begin);
      out->begin[1] = '.';
    } else {
      out->push_back('.');
    }
  } else if (mode == FormatStyle::Fixed) {
    out->push_front('0');
    out->push_back('.');
    printed = 1;
  }
  return printed;
}

// Back out 'extra_digits' digits and round up if necessary.
bool RemoveExtraPrecision(int extra_digits, bool has_leftover_value,
                          Buffer *out, int *exp_out) {
  if (extra_digits <= 0) return false;

  // Back out the extra digits
  out->end -= extra_digits;

  bool needs_to_round_up = [&] {
    // We look at the digit just past the end.
    // There must be 'extra_digits' extra valid digits after end.
    if (*out->end > '5') return true;
    if (*out->end < '5') return false;
    if (has_leftover_value || std::any_of(out->end + 1, out->end + extra_digits,
                                          [](char c) { return c != '0'; }))
      return true;

    // Ends in ...50*, round to even.
    return out->last_digit() % 2 == 1;
  }();

  if (needs_to_round_up) {
    RoundUp<FormatStyle::Precision>(out, exp_out);
  }
  return true;
}

// Print the value into the buffer.
// This will not include the exponent, which will be returned in 'exp_out' for
// Precision mode.
template <typename Int, typename Float, FormatStyle mode>
bool FloatToBufferImpl(Int int_mantissa, int exp, int precision, Buffer *out,
                       int *exp_out) {
  assert((CanFitMantissa<Float, Int>()));

  const int int_bits = std::numeric_limits<Int>::digits;

  // In precision mode, we start printing one char to the right because it will
  // also include the '.'
  // In fixed mode we put the dot afterwards on the right.
  out->begin = out->end =
      out->data + 1 + kMaxFixedPrecision + (mode == FormatStyle::Precision);

  if (exp >= 0) {
    if (std::numeric_limits<Float>::digits + exp > int_bits) {
      // The value will overflow the Int
      return false;
    }
    int digits_printed = PrintIntegralDigits<mode>(int_mantissa << exp, out);
    int digits_to_zero_pad = precision;
    if (mode == FormatStyle::Precision) {
      *exp_out = digits_printed - 1;
      digits_to_zero_pad -= digits_printed - 1;
      if (RemoveExtraPrecision(-digits_to_zero_pad, false, out, exp_out)) {
        return true;
      }
    }
    for (; digits_to_zero_pad-- > 0;) out->push_back('0');
    return true;
  }

  exp = -exp;
  // We need at least 4 empty bits for the next decimal digit.
  // We will multiply by 10.
  if (exp > int_bits - 4) return false;

  const Int mask = (Int{1} << exp) - 1;

  // Print the integral part first.
  int digits_printed = PrintIntegralDigits<mode>(int_mantissa >> exp, out);
  int_mantissa &= mask;

  int fractional_count = precision;
  if (mode == FormatStyle::Precision) {
    if (digits_printed == 0) {
      // Find the first non-zero digit, when in Precision mode.
      *exp_out = 0;
      if (int_mantissa) {
        while (int_mantissa <= mask) {
          int_mantissa *= 10;
          --*exp_out;
        }
      }
      out->push_front(static_cast<char>(int_mantissa >> exp) + '0');
      out->push_back('.');
      int_mantissa &= mask;
    } else {
      // We already have a digit, and a '.'
      *exp_out = digits_printed - 1;
      fractional_count -= *exp_out;
      if (RemoveExtraPrecision(-fractional_count, int_mantissa != 0, out,
                               exp_out)) {
        // If we had enough digits, return right away.
        // The code below will try to round again otherwise.
        return true;
      }
    }
  }

  auto get_next_digit = [&] {
    int_mantissa *= 10;
    int digit = static_cast<int>(int_mantissa >> exp);
    int_mantissa &= mask;
    return digit;
  };

  // Print fractional_count more digits, if available.
  for (; fractional_count > 0; --fractional_count) {
    out->push_back(get_next_digit() + '0');
  }

  int next_digit = get_next_digit();
  if (next_digit > 5 ||
      (next_digit == 5 && (int_mantissa || out->last_digit() % 2 == 1))) {
    RoundUp<mode>(out, exp_out);
  }

  return true;
}

template <FormatStyle mode, typename Float>
bool FloatToBuffer(Decomposed<Float> decomposed, int precision, Buffer *out,
                   int *exp) {
  if (precision > kMaxFixedPrecision) return false;

  // Try with uint64_t.
  if (CanFitMantissa<Float, std::uint64_t>() &&
      FloatToBufferImpl<std::uint64_t, Float, mode>(
          static_cast<std::uint64_t>(decomposed.mantissa),
          static_cast<std::uint64_t>(decomposed.exponent), precision, out, exp))
    return true;

#if defined(ABSL_HAVE_INTRINSIC_INT128)
  // If that is not enough, try with __uint128_t.
  return CanFitMantissa<Float, __uint128_t>() &&
         FloatToBufferImpl<__uint128_t, Float, mode>(
             static_cast<__uint128_t>(decomposed.mantissa),
             static_cast<__uint128_t>(decomposed.exponent), precision, out,
             exp);
#endif
  return false;
}

void WriteBufferToSink(char sign_char, string_view str,
                       const ConversionSpec &conv, FormatSinkImpl *sink) {
  int left_spaces = 0, zeros = 0, right_spaces = 0;
  int missing_chars =
      conv.width() >= 0 ? std::max(conv.width() - static_cast<int>(str.size()) -
                                       static_cast<int>(sign_char != 0),
                                   0)
                        : 0;
  if (conv.flags().left) {
    right_spaces = missing_chars;
  } else if (conv.flags().zero) {
    zeros = missing_chars;
  } else {
    left_spaces = missing_chars;
  }

  sink->Append(left_spaces, ' ');
  if (sign_char != '\0') sink->Append(1, sign_char);
  sink->Append(zeros, '0');
  sink->Append(str);
  sink->Append(right_spaces, ' ');
}

template <typename Float>
bool FloatToSink(const Float v, const ConversionSpec &conv,
                 FormatSinkImpl *sink) {
  // Print the sign or the sign column.
  Float abs_v = v;
  char sign_char = 0;
  if (std::signbit(abs_v)) {
    sign_char = '-';
    abs_v = -abs_v;
  } else if (conv.flags().show_pos) {
    sign_char = '+';
  } else if (conv.flags().sign_col) {
    sign_char = ' ';
  }

  // Print nan/inf.
  if (ConvertNonNumericFloats(sign_char, abs_v, conv, sink)) {
    return true;
  }

  int precision = conv.precision() < 0 ? 6 : conv.precision();

  int exp = 0;

  auto decomposed = Decompose(abs_v);

  Buffer buffer;

  switch (conv.conv().id()) {
    case ConversionChar::f:
    case ConversionChar::F:
      FormatF(decomposed.mantissa, decomposed.exponent,
              {sign_char, precision, conv, sink});
      return true;

    case ConversionChar::e:
    case ConversionChar::E:
      if (!FloatToBuffer<FormatStyle::Precision>(decomposed, precision, &buffer,
                                                 &exp)) {
        return FallbackToSnprintf(v, conv, sink);
      }
      if (!conv.flags().alt && buffer.back() == '.') buffer.pop_back();
      PrintExponent(exp, conv.conv().upper() ? 'E' : 'e', &buffer);
      break;

    case ConversionChar::g:
    case ConversionChar::G:
      precision = std::max(0, precision - 1);
      if (!FloatToBuffer<FormatStyle::Precision>(decomposed, precision, &buffer,
                                                 &exp)) {
        return FallbackToSnprintf(v, conv, sink);
      }
      if (precision + 1 > exp && exp >= -4) {
        if (exp < 0) {
          // Have 1.23456, needs 0.00123456
          // Move the first digit
          buffer.begin[1] = *buffer.begin;
          // Add some zeros
          for (; exp < -1; ++exp) *buffer.begin-- = '0';
          *buffer.begin-- = '.';
          *buffer.begin = '0';
        } else if (exp > 0) {
          // Have 1.23456, needs 1234.56
          // Move the '.' exp positions to the right.
          std::rotate(buffer.begin + 1, buffer.begin + 2,
                      buffer.begin + exp + 2);
        }
        exp = 0;
      }
      if (!conv.flags().alt) {
        while (buffer.back() == '0') buffer.pop_back();
        if (buffer.back() == '.') buffer.pop_back();
      }
      if (exp) PrintExponent(exp, conv.conv().upper() ? 'E' : 'e', &buffer);
      break;

    case ConversionChar::a:
    case ConversionChar::A:
      return FallbackToSnprintf(v, conv, sink);

    default:
      return false;
  }

  WriteBufferToSink(sign_char,
                    string_view(buffer.begin, buffer.end - buffer.begin), conv,
                    sink);

  return true;
}

}  // namespace

bool ConvertFloatImpl(long double v, const ConversionSpec &conv,
                      FormatSinkImpl *sink) {
  if (std::numeric_limits<long double>::digits ==
      2 * std::numeric_limits<double>::digits) {
    // This is the `double-double` representation of `long double`.
    // We do not handle it natively. Fallback to snprintf.
    return FallbackToSnprintf(v, conv, sink);
  }

  return FloatToSink(v, conv, sink);
}

bool ConvertFloatImpl(float v, const ConversionSpec &conv,
                      FormatSinkImpl *sink) {
  // DivideBy10WithCarry is not actually used in some builds. This here silences
  // the "unused" warning. We just need to put it in any function that is really
  // used.
  (void)&DivideBy10WithCarry;
  return FloatToSink(v, conv, sink);
}

bool ConvertFloatImpl(double v, const ConversionSpec &conv,
                      FormatSinkImpl *sink) {
  return FloatToSink(v, conv, sink);
}

}  // namespace str_format_internal
}  // namespace absl
