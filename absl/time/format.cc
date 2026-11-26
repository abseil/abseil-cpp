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

#include <string.h>

#include <chrono>  // NOLINT(build/c++11)
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"
#include "absl/time/time.h"

namespace cctz = absl::time_internal::cctz;

namespace absl {
ABSL_NAMESPACE_BEGIN

ABSL_DLL extern const char RFC3339_full[] = "%Y-%m-%d%ET%H:%M:%E*S%Ez";
ABSL_DLL extern const char RFC3339_sec[] = "%Y-%m-%d%ET%H:%M:%S%Ez";

ABSL_DLL extern const char RFC1123_full[] = "%a, %d %b %E4Y %H:%M:%S %z";
ABSL_DLL extern const char RFC1123_no_wday[] = "%d %b %E4Y %H:%M:%S %z";

namespace {

constexpr absl::string_view kInfiniteFutureStr = "infinite-future";
constexpr absl::string_view kInfinitePastStr = "infinite-past";

constexpr size_t kRfc3339FullSize = sizeof(RFC3339_full) - 1;
constexpr size_t kRfc3339SecSize = sizeof(RFC3339_sec) - 1;

enum class FractionalSeconds {
  kForbidden,
  kOptional,
};

constexpr uint32_t kZeroChar = static_cast<unsigned char>('0');

inline uint32_t DigitValue(char ch) {
  return static_cast<uint32_t>(static_cast<unsigned char>(ch)) - kZeroChar;
}

inline bool TryConsumeDigit(const char** cursor, const char* end,
                            uint32_t* digit) {
  if (*cursor >= end) return false;
  const uint32_t value = DigitValue(**cursor);
  if (value > 9) return false;
  *digit = value;
  ++(*cursor);
  return true;
}

inline bool ParseTwoDigits(const char* cursor, uint32_t* out) {
  const uint32_t d0 = DigitValue(cursor[0]);
  const uint32_t d1 = DigitValue(cursor[1]);
  if (d0 > 9 || d1 > 9) return false;
  *out = d0 * 10 + d1;
  return true;
}

inline bool IsLeapYear(uint32_t year) {
  return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
}

// Parses optional fractional seconds (".<digits>") for %E*S and scales them to
// absl::Duration's 0.25ns rep_lo ticks, matching cctz::detail::ParseSubSeconds
// followed by Join() truncation.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline bool ParseFractionalSeconds(
    const char** cursor, const char* end, uint32_t* rep_lo) {
  static_assert(time_internal::kTicksPerNanosecond == 4,
                "Subsecond tick scaling assumes 0.25ns ticks (4 per ns).");
  constexpr uint32_t kTicksPerNs =
      static_cast<uint32_t>(time_internal::kTicksPerNanosecond);

  const char* pos = *cursor;
  if (pos >= end || *pos != '.') {
    *rep_lo = 0;
    return true;
  }
  ++pos;

  uint32_t digit = 0;
  if (!TryConsumeDigit(&pos, end, &digit)) return false;

  uint32_t nanos = digit;
  uint32_t digits = 1;
  const char* const nine_end = (end - pos < 8) ? end : pos + 8;
  while (TryConsumeDigit(&pos, nine_end, &digit)) {
    nanos = nanos * 10 + digit;
    ++digits;
  }

  static constexpr uint32_t kPow10[9] = {
      1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
  if (digits < 9) {
    *rep_lo = (nanos * kPow10[9U - digits]) * kTicksPerNs;
  } else {
    // Digits 10 and 11 determine the 0.25ns quarter-nanosecond ticks:
    // floor(femtoseconds / 250000) == nanos * 4 + (d10 * 10 + d11) / 25.
    // Digits 12..15 contribute at most 9999 femtoseconds and cannot change the
    // quotient, while digits 16+ are consumed and ignored by ParseSubSeconds.
    uint32_t ticks = nanos * kTicksPerNs;
    if (TryConsumeDigit(&pos, end, &digit)) {
      uint32_t sub_nano = digit * 10;
      if (TryConsumeDigit(&pos, end, &digit)) {
        sub_nano += digit;
        while (pos < end && DigitValue(*pos) <= 9) {
          ++pos;
        }
      }
      ticks += sub_nano / 25;
    }
    *rep_lo = ticks;
  }

  *cursor = pos;
  return true;
}

// Attempts to consume an optional "[:]DD" offset field in [0, 59], advancing
// `*cursor` and storing `*out` only on success (matching cctz::ParseOffset).
ABSL_ATTRIBUTE_ALWAYS_INLINE inline bool TryConsumeOptionalOffsetField(
    const char** cursor, const char* end, uint32_t* out) {
  const char* pos = *cursor;
  if (pos < end && *pos == ':') ++pos;
  uint32_t value = 0;
  if (end - pos < 2 || !ParseTwoDigits(pos, &value) || value > 59) {
    return false;
  }
  *out = value;
  *cursor = pos + 2;
  return true;
}

// Parses a %Ez UTC offset: 'Z'/'z', or (+|-)HH[[:]MM[[:]SS]].
ABSL_ATTRIBUTE_ALWAYS_INLINE inline bool ParseUtcOffset(
    const char** cursor, const char* end, int64_t* offset_seconds) {
  const char* pos = *cursor;
  if (pos >= end) return false;
  const char tz_char = *pos++;
  if (tz_char == 'Z' || tz_char == 'z') {
    *offset_seconds = 0;
    *cursor = pos;
    return true;
  }
  if (tz_char != '+' && tz_char != '-') return false;

  uint32_t offset_hours = 0;
  if (end - pos < 2 || !ParseTwoDigits(pos, &offset_hours) ||
      offset_hours > 23) {
    return false;
  }
  pos += 2;

  uint32_t offset_minutes = 0;
  uint32_t offset_secs = 0;
  if (TryConsumeOptionalOffsetField(&pos, end, &offset_minutes)) {
    TryConsumeOptionalOffsetField(&pos, end, &offset_secs);
  }

  int64_t total_seconds = static_cast<int64_t>(
      (offset_hours * 60 + offset_minutes) * 60 + offset_secs);
  if (tz_char == '-') total_seconds = -total_seconds;
  *offset_seconds = total_seconds;
  *cursor = pos;
  return true;
}

// Converts a validated Gregorian civil date in [0000-01-01, 9999-12-31] to
// days since 1970-01-01. Self-contained (avoiding private cctz::detail::impl
// coupling) and algebraically identical to cctz::detail::impl::ymd_ord().
ABSL_ATTRIBUTE_ALWAYS_INLINE inline int64_t DaysSinceUnixEpoch(uint32_t year,
                                                               uint32_t month,
                                                               uint32_t day) {
  // 719468 is days from 0000-03-01 to 1970-01-01; 146097 is days per 400-year
  // Gregorian cycle from shifting `year` by +400 so `adjusted_year >= 399` and
  // unsigned division equals exact floor division.
  constexpr int64_t kShiftedEpochOffsetDays = 719468 + 146097;
  static constexpr uint32_t kMonthDayOffset[12] = {
      306, 337, 0, 31, 61, 92, 122, 153, 184, 214, 245, 275};
  const uint32_t adjusted_year = year + 400U - (month <= 2 ? 1U : 0U);
  const uint32_t raw_days = adjusted_year * 365U + adjusted_year / 4U -
                            adjusted_year / 100U + adjusted_year / 400U +
                            kMonthDayOffset[month - 1] + (day - 1);
  return static_cast<int64_t>(raw_days) - kShiftedEpochOffsetDays;
}

// Fast-path parser for RFC3339_full (%Y-%m-%d%ET%H:%M:%E*S%Ez) and
// RFC3339_sec (%Y-%m-%d%ET%H:%M:%S%Ez) inputs with standard 4-digit years
// (0000-9999), 2-digit fields, and non-leap seconds (00-59).
//
// Returns true and sets `*time` on success. On any unusual or invalid input
// (such as non-4-digit years, single-digit fields, leap seconds ":60",
// "infinite-future"/"infinite-past", or malformed syntax), returns false
// without dereferencing `time` so the caller falls back to
// cctz::detail::parse() for exact compatibility and error reporting.
//
// Marked NOINLINE to avoid duplicating the parser body across both ParseTime()
// overloads and AbslParseFlag(), and to keep non-RFC3339 ParseTime() calls
// shrink-wrapped as a direct tail-call to ParseTimeSlowPath().
ABSL_ATTRIBUTE_NOINLINE bool TryParseRfc3339(absl::string_view input,
                                             FractionalSeconds fractional_mode,
                                             absl::Time* time) {
  // Minimum valid fast-path input is "YYYY-MM-DDTHH:MM:SSZ" (20 bytes).
  // Checking input.size() first avoids pointer arithmetic on empty/null views.
  if (input.size() < 20) return false;

  const char* cursor = input.data();
  const char* const end = cursor + input.size();

  // Skip leading ASCII whitespace, matching StripLeadingAsciiWhitespace().
  if (static_cast<unsigned char>(*cursor) <= ' ') {
    while (cursor < end &&
           absl::ascii_isspace(static_cast<unsigned char>(*cursor))) {
      ++cursor;
    }
    if (end - cursor < 20) return false;
  }

  // Validate fixed separator positions first: YYYY-MM-DD[Tt]HH:MM:SS
  if (cursor[4] != '-' || cursor[7] != '-' ||
      (cursor[10] != 'T' && cursor[10] != 't') || cursor[13] != ':' ||
      cursor[16] != ':') {
    return false;
  }

  uint32_t year_hi = 0;
  uint32_t year_lo = 0;
  uint32_t month = 0;
  uint32_t day = 0;
  uint32_t hour = 0;
  uint32_t minute = 0;
  uint32_t second = 0;
  if (!ParseTwoDigits(cursor, &year_hi) ||
      !ParseTwoDigits(cursor + 2, &year_lo) ||
      !ParseTwoDigits(cursor + 5, &month) ||
      !ParseTwoDigits(cursor + 8, &day) ||
      !ParseTwoDigits(cursor + 11, &hour) ||
      !ParseTwoDigits(cursor + 14, &minute) ||
      !ParseTwoDigits(cursor + 17, &second)) {
    return false;
  }

  const uint32_t year = year_hi * 100 + year_lo;
  if (month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) {
    return false;
  }

  static constexpr uint32_t kDaysInMonth[13] = {0,  31, 28, 31, 30, 31, 30,
                                                31, 31, 30, 31, 30, 31};
  uint32_t max_day = kDaysInMonth[month];
  if (month == 2 && IsLeapYear(year)) {
    max_day = 29;
  }
  if (day < 1 || day > max_day) return false;

  cursor += 19;

  uint32_t rep_lo = 0;
  if (fractional_mode == FractionalSeconds::kOptional &&
      !ParseFractionalSeconds(&cursor, end, &rep_lo)) {
    return false;
  }

  int64_t offset_seconds = 0;
  if (!ParseUtcOffset(&cursor, end, &offset_seconds)) {
    return false;
  }

  // Trailing characters must be ASCII whitespace; otherwise fall back to
  // cctz::detail::parse() for locale whitespace or exact error messages.
  while (cursor < end) {
    if (!absl::ascii_isspace(static_cast<unsigned char>(*cursor))) return false;
    ++cursor;
  }

  const int64_t days = DaysSinceUnixEpoch(year, month, day);
  const int64_t seconds_of_day =
      static_cast<int64_t>(hour * 3600U + minute * 60U + second);
  const int64_t unix_seconds = days * 86400 + seconds_of_day - offset_seconds;
  *time = time_internal::FromUnixDuration(
      time_internal::MakeDuration(unix_seconds, rep_lo));
  return true;
}

bool TryParseRfc3339Format(absl::string_view format, absl::string_view input,
                           absl::Time* time) {
  if (format.size() == kRfc3339FullSize &&
      (format.data() == RFC3339_full ||
       format == absl::string_view(RFC3339_full, kRfc3339FullSize))) {
    return TryParseRfc3339(input, FractionalSeconds::kOptional, time);
  }
  if (format.size() == kRfc3339SecSize &&
      (format.data() == RFC3339_sec ||
       format == absl::string_view(RFC3339_sec, kRfc3339SecSize))) {
    return TryParseRfc3339(input, FractionalSeconds::kForbidden, time);
  }
  return false;
}

struct cctz_parts {
  cctz::time_point<cctz::seconds> sec;
  cctz::detail::femtoseconds fem;
};

inline cctz::time_point<cctz::seconds> unix_epoch() {
  return std::chrono::time_point_cast<cctz::seconds>(
      std::chrono::system_clock::from_time_t(0));
}

// Splits a Time into seconds and femtoseconds, which can be used with CCTZ.
// Requires that 't' is finite. See duration.cc for details about rep_hi and
// rep_lo.
cctz_parts Split(absl::Time t) {
  const auto d = time_internal::ToUnixDuration(t);
  const int64_t rep_hi = time_internal::GetRepHi(d);
  const int64_t rep_lo = time_internal::GetRepLo(d);
  const auto sec = unix_epoch() + cctz::seconds(rep_hi);
  const auto fem = cctz::detail::femtoseconds(rep_lo * (1000 * 1000 / 4));
  return {sec, fem};
}

// Joins the given seconds and femtoseconds into a Time. See duration.cc for
// details about rep_hi and rep_lo.
absl::Time Join(const cctz_parts& parts) {
  const int64_t rep_hi = (parts.sec - unix_epoch()).count();
  const uint32_t rep_lo =
      static_cast<uint32_t>(parts.fem.count() / (1000 * 1000 / 4));
  const auto d = time_internal::MakeDuration(rep_hi, rep_lo);
  return time_internal::FromUnixDuration(d);
}

bool ParseTimeSlowPath(absl::string_view format, absl::string_view input,
                       absl::TimeZone tz, absl::Time* time, std::string* err) {
  static constexpr struct Literal {
    absl::string_view name;
    absl::Time value;
  } kLiterals[] = {
      {kInfiniteFutureStr, InfiniteFuture()},
      {kInfinitePastStr, InfinitePast()},
  };
  input = StripLeadingAsciiWhitespace(input);
  for (const auto& lit : kLiterals) {
    if (absl::StartsWith(input, lit.name)) {
      absl::string_view tail = input.substr(lit.name.size());
      // The trailing portion must be empty or whitespace.
      if (StripLeadingAsciiWhitespace(tail).empty()) {
        *time = lit.value;
        return true;
      }
    }
  }

  std::string error;
  cctz_parts parts;
  const bool b =
      cctz::detail::parse(std::string(format), std::string(input),
                          cctz::time_zone(tz), &parts.sec, &parts.fem, &error);
  if (b) {
    *time = Join(parts);
  } else if (err != nullptr) {
    *err = std::move(error);
  }
  return b;
}

}  // namespace

std::string FormatTime(absl::string_view format, absl::Time t,
                       absl::TimeZone tz) {
  if (t == absl::InfiniteFuture()) return std::string(kInfiniteFutureStr);
  if (t == absl::InfinitePast()) return std::string(kInfinitePastStr);
  const auto parts = Split(t);
  return cctz::detail::format(std::string(format), parts.sec, parts.fem,
                              cctz::time_zone(tz));
}

std::string FormatTime(absl::Time t, absl::TimeZone tz) {
  return FormatTime(RFC3339_full, t, tz);
}

std::string FormatTime(absl::Time t) {
  return absl::FormatTime(RFC3339_full, t, absl::LocalTimeZone());
}

bool ParseTime(absl::string_view format, absl::string_view input,
               absl::Time* time, std::string* err) {
  if (TryParseRfc3339Format(format, input, time)) {
    return true;
  }
  return ParseTimeSlowPath(format, input, absl::UTCTimeZone(), time, err);
}

// If the input string does not contain an explicit UTC offset, interpret
// the fields with respect to the given TimeZone.
bool ParseTime(absl::string_view format, absl::string_view input,
               absl::TimeZone tz, absl::Time* time, std::string* err) {
  if (TryParseRfc3339Format(format, input, time)) {
    return true;
  }
  return ParseTimeSlowPath(format, input, tz, time, err);
}

// Functions required to support absl::Time flags.
bool AbslParseFlag(absl::string_view text, absl::Time* t, std::string* error) {
  if (TryParseRfc3339(text, FractionalSeconds::kOptional, t)) {
    return true;
  }
  return ParseTimeSlowPath(RFC3339_full, text, absl::UTCTimeZone(), t, error);
}

std::string AbslUnparseFlag(absl::Time t) {
  return absl::FormatTime(RFC3339_full, t, absl::UTCTimeZone());
}

ABSL_NAMESPACE_END
}  // namespace absl
