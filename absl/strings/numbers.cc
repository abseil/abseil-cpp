// This file contains std::string processing functions related to
// numeric values.

#include "absl/strings/numbers.h"

#include <cassert>
#include <cctype>
#include <cfloat>          // for DBL_DIG and FLT_DIG
#include <cmath>           // for HUGE_VAL
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

#include "absl/base/internal/raw_logging.h"
#include "absl/numeric/int128.h"
#include "absl/strings/ascii.h"
#include "absl/strings/internal/memutil.h"
#include "absl/strings/str_cat.h"

namespace absl {

bool SimpleAtof(absl::string_view str, float* value) {
  *value = 0.0;
  if (str.empty()) return false;
  char buf[32];
  std::unique_ptr<char[]> bigbuf;
  char* ptr = buf;
  if (str.size() > sizeof(buf) - 1) {
    bigbuf.reset(new char[str.size() + 1]);
    ptr = bigbuf.get();
  }
  memcpy(ptr, str.data(), str.size());
  ptr[str.size()] = '\0';

  char* endptr;
  *value = strtof(ptr, &endptr);
  if (endptr != ptr) {
    while (absl::ascii_isspace(*endptr)) ++endptr;
  }
  // Ignore range errors from strtod/strtof.
  // The values it returns on underflow and
  // overflow are the right fallback in a
  // robust setting.
  return *ptr != '\0' && *endptr == '\0';
}

bool SimpleAtod(absl::string_view str, double* value) {
  *value = 0.0;
  if (str.empty()) return false;
  char buf[32];
  std::unique_ptr<char[]> bigbuf;
  char* ptr = buf;
  if (str.size() > sizeof(buf) - 1) {
    bigbuf.reset(new char[str.size() + 1]);
    ptr = bigbuf.get();
  }
  memcpy(ptr, str.data(), str.size());
  ptr[str.size()] = '\0';

  char* endptr;
  *value = strtod(ptr, &endptr);
  if (endptr != ptr) {
    while (absl::ascii_isspace(*endptr)) ++endptr;
  }
  // Ignore range errors from strtod.  The values it
  // returns on underflow and overflow are the right
  // fallback in a robust setting.
  return *ptr != '\0' && *endptr == '\0';
}

namespace {

// TODO(rogeeff): replace with the real released thing once we figure out what
// it is.
inline bool CaseEqual(absl::string_view piece1, absl::string_view piece2) {
  return (piece1.size() == piece2.size() &&
          0 == strings_internal::memcasecmp(piece1.data(), piece2.data(),
                                            piece1.size()));
}

// Writes a two-character representation of 'i' to 'buf'. 'i' must be in the
// range 0 <= i < 100, and buf must have space for two characters. Example:
//   char buf[2];
//   PutTwoDigits(42, buf);
//   // buf[0] == '4'
//   // buf[1] == '2'
inline void PutTwoDigits(size_t i, char* buf) {
  static const char two_ASCII_digits[100][2] = {
    {'0', '0'}, {'0', '1'}, {'0', '2'}, {'0', '3'}, {'0', '4'},
    {'0', '5'}, {'0', '6'}, {'0', '7'}, {'0', '8'}, {'0', '9'},
    {'1', '0'}, {'1', '1'}, {'1', '2'}, {'1', '3'}, {'1', '4'},
    {'1', '5'}, {'1', '6'}, {'1', '7'}, {'1', '8'}, {'1', '9'},
    {'2', '0'}, {'2', '1'}, {'2', '2'}, {'2', '3'}, {'2', '4'},
    {'2', '5'}, {'2', '6'}, {'2', '7'}, {'2', '8'}, {'2', '9'},
    {'3', '0'}, {'3', '1'}, {'3', '2'}, {'3', '3'}, {'3', '4'},
    {'3', '5'}, {'3', '6'}, {'3', '7'}, {'3', '8'}, {'3', '9'},
    {'4', '0'}, {'4', '1'}, {'4', '2'}, {'4', '3'}, {'4', '4'},
    {'4', '5'}, {'4', '6'}, {'4', '7'}, {'4', '8'}, {'4', '9'},
    {'5', '0'}, {'5', '1'}, {'5', '2'}, {'5', '3'}, {'5', '4'},
    {'5', '5'}, {'5', '6'}, {'5', '7'}, {'5', '8'}, {'5', '9'},
    {'6', '0'}, {'6', '1'}, {'6', '2'}, {'6', '3'}, {'6', '4'},
    {'6', '5'}, {'6', '6'}, {'6', '7'}, {'6', '8'}, {'6', '9'},
    {'7', '0'}, {'7', '1'}, {'7', '2'}, {'7', '3'}, {'7', '4'},
    {'7', '5'}, {'7', '6'}, {'7', '7'}, {'7', '8'}, {'7', '9'},
    {'8', '0'}, {'8', '1'}, {'8', '2'}, {'8', '3'}, {'8', '4'},
    {'8', '5'}, {'8', '6'}, {'8', '7'}, {'8', '8'}, {'8', '9'},
    {'9', '0'}, {'9', '1'}, {'9', '2'}, {'9', '3'}, {'9', '4'},
    {'9', '5'}, {'9', '6'}, {'9', '7'}, {'9', '8'}, {'9', '9'}
  };
  assert(i < 100);
  memcpy(buf, two_ASCII_digits[i], 2);
}

}  // namespace

bool SimpleAtob(absl::string_view str, bool* value) {
  ABSL_RAW_CHECK(value != nullptr, "Output pointer must not be nullptr.");
  if (CaseEqual(str, "true") || CaseEqual(str, "t") ||
      CaseEqual(str, "yes") || CaseEqual(str, "y") ||
      CaseEqual(str, "1")) {
    *value = true;
    return true;
  }
  if (CaseEqual(str, "false") || CaseEqual(str, "f") ||
      CaseEqual(str, "no") || CaseEqual(str, "n") ||
      CaseEqual(str, "0")) {
    *value = false;
    return true;
  }
  return false;
}

// ----------------------------------------------------------------------
// FastInt32ToBuffer()
// FastUInt32ToBuffer()
// FastInt64ToBuffer()
// FastUInt64ToBuffer()
//
// Like the Fast*ToBuffer() functions above, these are intended for speed.
// Unlike the Fast*ToBuffer() functions, however, these functions write
// their output to the beginning of the buffer (hence the name, as the
// output is left-aligned).  The caller is responsible for ensuring that
// the buffer has enough space to hold the output.
//
// Returns a pointer to the end of the std::string (i.e. the null character
// terminating the std::string).
// ----------------------------------------------------------------------

namespace {

// Used to optimize printing a decimal number's final digit.
const char one_ASCII_final_digits[10][2] {
  {'0', 0}, {'1', 0}, {'2', 0}, {'3', 0}, {'4', 0},
  {'5', 0}, {'6', 0}, {'7', 0}, {'8', 0}, {'9', 0},
};

}  // namespace

char* numbers_internal::FastUInt32ToBuffer(uint32_t i, char* buffer) {
  uint32_t digits;
  // The idea of this implementation is to trim the number of divides to as few
  // as possible, and also reducing memory stores and branches, by going in
  // steps of two digits at a time rather than one whenever possible.
  // The huge-number case is first, in the hopes that the compiler will output
  // that case in one branch-free block of code, and only output conditional
  // branches into it from below.
  if (i >= 1000000000) {     // >= 1,000,000,000
    digits = i / 100000000;  //      100,000,000
    i -= digits * 100000000;
    PutTwoDigits(digits, buffer);
    buffer += 2;
  lt100_000_000:
    digits = i / 1000000;  // 1,000,000
    i -= digits * 1000000;
    PutTwoDigits(digits, buffer);
    buffer += 2;
  lt1_000_000:
    digits = i / 10000;  // 10,000
    i -= digits * 10000;
    PutTwoDigits(digits, buffer);
    buffer += 2;
  lt10_000:
    digits = i / 100;
    i -= digits * 100;
    PutTwoDigits(digits, buffer);
    buffer += 2;
 lt100:
    digits = i;
    PutTwoDigits(digits, buffer);
    buffer += 2;
    *buffer = 0;
    return buffer;
  }

  if (i < 100) {
    digits = i;
    if (i >= 10) goto lt100;
    memcpy(buffer, one_ASCII_final_digits[i], 2);
    return buffer + 1;
  }
  if (i < 10000) {  //    10,000
    if (i >= 1000) goto lt10_000;
    digits = i / 100;
    i -= digits * 100;
    *buffer++ = '0' + digits;
    goto lt100;
  }
  if (i < 1000000) {  //    1,000,000
    if (i >= 100000) goto lt1_000_000;
    digits = i / 10000;  //    10,000
    i -= digits * 10000;
    *buffer++ = '0' + digits;
    goto lt10_000;
  }
  if (i < 100000000) {  //    100,000,000
    if (i >= 10000000) goto lt100_000_000;
    digits = i / 1000000;  //   1,000,000
    i -= digits * 1000000;
    *buffer++ = '0' + digits;
    goto lt1_000_000;
  }
  // we already know that i < 1,000,000,000
  digits = i / 100000000;  //   100,000,000
  i -= digits * 100000000;
  *buffer++ = '0' + digits;
  goto lt100_000_000;
}

char* numbers_internal::FastInt32ToBuffer(int32_t i, char* buffer) {
  uint32_t u = i;
  if (i < 0) {
    *buffer++ = '-';
    // We need to do the negation in modular (i.e., "unsigned")
    // arithmetic; MSVC++ apprently warns for plain "-u", so
    // we write the equivalent expression "0 - u" instead.
    u = 0 - u;
  }
  return numbers_internal::FastUInt32ToBuffer(u, buffer);
}

char* numbers_internal::FastUInt64ToBuffer(uint64_t i, char* buffer) {
  uint32_t u32 = static_cast<uint32_t>(i);
  if (u32 == i) return numbers_internal::FastUInt32ToBuffer(u32, buffer);

  // Here we know i has at least 10 decimal digits.
  uint64_t top_1to11 = i / 1000000000;
  u32 = static_cast<uint32_t>(i - top_1to11 * 1000000000);
  uint32_t top_1to11_32 = static_cast<uint32_t>(top_1to11);

  if (top_1to11_32 == top_1to11) {
    buffer = numbers_internal::FastUInt32ToBuffer(top_1to11_32, buffer);
  } else {
    // top_1to11 has more than 32 bits too; print it in two steps.
    uint32_t top_8to9 = static_cast<uint32_t>(top_1to11 / 100);
    uint32_t mid_2 = static_cast<uint32_t>(top_1to11 - top_8to9 * 100);
    buffer = numbers_internal::FastUInt32ToBuffer(top_8to9, buffer);
    PutTwoDigits(mid_2, buffer);
    buffer += 2;
  }

  // We have only 9 digits now, again the maximum uint32_t can handle fully.
  uint32_t digits = u32 / 10000000;  // 10,000,000
  u32 -= digits * 10000000;
  PutTwoDigits(digits, buffer);
  buffer += 2;
  digits = u32 / 100000;  // 100,000
  u32 -= digits * 100000;
  PutTwoDigits(digits, buffer);
  buffer += 2;
  digits = u32 / 1000;  // 1,000
  u32 -= digits * 1000;
  PutTwoDigits(digits, buffer);
  buffer += 2;
  digits = u32 / 10;
  u32 -= digits * 10;
  PutTwoDigits(digits, buffer);
  buffer += 2;
  memcpy(buffer, one_ASCII_final_digits[u32], 2);
  return buffer + 1;
}

char* numbers_internal::FastInt64ToBuffer(int64_t i, char* buffer) {
  uint64_t u = i;
  if (i < 0) {
    *buffer++ = '-';
    u = 0 - u;
  }
  return numbers_internal::FastUInt64ToBuffer(u, buffer);
}

// Although DBL_DIG is typically 15, DBL_MAX is normally represented with 17
// digits of precision. When converted to a std::string value with fewer digits
// of precision using strtod(), the result can be bigger than DBL_MAX due to
// a rounding error. Converting this value back to a double will produce an
// Inf which will trigger a SIGFPE if FP exceptions are enabled. We skip
// the precision check for sufficiently large values to avoid the SIGFPE.
static const double kDoublePrecisionCheckMax = DBL_MAX / 1.000000000000001;

char* numbers_internal::RoundTripDoubleToBuffer(double d, char* buffer) {
  // DBL_DIG is 15 for IEEE-754 doubles, which are used on almost all
  // platforms these days.  Just in case some system exists where DBL_DIG
  // is significantly larger -- and risks overflowing our buffer -- we have
  // this assert.
  static_assert(DBL_DIG < 20, "DBL_DIG is too big");

  bool full_precision_needed = true;
  if (std::abs(d) <= kDoublePrecisionCheckMax) {
    int snprintf_result = snprintf(buffer, numbers_internal::kFastToBufferSize,
                                   "%.*g", DBL_DIG, d);

    // The snprintf should never overflow because the buffer is significantly
    // larger than the precision we asked for.
    assert(snprintf_result > 0 &&
           snprintf_result < numbers_internal::kFastToBufferSize);
    (void)snprintf_result;

    full_precision_needed = strtod(buffer, nullptr) != d;
  }

  if (full_precision_needed) {
    int snprintf_result = snprintf(buffer, numbers_internal::kFastToBufferSize,
                                   "%.*g", DBL_DIG + 2, d);

    // Should never overflow; see above.
    assert(snprintf_result > 0 &&
           snprintf_result < numbers_internal::kFastToBufferSize);
    (void)snprintf_result;
  }
  return buffer;
}
// This table is used to quickly calculate the base-ten exponent of a given
// float, and then to provide a multiplier to bring that number into the
// range 1-999,999,999, that is, into uint32_t range.  Finally, the exp
// std::string is made available so there is one less int-to-std::string conversion
// to be done.

struct Spec {
  double min_range;
  double multiplier;
  const char expstr[5];
};
const Spec neg_exp_table[] = {
    {1.4e-45f, 1e+55, "e-45"},  //
    {1e-44f, 1e+54, "e-44"},    //
    {1e-43f, 1e+53, "e-43"},    //
    {1e-42f, 1e+52, "e-42"},    //
    {1e-41f, 1e+51, "e-41"},    //
    {1e-40f, 1e+50, "e-40"},    //
    {1e-39f, 1e+49, "e-39"},    //
    {1e-38f, 1e+48, "e-38"},    //
    {1e-37f, 1e+47, "e-37"},    //
    {1e-36f, 1e+46, "e-36"},    //
    {1e-35f, 1e+45, "e-35"},    //
    {1e-34f, 1e+44, "e-34"},    //
    {1e-33f, 1e+43, "e-33"},    //
    {1e-32f, 1e+42, "e-32"},    //
    {1e-31f, 1e+41, "e-31"},    //
    {1e-30f, 1e+40, "e-30"},    //
    {1e-29f, 1e+39, "e-29"},    //
    {1e-28f, 1e+38, "e-28"},    //
    {1e-27f, 1e+37, "e-27"},    //
    {1e-26f, 1e+36, "e-26"},    //
    {1e-25f, 1e+35, "e-25"},    //
    {1e-24f, 1e+34, "e-24"},    //
    {1e-23f, 1e+33, "e-23"},    //
    {1e-22f, 1e+32, "e-22"},    //
    {1e-21f, 1e+31, "e-21"},    //
    {1e-20f, 1e+30, "e-20"},    //
    {1e-19f, 1e+29, "e-19"},    //
    {1e-18f, 1e+28, "e-18"},    //
    {1e-17f, 1e+27, "e-17"},    //
    {1e-16f, 1e+26, "e-16"},    //
    {1e-15f, 1e+25, "e-15"},    //
    {1e-14f, 1e+24, "e-14"},    //
    {1e-13f, 1e+23, "e-13"},    //
    {1e-12f, 1e+22, "e-12"},    //
    {1e-11f, 1e+21, "e-11"},    //
    {1e-10f, 1e+20, "e-10"},    //
    {1e-09f, 1e+19, "e-09"},    //
    {1e-08f, 1e+18, "e-08"},    //
    {1e-07f, 1e+17, "e-07"},    //
    {1e-06f, 1e+16, "e-06"},    //
    {1e-05f, 1e+15, "e-05"},    //
    {1e-04f, 1e+14, "e-04"},    //
};

const Spec pos_exp_table[] = {
    {1e+08f, 1e+02, "e+08"},  //
    {1e+09f, 1e+01, "e+09"},  //
    {1e+10f, 1e+00, "e+10"},  //
    {1e+11f, 1e-01, "e+11"},  //
    {1e+12f, 1e-02, "e+12"},  //
    {1e+13f, 1e-03, "e+13"},  //
    {1e+14f, 1e-04, "e+14"},  //
    {1e+15f, 1e-05, "e+15"},  //
    {1e+16f, 1e-06, "e+16"},  //
    {1e+17f, 1e-07, "e+17"},  //
    {1e+18f, 1e-08, "e+18"},  //
    {1e+19f, 1e-09, "e+19"},  //
    {1e+20f, 1e-10, "e+20"},  //
    {1e+21f, 1e-11, "e+21"},  //
    {1e+22f, 1e-12, "e+22"},  //
    {1e+23f, 1e-13, "e+23"},  //
    {1e+24f, 1e-14, "e+24"},  //
    {1e+25f, 1e-15, "e+25"},  //
    {1e+26f, 1e-16, "e+26"},  //
    {1e+27f, 1e-17, "e+27"},  //
    {1e+28f, 1e-18, "e+28"},  //
    {1e+29f, 1e-19, "e+29"},  //
    {1e+30f, 1e-20, "e+30"},  //
    {1e+31f, 1e-21, "e+31"},  //
    {1e+32f, 1e-22, "e+32"},  //
    {1e+33f, 1e-23, "e+33"},  //
    {1e+34f, 1e-24, "e+34"},  //
    {1e+35f, 1e-25, "e+35"},  //
    {1e+36f, 1e-26, "e+36"},  //
    {1e+37f, 1e-27, "e+37"},  //
    {1e+38f, 1e-28, "e+38"},  //
    {1e+39,  1e-29, "e+39"},  //
};

struct ExpCompare {
  bool operator()(const Spec& spec, double d) const {
    return spec.min_range < d;
  }
};

// Utility routine(s) for RoundTripFloatToBuffer:
// OutputNecessaryDigits takes two 11-digit numbers, whose integer portion
// represents the fractional part of a floating-point number, and outputs a
// number that is in-between them, with the fewest digits possible. For
// instance, given 12345678900 and 12345876900, it would output "0123457".
// When there are multiple final digits that would satisfy this requirement,
// this routine attempts to use a digit that would represent the average of
// lower_double and upper_double.
//
// Although the routine works using integers, all callers use doubles, so
// for their convenience this routine accepts doubles.
static char* OutputNecessaryDigits(double lower_double, double upper_double,
                                   char* out) {
  assert(lower_double > 0);
  assert(lower_double < upper_double - 10);
  assert(upper_double < 100000000000.0);

  // Narrow the range a bit; without this bias, an input of lower=87654320010.0
  // and upper=87654320100.0 would produce an output of 876543201
  //
  // We do this in three steps: first, we lower the upper bound and truncate it
  // to an integer.  Then, we increase the lower bound by exactly the amount we
  // just decreased the upper bound by - at that point, the midpoint is exactly
  // where it used to be.  Then we truncate the lower bound.

  uint64_t upper64 = upper_double - (1.0 / 1024);
  double shrink = upper_double - upper64;
  uint64_t lower64 = lower_double + shrink;

  // Theory of operation: we convert the lower number to ascii representation,
  // two digits at a time.  As we go, we remove the same digits from the upper
  // number.  When we see the upper number does not share those same digits, we
  // know we can stop converting. When we stop, the last digit we output is
  // taken from the average of upper and lower values, rounded up.
  char buf[2];
  uint32_t lodigits =
      static_cast<uint32_t>(lower64 / 1000000000);  // 1,000,000,000
  uint64_t mul64 = lodigits * uint64_t{1000000000};

  PutTwoDigits(lodigits, out);
  out += 2;
  if (upper64 - mul64 >= 1000000000) {  // digit mismatch!
    PutTwoDigits(upper64 / 1000000000, buf);
    if (out[-2] != buf[0]) {
      out[-2] = '0' + (upper64 + lower64 + 10000000000) / 20000000000;
      --out;
    } else {
      PutTwoDigits((upper64 + lower64 + 1000000000) / 2000000000, out - 2);
    }
    *out = '\0';
    return out;
  }
  uint32_t lower = static_cast<uint32_t>(lower64 - mul64);
  uint32_t upper = static_cast<uint32_t>(upper64 - mul64);

  lodigits = lower / 10000000;  // 10,000,000
  uint32_t mul = lodigits * 10000000;
  PutTwoDigits(lodigits, out);
  out += 2;
  if (upper - mul >= 10000000) {  // digit mismatch!
    PutTwoDigits(upper / 10000000, buf);
    if (out[-2] != buf[0]) {
      out[-2] = '0' + (upper + lower + 100000000) / 200000000;
      --out;
    } else {
      PutTwoDigits((upper + lower + 10000000) / 20000000, out - 2);
    }
    *out = '\0';
    return out;
  }
  lower -= mul;
  upper -= mul;

  lodigits = lower / 100000;  // 100,000
  mul = lodigits * 100000;
  PutTwoDigits(lodigits, out);
  out += 2;
  if (upper - mul >= 100000) {  // digit mismatch!
    PutTwoDigits(upper / 100000, buf);
    if (out[-2] != buf[0]) {
      out[-2] = '0' + (upper + lower + 1000000) / 2000000;
      --out;
    } else {
      PutTwoDigits((upper + lower + 100000) / 200000, out - 2);
    }
    *out = '\0';
    return out;
  }
  lower -= mul;
  upper -= mul;

  lodigits = lower / 1000;
  mul = lodigits * 1000;
  PutTwoDigits(lodigits, out);
  out += 2;
  if (upper - mul >= 1000) {  // digit mismatch!
    PutTwoDigits(upper / 1000, buf);
    if (out[-2] != buf[0]) {
      out[-2] = '0' + (upper + lower + 10000) / 20000;
      --out;
    } else {
      PutTwoDigits((upper + lower + 1000) / 2000, out - 2);
    }
    *out = '\0';
    return out;
  }
  lower -= mul;
  upper -= mul;

  PutTwoDigits(lower / 10, out);
  out += 2;
  PutTwoDigits(upper / 10, buf);
  if (out[-2] != buf[0]) {
    out[-2] = '0' + (upper + lower + 100) / 200;
    --out;
  } else {
    PutTwoDigits((upper + lower + 10) / 20, out - 2);
  }
  *out = '\0';
  return out;
}

// RoundTripFloatToBuffer converts the given float into a std::string which, if
// passed to strtof, will produce the exact same original float.  It does this
// by computing the range of possible doubles which map to the given float, and
// then examining the digits of the doubles in that range.  If all the doubles
// in the range start with "2.37", then clearly our float does, too.  As soon as
// they diverge, only one more digit is needed.
char* numbers_internal::RoundTripFloatToBuffer(float f, char* buffer) {
  static_assert(std::numeric_limits<float>::is_iec559,
                "IEEE-754/IEC-559 support only");

  char* out = buffer;  // we write data to out, incrementing as we go, but
                       // FloatToBuffer always returns the address of the buffer
                       // passed in.

  if (std::isnan(f)) {
    strcpy(out, "nan");  // NOLINT(runtime/printf)
    return buffer;
  }
  if (f == 0) {  // +0 and -0 are handled here
    if (std::signbit(f)) {
      strcpy(out, "-0");  // NOLINT(runtime/printf)
    } else {
      strcpy(out, "0");  // NOLINT(runtime/printf)
    }
    return buffer;
  }
  if (f < 0) {
    *out++ = '-';
    f = -f;
  }
  if (std::isinf(f)) {
    strcpy(out, "inf");  // NOLINT(runtime/printf)
    return buffer;
  }

  double next_lower = nextafterf(f, 0.0f);
  // For all doubles in the range lower_bound < f < upper_bound, the
  // nearest float is f.
  double lower_bound = (f + next_lower) * 0.5;
  double upper_bound = f + (f - lower_bound);
  // Note: because std::nextafter is slow, we calculate upper_bound
  // assuming that it is the same distance from f as lower_bound is.
  // For exact powers of two, upper_bound is actually twice as far
  // from f as lower_bound is, but this turns out not to matter.

  // Most callers pass floats that are either 0 or within the
  // range 0.0001 through 100,000,000, so handle those first,
  // since they don't need exponential notation.
  const Spec* spec = nullptr;
  if (f < 1.0) {
    if (f >= 0.0001f) {
      // for fractional values, we set up the multiplier at the same
      // time as we output the leading "0." / "0.0" / "0.00" / "0.000"
      double multiplier = 1e+11;
      *out++ = '0';
      *out++ = '.';
      if (f < 0.1f) {
        multiplier = 1e+12;
        *out++ = '0';
        if (f < 0.01f) {
          multiplier = 1e+13;
          *out++ = '0';
          if (f < 0.001f) {
            multiplier = 1e+14;
            *out++ = '0';
          }
        }
      }
      OutputNecessaryDigits(lower_bound * multiplier, upper_bound * multiplier,
                            out);
      return buffer;
    }
    spec = std::lower_bound(std::begin(neg_exp_table), std::end(neg_exp_table),
                            double{f}, ExpCompare());
    if (spec == std::end(neg_exp_table)) --spec;
  } else if (f < 1e8) {
    // Handling non-exponential format greater than 1.0 is similar to the above,
    // but instead of 0.0 / 0.00 / 0.000, the prefix is simply the truncated
    // integer part of f.
    int32_t as_int = f;
    out = numbers_internal::FastUInt32ToBuffer(as_int, out);
    // Easy: if the integer part is within (lower_bound, upper_bound), then we
    // are already done.
    if (as_int > lower_bound && as_int < upper_bound) {
      return buffer;
    }
    *out++ = '.';
    OutputNecessaryDigits((lower_bound - as_int) * 1e11,
                          (upper_bound - as_int) * 1e11, out);
    return buffer;
  } else {
    spec = std::lower_bound(std::begin(pos_exp_table),
                            std::end(pos_exp_table),
                            double{f}, ExpCompare());
    if (spec == std::end(pos_exp_table)) --spec;
  }
  // Exponential notation from here on.  "spec" was computed using lower_bound,
  // which means it's the first spec from the table where min_range is greater
  // or equal to f.
  // Unfortunately that's not quite what we want; we want a min_range that is
  // less or equal.  So first thing, if it was greater, back up one entry.
  if (spec->min_range > f) --spec;

  // The digits might be "237000123", but we want "2.37000123",
  // so we output the digits one character later, and then move the first
  // digit back so we can stick the "." in.
  char* start = out;
  out = OutputNecessaryDigits(lower_bound * spec->multiplier,
                              upper_bound * spec->multiplier, start + 1);
  start[0] = start[1];
  start[1] = '.';

  // If it turns out there was only one digit output, then back up over the '.'
  if (out == &start[2]) --out;

  // Now add the "e+NN" part.
  memcpy(out, spec->expstr, 4);
  out[4] = '\0';
  return buffer;
}

// Returns the number of leading 0 bits in a 64-bit value.
// TODO(jorg): Replace with builtin_clzll if available.
// Are we shipping util/bits in absl?
static inline int CountLeadingZeros64(uint64_t n) {
  int zeroes = 60;
  if (n >> 32) zeroes -= 32, n >>= 32;
  if (n >> 16) zeroes -= 16, n >>= 16;
  if (n >> 8) zeroes -= 8, n >>= 8;
  if (n >> 4) zeroes -= 4, n >>= 4;
  return "\4\3\2\2\1\1\1\1\0\0\0\0\0\0\0\0"[n] + zeroes;
}

// Given a 128-bit number expressed as a pair of uint64_t, high half first,
// return that number multiplied by the given 32-bit value.  If the result is
// too large to fit in a 128-bit number, divide it by 2 until it fits.
static std::pair<uint64_t, uint64_t> Mul32(std::pair<uint64_t, uint64_t> num,
                                           uint32_t mul) {
  uint64_t bits0_31 = num.second & 0xFFFFFFFF;
  uint64_t bits32_63 = num.second >> 32;
  uint64_t bits64_95 = num.first & 0xFFFFFFFF;
  uint64_t bits96_127 = num.first >> 32;

  // The picture so far: each of these 64-bit values has only the lower 32 bits
  // filled in.
  // bits96_127:          [ 00000000 xxxxxxxx ]
  // bits64_95:                    [ 00000000 xxxxxxxx ]
  // bits32_63:                             [ 00000000 xxxxxxxx ]
  // bits0_31:                                       [ 00000000 xxxxxxxx ]

  bits0_31 *= mul;
  bits32_63 *= mul;
  bits64_95 *= mul;
  bits96_127 *= mul;

  // Now the top halves may also have value, though all 64 of their bits will
  // never be set at the same time, since they are a result of a 32x32 bit
  // multiply.  This makes the carry calculation slightly easier.
  // bits96_127:          [ mmmmmmmm | mmmmmmmm ]
  // bits64_95:                    [ | mmmmmmmm mmmmmmmm | ]
  // bits32_63:                      |        [ mmmmmmmm | mmmmmmmm ]
  // bits0_31:                       |                 [ | mmmmmmmm mmmmmmmm ]
  // eventually:        [ bits128_up | ...bits64_127.... | ..bits0_63... ]

  uint64_t bits0_63 = bits0_31 + (bits32_63 << 32);
  uint64_t bits64_127 = bits64_95 + (bits96_127 << 32) + (bits32_63 >> 32) +
                        (bits0_63 < bits0_31);
  uint64_t bits128_up = (bits96_127 >> 32) + (bits64_127 < bits64_95);
  if (bits128_up == 0) return {bits64_127, bits0_63};

  int shift = 64 - CountLeadingZeros64(bits128_up);
  uint64_t lo = (bits0_63 >> shift) + (bits64_127 << (64 - shift));
  uint64_t hi = (bits64_127 >> shift) + (bits128_up << (64 - shift));
  return {hi, lo};
}

// Compute num * 5 ^ expfive, and return the first 128 bits of the result,
// where the first bit is always a one.  So PowFive(1, 0) starts 0b100000,
// PowFive(1, 1) starts 0b101000, PowFive(1, 2) starts 0b110010, etc.
static std::pair<uint64_t, uint64_t> PowFive(uint64_t num, int expfive) {
  std::pair<uint64_t, uint64_t> result = {num, 0};
  while (expfive >= 13) {
    // 5^13 is the highest power of five that will fit in a 32-bit integer.
    result = Mul32(result, 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5);
    expfive -= 13;
  }
  constexpr int powers_of_five[13] = {
      1,
      5,
      5 * 5,
      5 * 5 * 5,
      5 * 5 * 5 * 5,
      5 * 5 * 5 * 5 * 5,
      5 * 5 * 5 * 5 * 5 * 5,
      5 * 5 * 5 * 5 * 5 * 5 * 5,
      5 * 5 * 5 * 5 * 5 * 5 * 5 * 5,
      5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5,
      5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5,
      5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5,
      5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5 * 5};
  result = Mul32(result, powers_of_five[expfive & 15]);
  int shift = CountLeadingZeros64(result.first);
  if (shift != 0) {
    result.first = (result.first << shift) + (result.second >> (64 - shift));
    result.second = (result.second << shift);
  }
  return result;
}

struct ExpDigits {
  int32_t exponent;
  char digits[6];
};

// SplitToSix converts value, a positive double-precision floating-point number,
// into a base-10 exponent and 6 ASCII digits, where the first digit is never
// zero.  For example, SplitToSix(1) returns an exponent of zero and a digits
// array of {'1', '0', '0', '0', '0', '0'}.  If value is exactly halfway between
// two possible representations, e.g. value = 100000.5, then "round to even" is
// performed.
static ExpDigits SplitToSix(const double value) {
  ExpDigits exp_dig;
  int exp = 5;
  double d = value;
  // First step: calculate a close approximation of the output, where the
  // value d will be between 100,000 and 999,999, representing the digits
  // in the output ASCII array, and exp is the base-10 exponent.  It would be
  // faster to use a table here, and to look up the base-2 exponent of value,
  // however value is an IEEE-754 64-bit number, so the table would have 2,000
  // entries, which is not cache-friendly.
  if (d >= 999999.5) {
    if (d >= 1e+261) exp += 256, d *= 1e-256;
    if (d >= 1e+133) exp += 128, d *= 1e-128;
    if (d >= 1e+69) exp += 64, d *= 1e-64;
    if (d >= 1e+37) exp += 32, d *= 1e-32;
    if (d >= 1e+21) exp += 16, d *= 1e-16;
    if (d >= 1e+13) exp += 8, d *= 1e-8;
    if (d >= 1e+9) exp += 4, d *= 1e-4;
    if (d >= 1e+7) exp += 2, d *= 1e-2;
    if (d >= 1e+6) exp += 1, d *= 1e-1;
  } else {
    if (d < 1e-250) exp -= 256, d *= 1e256;
    if (d < 1e-122) exp -= 128, d *= 1e128;
    if (d < 1e-58) exp -= 64, d *= 1e64;
    if (d < 1e-26) exp -= 32, d *= 1e32;
    if (d < 1e-10) exp -= 16, d *= 1e16;
    if (d < 1e-2) exp -= 8, d *= 1e8;
    if (d < 1e+2) exp -= 4, d *= 1e4;
    if (d < 1e+4) exp -= 2, d *= 1e2;
    if (d < 1e+5) exp -= 1, d *= 1e1;
  }
  // At this point, d is in the range [99999.5..999999.5) and exp is in the
  // range [-324..308]. Since we need to round d up, we want to add a half
  // and truncate.
  // However, the technique above may have lost some precision, due to its
  // repeated multiplication by constants that each may be off by half a bit
  // of precision.  This only matters if we're close to the edge though.
  // Since we'd like to know if the fractional part of d is close to a half,
  // we multiply it by 65536 and see if the fractional part is close to 32768.
  // (The number doesn't have to be a power of two,but powers of two are faster)
  uint64_t d64k = d * 65536;
  int dddddd;  // A 6-digit decimal integer.
  if ((d64k % 65536) == 32767 || (d64k % 65536) == 32768) {
    // OK, it's fairly likely that precision was lost above, which is
    // not a surprise given only 52 mantissa bits are available.  Therefore
    // redo the calculation using 128-bit numbers.  (64 bits are not enough).

    // Start out with digits rounded down; maybe add one below.
    dddddd = static_cast<int>(d64k / 65536);

    // mantissa is a 64-bit integer representing M.mmm... * 2^63.  The actual
    // value we're representing, of course, is M.mmm... * 2^exp2.
    int exp2;
    double m = std::frexp(value, &exp2);
    uint64_t mantissa = m * (32768.0 * 65536.0 * 65536.0 * 65536.0);
    // std::frexp returns an m value in the range [0.5, 1.0), however we
    // can't multiply it by 2^64 and convert to an integer because some FPUs
    // throw an exception when converting an number higher than 2^63 into an
    // integer - even an unsigned 64-bit integer!  Fortunately it doesn't matter
    // since m only has 52 significant bits anyway.
    mantissa <<= 1;
    exp2 -= 64;  // not needed, but nice for debugging

    // OK, we are here to compare:
    //     (dddddd + 0.5) * 10^(exp-5)  vs.  mantissa * 2^exp2
    // so we can round up dddddd if appropriate.  Those values span the full
    // range of 600 orders of magnitude of IEE 64-bit floating-point.
    // Fortunately, we already know they are very close, so we don't need to
    // track the base-2 exponent of both sides.  This greatly simplifies the
    // the math since the 2^exp2 calculation is unnecessary and the power-of-10
    // calculation can become a power-of-5 instead.

    std::pair<uint64_t, uint64_t> edge, val;
    if (exp >= 6) {
      // Compare (dddddd + 0.5) * 5 ^ (exp - 5) to mantissa
      // Since we're tossing powers of two, 2 * dddddd + 1 is the
      // same as dddddd + 0.5
      edge = PowFive(2 * dddddd + 1, exp - 5);

      val.first = mantissa;
      val.second = 0;
    } else {
      // We can't compare (dddddd + 0.5) * 5 ^ (exp - 5) to mantissa as we did
      // above because (exp - 5) is negative.  So we compare (dddddd + 0.5) to
      // mantissa * 5 ^ (5 - exp)
      edge = PowFive(2 * dddddd + 1, 0);

      val = PowFive(mantissa, 5 - exp);
    }
    // printf("exp=%d %016lx %016lx vs %016lx %016lx\n", exp, val.first,
    //        val.second, edge.first, edge.second);
    if (val > edge) {
      dddddd++;
    } else if (val == edge) {
      dddddd += (dddddd & 1);
    }
  } else {
    // Here, we are not close to the edge.
    dddddd = static_cast<int>((d64k + 32768) / 65536);
  }
  if (dddddd == 1000000) {
    dddddd = 100000;
    exp += 1;
  }
  exp_dig.exponent = exp;

  int two_digits = dddddd / 10000;
  dddddd -= two_digits * 10000;
  PutTwoDigits(two_digits, &exp_dig.digits[0]);

  two_digits = dddddd / 100;
  dddddd -= two_digits * 100;
  PutTwoDigits(two_digits, &exp_dig.digits[2]);

  PutTwoDigits(dddddd, &exp_dig.digits[4]);
  return exp_dig;
}

// Helper function for fast formatting of floating-point.
// The result is the same as "%g", a.k.a. "%.6g".
size_t numbers_internal::SixDigitsToBuffer(double d, char* const buffer) {
  static_assert(std::numeric_limits<float>::is_iec559,
                "IEEE-754/IEC-559 support only");

  char* out = buffer;  // we write data to out, incrementing as we go, but
                       // FloatToBuffer always returns the address of the buffer
                       // passed in.

  if (std::isnan(d)) {
    strcpy(out, "nan");  // NOLINT(runtime/printf)
    return 3;
  }
  if (d == 0) {  // +0 and -0 are handled here
    if (std::signbit(d)) *out++ = '-';
    *out++ = '0';
    *out = 0;
    return out - buffer;
  }
  if (d < 0) {
    *out++ = '-';
    d = -d;
  }
  if (std::isinf(d)) {
    strcpy(out, "inf");  // NOLINT(runtime/printf)
    return out + 3 - buffer;
  }

  auto exp_dig = SplitToSix(d);
  int exp = exp_dig.exponent;
  const char* digits = exp_dig.digits;
  out[0] = '0';
  out[1] = '.';
  switch (exp) {
    case 5:
      memcpy(out, &digits[0], 6), out += 6;
      *out = 0;
      return out - buffer;
    case 4:
      memcpy(out, &digits[0], 5), out += 5;
      if (digits[5] != '0') {
        *out++ = '.';
        *out++ = digits[5];
      }
      *out = 0;
      return out - buffer;
    case 3:
      memcpy(out, &digits[0], 4), out += 4;
      if ((digits[5] | digits[4]) != '0') {
        *out++ = '.';
        *out++ = digits[4];
        if (digits[5] != '0') *out++ = digits[5];
      }
      *out = 0;
      return out - buffer;
    case 2:
      memcpy(out, &digits[0], 3), out += 3;
      *out++ = '.';
      memcpy(out, &digits[3], 3);
      out += 3;
      while (out[-1] == '0') --out;
      if (out[-1] == '.') --out;
      *out = 0;
      return out - buffer;
    case 1:
      memcpy(out, &digits[0], 2), out += 2;
      *out++ = '.';
      memcpy(out, &digits[2], 4);
      out += 4;
      while (out[-1] == '0') --out;
      if (out[-1] == '.') --out;
      *out = 0;
      return out - buffer;
    case 0:
      memcpy(out, &digits[0], 1), out += 1;
      *out++ = '.';
      memcpy(out, &digits[1], 5);
      out += 5;
      while (out[-1] == '0') --out;
      if (out[-1] == '.') --out;
      *out = 0;
      return out - buffer;
    case -4:
      out[2] = '0';
      ++out;
      ABSL_FALLTHROUGH_INTENDED;
    case -3:
      out[2] = '0';
      ++out;
      ABSL_FALLTHROUGH_INTENDED;
    case -2:
      out[2] = '0';
      ++out;
      ABSL_FALLTHROUGH_INTENDED;
    case -1:
      out += 2;
      memcpy(out, &digits[0], 6);
      out += 6;
      while (out[-1] == '0') --out;
      *out = 0;
      return out - buffer;
  }
  assert(exp < -4 || exp >= 6);
  out[0] = digits[0];
  assert(out[1] == '.');
  out += 2;
  memcpy(out, &digits[1], 5), out += 5;
  while (out[-1] == '0') --out;
  if (out[-1] == '.') --out;
  *out++ = 'e';
  if (exp > 0) {
    *out++ = '+';
  } else {
    *out++ = '-';
    exp = -exp;
  }
  if (exp > 99) {
    int dig1 = exp / 100;
    exp -= dig1 * 100;
    *out++ = '0' + dig1;
  }
  PutTwoDigits(exp, out);
  out += 2;
  *out = 0;
  return out - buffer;
}

namespace {
// Represents integer values of digits.
// Uses 36 to indicate an invalid character since we support
// bases up to 36.
static const int8_t kAsciiToInt[256] = {
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,  // 16 36s.
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 0,  1,  2,  3,  4,  5,
    6,  7,  8,  9,  36, 36, 36, 36, 36, 36, 36, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    36, 36, 36, 36, 36, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36};

// Parse the sign and optional hex or oct prefix in text.
inline bool safe_parse_sign_and_base(absl::string_view* text /*inout*/,
                                     int* base_ptr /*inout*/,
                                     bool* negative_ptr /*output*/) {
  if (text->data() == nullptr) {
    return false;
  }

  const char* start = text->data();
  const char* end = start + text->size();
  int base = *base_ptr;

  // Consume whitespace.
  while (start < end && absl::ascii_isspace(start[0])) {
    ++start;
  }
  while (start < end && absl::ascii_isspace(end[-1])) {
    --end;
  }
  if (start >= end) {
    return false;
  }

  // Consume sign.
  *negative_ptr = (start[0] == '-');
  if (*negative_ptr || start[0] == '+') {
    ++start;
    if (start >= end) {
      return false;
    }
  }

  // Consume base-dependent prefix.
  //  base 0: "0x" -> base 16, "0" -> base 8, default -> base 10
  //  base 16: "0x" -> base 16
  // Also validate the base.
  if (base == 0) {
    if (end - start >= 2 && start[0] == '0' &&
        (start[1] == 'x' || start[1] == 'X')) {
      base = 16;
      start += 2;
      if (start >= end) {
        // "0x" with no digits after is invalid.
        return false;
      }
    } else if (end - start >= 1 && start[0] == '0') {
      base = 8;
      start += 1;
    } else {
      base = 10;
    }
  } else if (base == 16) {
    if (end - start >= 2 && start[0] == '0' &&
        (start[1] == 'x' || start[1] == 'X')) {
      start += 2;
      if (start >= end) {
        // "0x" with no digits after is invalid.
        return false;
      }
    }
  } else if (base >= 2 && base <= 36) {
    // okay
  } else {
    return false;
  }
  *text = absl::string_view(start, end - start);
  *base_ptr = base;
  return true;
}

// Consume digits.
//
// The classic loop:
//
//   for each digit
//     value = value * base + digit
//   value *= sign
//
// The classic loop needs overflow checking.  It also fails on the most
// negative integer, -2147483648 in 32-bit two's complement representation.
//
// My improved loop:
//
//  if (!negative)
//    for each digit
//      value = value * base
//      value = value + digit
//  else
//    for each digit
//      value = value * base
//      value = value - digit
//
// Overflow checking becomes simple.

// Lookup tables per IntType:
// vmax/base and vmin/base are precomputed because division costs at least 8ns.
// TODO(junyer): Doing this per base instead (i.e. an array of structs, not a
// struct of arrays) would probably be better in terms of d-cache for the most
// commonly used bases.
template <typename IntType>
struct LookupTables {
  static const IntType kVmaxOverBase[];
  static const IntType kVminOverBase[];
};

// An array initializer macro for X/base where base in [0, 36].
// However, note that lookups for base in [0, 1] should never happen because
// base has been validated to be in [2, 36] by safe_parse_sign_and_base().
#define X_OVER_BASE_INITIALIZER(X)                                        \
  {                                                                       \
    0, 0, X / 2, X / 3, X / 4, X / 5, X / 6, X / 7, X / 8, X / 9, X / 10, \
        X / 11, X / 12, X / 13, X / 14, X / 15, X / 16, X / 17, X / 18,   \
        X / 19, X / 20, X / 21, X / 22, X / 23, X / 24, X / 25, X / 26,   \
        X / 27, X / 28, X / 29, X / 30, X / 31, X / 32, X / 33, X / 34,   \
        X / 35, X / 36,                                                   \
  }

template <typename IntType>
const IntType LookupTables<IntType>::kVmaxOverBase[] =
    X_OVER_BASE_INITIALIZER(std::numeric_limits<IntType>::max());

template <typename IntType>
const IntType LookupTables<IntType>::kVminOverBase[] =
    X_OVER_BASE_INITIALIZER(std::numeric_limits<IntType>::min());

#undef X_OVER_BASE_INITIALIZER

template <typename IntType>
inline bool safe_parse_positive_int(absl::string_view text, int base,
                                    IntType* value_p) {
  IntType value = 0;
  const IntType vmax = std::numeric_limits<IntType>::max();
  assert(vmax > 0);
  assert(base >= 0);
  assert(vmax >= static_cast<IntType>(base));
  const IntType vmax_over_base = LookupTables<IntType>::kVmaxOverBase[base];
  const char* start = text.data();
  const char* end = start + text.size();
  // loop over digits
  for (; start < end; ++start) {
    unsigned char c = static_cast<unsigned char>(start[0]);
    int digit = kAsciiToInt[c];
    if (digit >= base) {
      *value_p = value;
      return false;
    }
    if (value > vmax_over_base) {
      *value_p = vmax;
      return false;
    }
    value *= base;
    if (value > vmax - digit) {
      *value_p = vmax;
      return false;
    }
    value += digit;
  }
  *value_p = value;
  return true;
}

template <typename IntType>
inline bool safe_parse_negative_int(absl::string_view text, int base,
                                    IntType* value_p) {
  IntType value = 0;
  const IntType vmin = std::numeric_limits<IntType>::min();
  assert(vmin < 0);
  assert(vmin <= 0 - base);
  IntType vmin_over_base = LookupTables<IntType>::kVminOverBase[base];
  // 2003 c++ standard [expr.mul]
  // "... the sign of the remainder is implementation-defined."
  // Although (vmin/base)*base + vmin%base is always vmin.
  // 2011 c++ standard tightens the spec but we cannot rely on it.
  // TODO(junyer): Handle this in the lookup table generation.
  if (vmin % base > 0) {
    vmin_over_base += 1;
  }
  const char* start = text.data();
  const char* end = start + text.size();
  // loop over digits
  for (; start < end; ++start) {
    unsigned char c = static_cast<unsigned char>(start[0]);
    int digit = kAsciiToInt[c];
    if (digit >= base) {
      *value_p = value;
      return false;
    }
    if (value < vmin_over_base) {
      *value_p = vmin;
      return false;
    }
    value *= base;
    if (value < vmin + digit) {
      *value_p = vmin;
      return false;
    }
    value -= digit;
  }
  *value_p = value;
  return true;
}

// Input format based on POSIX.1-2008 strtol
// http://pubs.opengroup.org/onlinepubs/9699919799/functions/strtol.html
template <typename IntType>
inline bool safe_int_internal(absl::string_view text, IntType* value_p,
                              int base) {
  *value_p = 0;
  bool negative;
  if (!safe_parse_sign_and_base(&text, &base, &negative)) {
    return false;
  }
  if (!negative) {
    return safe_parse_positive_int(text, base, value_p);
  } else {
    return safe_parse_negative_int(text, base, value_p);
  }
}

template <typename IntType>
inline bool safe_uint_internal(absl::string_view text, IntType* value_p,
                               int base) {
  *value_p = 0;
  bool negative;
  if (!safe_parse_sign_and_base(&text, &base, &negative) || negative) {
    return false;
  }
  return safe_parse_positive_int(text, base, value_p);
}
}  // anonymous namespace

namespace numbers_internal {
bool safe_strto32_base(absl::string_view text, int32_t* value, int base) {
  return safe_int_internal<int32_t>(text, value, base);
}

bool safe_strto64_base(absl::string_view text, int64_t* value, int base) {
  return safe_int_internal<int64_t>(text, value, base);
}

bool safe_strtou32_base(absl::string_view text, uint32_t* value, int base) {
  return safe_uint_internal<uint32_t>(text, value, base);
}

bool safe_strtou64_base(absl::string_view text, uint64_t* value, int base) {
  return safe_uint_internal<uint64_t>(text, value, base);
}
}  // namespace numbers_internal

}  // namespace absl
