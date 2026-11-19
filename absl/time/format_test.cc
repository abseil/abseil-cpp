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

#include <cstdint>
#include <limits>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/internal/test_util.h"
#include "absl/time/time.h"

using testing::HasSubstr;

namespace {

// A helper that tests the given format specifier by itself, and with leading
// and trailing characters.  For example: TestFormatSpecifier(t, "%a", "Thu").
void TestFormatSpecifier(absl::Time t, absl::TimeZone tz,
                         const std::string& fmt, const std::string& ans) {
  EXPECT_EQ(ans, absl::FormatTime(fmt, t, tz));
  EXPECT_EQ("xxx " + ans, absl::FormatTime("xxx " + fmt, t, tz));
  EXPECT_EQ(ans + " yyy", absl::FormatTime(fmt + " yyy", t, tz));
  EXPECT_EQ("xxx " + ans + " yyy",
            absl::FormatTime("xxx " + fmt + " yyy", t, tz));
}

//
// Testing FormatTime()
//

TEST(FormatTime, Basics) {
  absl::TimeZone tz = absl::UTCTimeZone();
  absl::Time t = absl::FromTimeT(0);

  // Starts with a couple basic edge cases.
  EXPECT_EQ("", absl::FormatTime("", t, tz));
  EXPECT_EQ(" ", absl::FormatTime(" ", t, tz));
  EXPECT_EQ("  ", absl::FormatTime("  ", t, tz));
  EXPECT_EQ("xxx", absl::FormatTime("xxx", t, tz));
  std::string big(128, 'x');
  EXPECT_EQ(big, absl::FormatTime(big, t, tz));
  // Cause the 1024-byte buffer to grow.
  std::string bigger(100000, 'x');
  EXPECT_EQ(bigger, absl::FormatTime(bigger, t, tz));

  t += absl::Hours(13) + absl::Minutes(4) + absl::Seconds(5);
  t += absl::Milliseconds(6) + absl::Microseconds(7) + absl::Nanoseconds(8);
  EXPECT_EQ("1970-01-01", absl::FormatTime("%Y-%m-%d", t, tz));
  EXPECT_EQ("13:04:05", absl::FormatTime("%H:%M:%S", t, tz));
  EXPECT_EQ("13:04:05.006", absl::FormatTime("%H:%M:%E3S", t, tz));
  EXPECT_EQ("13:04:05.006007", absl::FormatTime("%H:%M:%E6S", t, tz));
  EXPECT_EQ("13:04:05.006007008", absl::FormatTime("%H:%M:%E9S", t, tz));
}

TEST(FormatTime, LocaleSpecific) {
  const absl::TimeZone tz = absl::UTCTimeZone();
  absl::Time t = absl::FromTimeT(0);

  TestFormatSpecifier(t, tz, "%a", "Thu");
  TestFormatSpecifier(t, tz, "%A", "Thursday");
  TestFormatSpecifier(t, tz, "%b", "Jan");
  TestFormatSpecifier(t, tz, "%B", "January");

  // %c should at least produce the numeric year and time-of-day.
  const std::string s =
      absl::FormatTime("%c", absl::FromTimeT(0), absl::UTCTimeZone());
  EXPECT_THAT(s, HasSubstr("1970"));
  EXPECT_THAT(s, HasSubstr("00:00:00"));

  TestFormatSpecifier(t, tz, "%p", "AM");
  TestFormatSpecifier(t, tz, "%x", "01/01/70");
  TestFormatSpecifier(t, tz, "%X", "00:00:00");
}

TEST(FormatTime, ExtendedSeconds) {
  const absl::TimeZone tz = absl::UTCTimeZone();

  // No subseconds.
  absl::Time t = absl::FromTimeT(0) + absl::Seconds(5);
  EXPECT_EQ("05", absl::FormatTime("%E*S", t, tz));
  EXPECT_EQ("05.000000000000000", absl::FormatTime("%E15S", t, tz));

  // With subseconds.
  t += absl::Milliseconds(6) + absl::Microseconds(7) + absl::Nanoseconds(8);
  EXPECT_EQ("05.006007008", absl::FormatTime("%E*S", t, tz));
  EXPECT_EQ("05", absl::FormatTime("%E0S", t, tz));
  EXPECT_EQ("05.006007008000000", absl::FormatTime("%E15S", t, tz));

  // Times before the Unix epoch.
  t = absl::FromUnixMicros(-1);
  EXPECT_EQ("1969-12-31 23:59:59.999999",
            absl::FormatTime("%Y-%m-%d %H:%M:%E*S", t, tz));

  // Here is a "%E*S" case we got wrong for a while.  While the first
  // instant below is correctly rendered as "...:07.333304", the second
  // one used to appear as "...:07.33330499999999999".
  t = absl::FromUnixMicros(1395024427333304);
  EXPECT_EQ("2014-03-17 02:47:07.333304",
            absl::FormatTime("%Y-%m-%d %H:%M:%E*S", t, tz));
  t += absl::Microseconds(1);
  EXPECT_EQ("2014-03-17 02:47:07.333305",
            absl::FormatTime("%Y-%m-%d %H:%M:%E*S", t, tz));
}

TEST(FormatTime, RFC1123FormatPadsYear) {  // locale specific
  absl::TimeZone tz = absl::UTCTimeZone();

  // A year of 77 should be padded to 0077.
  absl::Time t = absl::FromCivil(absl::CivilSecond(77, 6, 28, 9, 8, 7), tz);
  EXPECT_EQ("Mon, 28 Jun 0077 09:08:07 +0000",
            absl::FormatTime(absl::RFC1123_full, t, tz));
  EXPECT_EQ("28 Jun 0077 09:08:07 +0000",
            absl::FormatTime(absl::RFC1123_no_wday, t, tz));
}

TEST(FormatTime, InfiniteTime) {
  absl::TimeZone tz = absl::time_internal::LoadTimeZone("America/Los_Angeles");

  // The format and timezone are ignored.
  EXPECT_EQ("infinite-future",
            absl::FormatTime("%H:%M blah", absl::InfiniteFuture(), tz));
  EXPECT_EQ("infinite-past",
            absl::FormatTime("%H:%M blah", absl::InfinitePast(), tz));
}

//
// Testing ParseTime()
//

TEST(ParseTime, Basics) {
  absl::Time t = absl::FromTimeT(1234567890);
  std::string err;

  // Simple edge cases.
  EXPECT_TRUE(absl::ParseTime("", "", &t, &err)) << err;
  EXPECT_EQ(absl::UnixEpoch(), t);  // everything defaulted
  EXPECT_TRUE(absl::ParseTime(" ", " ", &t, &err)) << err;
  EXPECT_TRUE(absl::ParseTime("  ", "  ", &t, &err)) << err;
  EXPECT_TRUE(absl::ParseTime("x", "x", &t, &err)) << err;
  EXPECT_TRUE(absl::ParseTime("xxx", "xxx", &t, &err)) << err;

  EXPECT_TRUE(absl::ParseTime("%Y-%m-%d %H:%M:%S %z",
                              "2013-06-28 19:08:09 -0800", &t, &err))
      << err;
  const auto ci = absl::FixedTimeZone(-8 * 60 * 60).At(t);
  EXPECT_EQ(absl::CivilSecond(2013, 6, 28, 19, 8, 9), ci.cs);
  EXPECT_EQ(absl::ZeroDuration(), ci.subsecond);
}

TEST(ParseTime, NullErrorString) {
  absl::Time t;
  EXPECT_FALSE(absl::ParseTime("%Q", "invalid format", &t, nullptr));
  EXPECT_FALSE(absl::ParseTime("%H", "12 trailing data", &t, nullptr));
  EXPECT_FALSE(
      absl::ParseTime("%H out of range", "42 out of range", &t, nullptr));
}

TEST(ParseTime, WithTimeZone) {
  const absl::TimeZone tz =
      absl::time_internal::LoadTimeZone("America/Los_Angeles");
  absl::Time t;
  std::string e;

  // We can parse a string without a UTC offset if we supply a timezone.
  EXPECT_TRUE(
      absl::ParseTime("%Y-%m-%d %H:%M:%S", "2013-06-28 19:08:09", tz, &t, &e))
      << e;
  auto ci = tz.At(t);
  EXPECT_EQ(absl::CivilSecond(2013, 6, 28, 19, 8, 9), ci.cs);
  EXPECT_EQ(absl::ZeroDuration(), ci.subsecond);

  // But the timezone is ignored when a UTC offset is present.
  EXPECT_TRUE(absl::ParseTime("%Y-%m-%d %H:%M:%S %z",
                              "2013-06-28 19:08:09 +0800", tz, &t, &e))
      << e;
  ci = absl::FixedTimeZone(8 * 60 * 60).At(t);
  EXPECT_EQ(absl::CivilSecond(2013, 6, 28, 19, 8, 9), ci.cs);
  EXPECT_EQ(absl::ZeroDuration(), ci.subsecond);
}

TEST(ParseTime, ErrorCases) {
  absl::Time t = absl::FromTimeT(0);
  std::string err;

  EXPECT_FALSE(absl::ParseTime("%S", "123", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Illegal trailing data"));

  // Can't parse an illegal format specifier.
  err.clear();
  EXPECT_FALSE(absl::ParseTime("%Q", "x", &t, &err)) << err;
  // Exact contents of "err" are platform-dependent because of
  // differences in the strptime implementation between macOS and Linux.
  EXPECT_FALSE(err.empty());

  // Fails because of trailing, unparsed data "blah".
  EXPECT_FALSE(absl::ParseTime("%m-%d", "2-3 blah", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Illegal trailing data"));

  // Feb 31 requires normalization.
  EXPECT_FALSE(absl::ParseTime("%m-%d", "2-31", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Out-of-range"));

  // Check that we cannot have spaces in UTC offsets.
  EXPECT_TRUE(absl::ParseTime("%z", "-0203", &t, &err)) << err;
  EXPECT_FALSE(absl::ParseTime("%z", "- 2 3", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
  EXPECT_TRUE(absl::ParseTime("%Ez", "-02:03", &t, &err)) << err;
  EXPECT_FALSE(absl::ParseTime("%Ez", "- 2: 3", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));

  // Check that we reject other malformed UTC offsets.
  EXPECT_FALSE(absl::ParseTime("%Ez", "+-08:00", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
  EXPECT_FALSE(absl::ParseTime("%Ez", "-+08:00", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));

  // Check that we do not accept "-0" in fields that allow zero.
  EXPECT_FALSE(absl::ParseTime("%Y", "-0", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
  EXPECT_FALSE(absl::ParseTime("%E4Y", "-0", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
  EXPECT_FALSE(absl::ParseTime("%H", "-0", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
  EXPECT_FALSE(absl::ParseTime("%M", "-0", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
  EXPECT_FALSE(absl::ParseTime("%S", "-0", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
  EXPECT_FALSE(absl::ParseTime("%z", "+-000", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
  EXPECT_FALSE(absl::ParseTime("%Ez", "+-0:00", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
  EXPECT_FALSE(absl::ParseTime("%z", "-00-0", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Illegal trailing data"));
  EXPECT_FALSE(absl::ParseTime("%Ez", "-00:-0", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Illegal trailing data"));

  // Check that we do not accept strings with embedded NULs.
  EXPECT_FALSE(
      absl::ParseTime("%Y", std::string("2026\0payload", 12), &t, &err));
  EXPECT_THAT(err, HasSubstr("Illegal trailing data"));

  // High-bit character test for sign-extension bugs.
  for (int i = 128; i < 256; ++i) {
    char c = static_cast<char>(i);
    std::string input = std::string(1, c) + "2015-01-02";
    EXPECT_FALSE(absl::ParseTime("%Y-%m-%d", input, &t, &err));
  }
}

TEST(ParseTime, ExtendedSeconds) {
  std::string err;
  absl::Time t;

  // Here is a "%E*S" case we got wrong for a while.  The fractional
  // part of the first instant is less than 2^31 and was correctly
  // parsed, while the second (and any subsecond field >=2^31) failed.
  t = absl::UnixEpoch();
  EXPECT_TRUE(absl::ParseTime("%E*S", "0.2147483647", &t, &err)) << err;
  EXPECT_EQ(absl::UnixEpoch() + absl::Nanoseconds(214748364) +
                absl::Nanoseconds(1) / 2,
            t);
  t = absl::UnixEpoch();
  EXPECT_TRUE(absl::ParseTime("%E*S", "0.2147483648", &t, &err)) << err;
  EXPECT_EQ(absl::UnixEpoch() + absl::Nanoseconds(214748364) +
                absl::Nanoseconds(3) / 4,
            t);

  // We should also be able to specify long strings of digits far
  // beyond the current resolution and have them convert the same way.
  t = absl::UnixEpoch();
  EXPECT_TRUE(absl::ParseTime(
      "%E*S", "0.214748364801234567890123456789012345678901234567890123456789",
      &t, &err))
      << err;
  EXPECT_EQ(absl::UnixEpoch() + absl::Nanoseconds(214748364) +
                absl::Nanoseconds(3) / 4,
            t);
}

TEST(ParseTime, ExtendedOffsetErrors) {
  std::string err;
  absl::Time t;

  // %z against +-HHMM.
  EXPECT_FALSE(absl::ParseTime("%z", "-123", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Illegal trailing data"));

  // %z against +-HH.
  EXPECT_FALSE(absl::ParseTime("%z", "-1", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));

  // %Ez against +-HH:MM.
  EXPECT_FALSE(absl::ParseTime("%Ez", "-12:3", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Illegal trailing data"));

  // %Ez against +-HHMM.
  EXPECT_FALSE(absl::ParseTime("%Ez", "-123", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Illegal trailing data"));

  // %Ez against +-HH.
  EXPECT_FALSE(absl::ParseTime("%Ez", "-1", &t, &err)) << err;
  EXPECT_THAT(err, HasSubstr("Failed to parse"));
}

TEST(ParseTime, InfiniteTime) {
  absl::Time t;
  std::string err;
  EXPECT_TRUE(absl::ParseTime("%H:%M blah", "infinite-future", &t, &err));
  EXPECT_EQ(absl::InfiniteFuture(), t);

  // Surrounding whitespace.
  EXPECT_TRUE(absl::ParseTime("%H:%M blah", "  infinite-future", &t, &err));
  EXPECT_EQ(absl::InfiniteFuture(), t);
  EXPECT_TRUE(absl::ParseTime("%H:%M blah", "infinite-future  ", &t, &err));
  EXPECT_EQ(absl::InfiniteFuture(), t);
  EXPECT_TRUE(absl::ParseTime("%H:%M blah", "  infinite-future  ", &t, &err));
  EXPECT_EQ(absl::InfiniteFuture(), t);

  EXPECT_TRUE(absl::ParseTime("%H:%M blah", "infinite-past", &t, &err));
  EXPECT_EQ(absl::InfinitePast(), t);

  // Surrounding whitespace.
  EXPECT_TRUE(absl::ParseTime("%H:%M blah", "  infinite-past", &t, &err));
  EXPECT_EQ(absl::InfinitePast(), t);
  EXPECT_TRUE(absl::ParseTime("%H:%M blah", "infinite-past  ", &t, &err));
  EXPECT_EQ(absl::InfinitePast(), t);
  EXPECT_TRUE(absl::ParseTime("%H:%M blah", "  infinite-past  ", &t, &err));
  EXPECT_EQ(absl::InfinitePast(), t);

  // "infinite-future" as literal string
  absl::TimeZone tz = absl::UTCTimeZone();
  EXPECT_TRUE(absl::ParseTime("infinite-future %H:%M", "infinite-future 03:04",
                              &t, &err));
  EXPECT_NE(absl::InfiniteFuture(), t);
  EXPECT_EQ(3, tz.At(t).cs.hour());
  EXPECT_EQ(4, tz.At(t).cs.minute());

  // "infinite-past" as literal string
  EXPECT_TRUE(
      absl::ParseTime("infinite-past %H:%M", "infinite-past 03:04", &t, &err));
  EXPECT_NE(absl::InfinitePast(), t);
  EXPECT_EQ(3, tz.At(t).cs.hour());
  EXPECT_EQ(4, tz.At(t).cs.minute());

  // The input doesn't match the format.
  EXPECT_FALSE(absl::ParseTime("infinite-future %H:%M", "03:04", &t, &err));
  EXPECT_FALSE(absl::ParseTime("infinite-past %H:%M", "03:04", &t, &err));
}

TEST(ParseTime, FailsOnUnrepresentableTime) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  absl::Time t;
  EXPECT_FALSE(
      absl::ParseTime("%Y-%m-%d", "-292277022657-01-27", utc, &t, nullptr));
  EXPECT_TRUE(
      absl::ParseTime("%Y-%m-%d", "-292277022657-01-28", utc, &t, nullptr));
  EXPECT_TRUE(
      absl::ParseTime("%Y-%m-%d", "292277026596-12-04", utc, &t, nullptr));
  EXPECT_FALSE(
      absl::ParseTime("%Y-%m-%d", "292277026596-12-05", utc, &t, nullptr));
}

//
// Roundtrip test for FormatTime()/ParseTime().
//

TEST(FormatParse, RoundTrip) {
  const absl::TimeZone lax =
      absl::time_internal::LoadTimeZone("America/Los_Angeles");
  const absl::Time in =
      absl::FromCivil(absl::CivilSecond(1977, 6, 28, 9, 8, 7), lax);
  const absl::Duration subseconds = absl::Nanoseconds(654321);
  std::string err;

  // RFC3339, which renders subseconds.
  {
    absl::Time out;
    const std::string s =
        absl::FormatTime(absl::RFC3339_full, in + subseconds, lax);
    EXPECT_TRUE(absl::ParseTime(absl::RFC3339_full, s, &out, &err))
        << s << ": " << err;
    EXPECT_EQ(in + subseconds, out);  // RFC3339_full includes %Ez
  }

  // RFC1123, which only does whole seconds.
  {
    absl::Time out;
    const std::string s = absl::FormatTime(absl::RFC1123_full, in, lax);
    EXPECT_TRUE(absl::ParseTime(absl::RFC1123_full, s, &out, &err))
        << s << ": " << err;
    EXPECT_EQ(in, out);  // RFC1123_full includes %z
  }

  // `absl::FormatTime()` falls back to strftime() for "%c", which appears to
  // work. On Windows, `absl::ParseTime()` falls back to std::get_time() which
  // appears to fail on "%c" (or at least on the "%c" text produced by
  // `strftime()`). This makes it fail the round-trip test.
  //
  // Under the emscripten compiler `absl::ParseTime() falls back to
  // `strptime()`, but that ends up using a different definition for "%c"
  // compared to `strftime()`, also causing the round-trip test to fail
  // (see https://github.com/kripken/emscripten/pull/7491).
#if !defined(_MSC_VER) && !defined(__EMSCRIPTEN__)
  // Even though we don't know what %c will produce, it should roundtrip,
  // but only in the 0-offset timezone.
  {
    absl::Time out;
    const std::string s = absl::FormatTime("%c", in, absl::UTCTimeZone());
    EXPECT_TRUE(absl::ParseTime("%c", s, &out, &err)) << s << ": " << err;
    EXPECT_EQ(in, out);
  }
#endif  // !_MSC_VER && !__EMSCRIPTEN__
}

TEST(FormatParse, RoundTripDistantFuture) {
  const absl::TimeZone tz = absl::UTCTimeZone();
  const absl::Time in =
      absl::FromUnixSeconds(std::numeric_limits<int64_t>::max());
  std::string err;

  absl::Time out;
  const std::string s = absl::FormatTime(absl::RFC3339_full, in, tz);
  EXPECT_TRUE(absl::ParseTime(absl::RFC3339_full, s, &out, &err))
      << s << ": " << err;
  EXPECT_EQ(in, out);
}

TEST(FormatParse, RoundTripDistantPast) {
  const absl::TimeZone tz = absl::UTCTimeZone();
  const absl::Time in =
      absl::FromUnixSeconds(std::numeric_limits<int64_t>::min());
  std::string err;

  absl::Time out;
  const std::string s = absl::FormatTime(absl::RFC3339_full, in, tz);
  EXPECT_TRUE(absl::ParseTime(absl::RFC3339_full, s, &out, &err))
      << s << ": " << err;
  EXPECT_EQ(in, out);
}

TEST(ParseTime, Rfc3339DifferentialEquivalence) {
  // Trailing space in the format string is semantically identical to the base
  // RFC3339 format in cctz::detail::parse() (which always skips trailing
  // whitespace), while bypassing the RFC3339 fast path in absl::ParseTime().
  const std::string ref_full = std::string(absl::RFC3339_full) + " ";
  const std::string ref_sec = std::string(absl::RFC3339_sec) + " ";
  const std::string copy_full = absl::RFC3339_full;
  const std::string copy_sec = absl::RFC3339_sec;

  const absl::TimeZone utc = absl::UTCTimeZone();
  const absl::TimeZone lax =
      absl::time_internal::LoadTimeZone("America/Los_Angeles");
  const absl::Time sentinel = absl::FromUnixSeconds(123456789);
  constexpr absl::string_view kErrSentinel = "unchanged";

  static constexpr absl::string_view kInputs[] = {
      // Standard valid timestamps (epoch, pre-epoch, extreme 4-digit years).
      "1970-01-01T00:00:00Z",
      "1970-01-01t00:00:00z",
      "1969-12-31T23:59:59Z",
      "1969-12-31T23:59:59.999999Z",
      "1969-12-31T23:59:59.999999999Z",
      "0000-01-01T00:00:00Z",
      "0000-02-29T12:34:56Z",
      "0000-03-01T00:00:00Z",
      "9999-12-31T23:59:59Z",
      "9999-12-31T23:59:59.999999999Z",
      "9999-12-31T23:59:59-23:59:59",
      "0000-01-01T00:00:00+23:59:59",

      // Leap year and calendar boundary cases.
      "1900-02-28T23:59:59Z",
      "1900-02-29T00:00:00Z",
      "2000-02-29T00:00:00Z",
      "2023-02-29T00:00:00Z",
      "2024-02-29T00:00:00Z",
      "2100-02-29T00:00:00Z",
      "2400-02-29T00:00:00Z",
      "2024-04-30T00:00:00Z",
      "2024-04-31T00:00:00Z",
      "2024-06-31T00:00:00Z",
      "2024-09-31T00:00:00Z",
      "2024-11-31T00:00:00Z",
      "2024-00-01T00:00:00Z",
      "2024-13-01T00:00:00Z",
      "2024-01-00T00:00:00Z",
      "2024-01-32T00:00:00Z",

      // Fractional seconds: 1..20 digits and quarter-nanosecond boundaries.
      "2024-05-06T07:08:09.0Z",
      "2024-05-06T07:08:09.5Z",
      "2024-05-06T07:08:09.12Z",
      "2024-05-06T07:08:09.123Z",
      "2024-05-06T07:08:09.1234Z",
      "2024-05-06T07:08:09.12345Z",
      "2024-05-06T07:08:09.123456Z",
      "2024-05-06T07:08:09.1234567Z",
      "2024-05-06T07:08:09.12345678Z",
      "2024-05-06T07:08:09.123456789Z",
      "2024-05-06T07:08:09.0000000000Z",
      "2024-05-06T07:08:09.0000000002Z",
      "2024-05-06T07:08:09.0000000003Z",
      "2024-05-06T07:08:09.0000000005Z",
      "2024-05-06T07:08:09.0000000007Z",
      "2024-05-06T07:08:09.0000000008Z",
      "2024-05-06T07:08:09.00000000024Z",
      "2024-05-06T07:08:09.00000000025Z",
      "2024-05-06T07:08:09.00000000049Z",
      "2024-05-06T07:08:09.00000000050Z",
      "2024-05-06T07:08:09.00000000074Z",
      "2024-05-06T07:08:09.00000000075Z",
      "2024-05-06T07:08:09.00000000099Z",
      "2024-05-06T07:08:09.000000000249999Z",
      "2024-05-06T07:08:09.00000000024999999999Z",
      "2024-05-06T07:08:09.2147483647Z",
      "2024-05-06T07:08:09.2147483648Z",
      "2024-05-06T07:08:09.99999999999999999999Z",
      "2024-05-06T07:08:09.123456789",
      "2024-05-06T07:08:09.1234567890",
      "2024-05-06T07:08:09.12345678901",
      "2024-05-06T07:08:09.12345678901234567890",
      "2024-05-06T07:08:09.Z",
      "2024-05-06T07:08:09.",

      // All valid and invalid %Ez offset forms.
      "2024-05-06T07:08:09+00:00",
      "2024-05-06T07:08:09-00:00",
      "2024-05-06T07:08:09+00",
      "2024-05-06T07:08:09-00",
      "2024-05-06T07:08:09+0000",
      "2024-05-06T07:08:09-0000",
      "2024-05-06T07:08:09+00:00:00",
      "2024-05-06T07:08:09-00:00:00",
      "2024-05-06T07:08:09+000000",
      "2024-05-06T07:08:09-000000",
      "2024-05-06T07:08:09+05:30",
      "2024-05-06T07:08:09-07:00",
      "2024-05-06T07:08:09+0530",
      "2024-05-06T07:08:09-0700",
      "2024-05-06T07:08:09+01:02:03",
      "2024-05-06T07:08:09-01:02:03",
      "2024-05-06T07:08:09+010203",
      "2024-05-06T07:08:09-010203",
      "2024-05-06T07:08:09+01:0203",
      "2024-05-06T07:08:09+0102:03",
      "2024-05-06T07:08:09+01:02 ",
      "2024-05-06T07:08:09+02",
      "2024-05-06T07:08:09-12",
      "2024-05-06T07:08:09+23:59:59",
      "2024-05-06T07:08:09-23:59:59",
      "2024-05-06T07:08:09+24:00",
      "2024-05-06T07:08:09-24:00",
      "2024-05-06T07:08:09+00:60",
      "2024-05-06T07:08:09+00:00:60",
      "2024-05-06T07:08:09+08:99",
      "2024-05-06T07:08:09+01::02",
      "2024-05-06T07:08:09+01020304",
      "2024-05-06T07:08:09+01:",
      "2024-05-06T07:08:09+01:2",
      "2024-05-06T07:08:09+01:23:",
      "2024-05-06T07:08:09+01:23:4",
      "2024-05-06T07:08:09+1",
      "2024-05-06T07:08:09-1",
      "2024-05-06T07:08:09+",
      "2024-05-06T07:08:09-",
      "2024-05-06T07:08:09+-08:00",
      "2024-05-06T07:08:09-00:-0",
      "2024-05-06T07:08:09",

      // Leap seconds and out-of-range time fields.
      "2016-12-31T23:59:60Z",
      "2016-12-31T23:59:60.999Z",
      "2016-12-31T23:59:60-05:00",
      "2016-12-31T23:59:61Z",
      "2024-05-06T24:00:00Z",
      "2024-05-06T00:60:00Z",

      // Fallback valid cases: non-4-digit years, single-digit fields,
      // infinities.
      "77-06-28T09:08:07Z",
      "999-01-01T00:00:00Z",
      "10000-01-01T00:00:00Z",
      "-0001-01-01T00:00:00Z",
      "-2024-01-01T00:00:00Z",
      "2024-1-2T3:4:5Z",
      "2024-01-2T03:04:05Z",
      "infinite-future",
      "  infinite-future  ",
      "   infinite-future  ",
      "infinite-past",
      "\tinfinite-past\n",

      // Leading/trailing whitespace (including >=20-byte whitespace-padded
      // short inputs), trailing junk, and malformed syntax.
      "                    ",
      "   2024-05-06T07:08:09",
      " 2024-05-06T07:08:09Z",
      "\t\n\r\v\f2024-05-06T07:08:09Z",
      "2024-05-06T07:08:09Z ",
      "2024-05-06T07:08:09Z \t\n\r\v\f",
      "  2024-05-06T07:08:09.123456-07:00  ",
      "2024-05-06T07:08:09Z extra",
      "2024-05-06T07:08:09Z0",
      "2024-05-06 07:08:09Z",
      "2024/05/06T07:08:09Z",
      "2024-05-06T07:08:09.123.456Z",
      "-000-01-01T00:00:00Z",
      "",
      "2024-05-06T07:08:0",
      "\x80"
      "2024-05-06T07:08:09Z",
      "2024-05-06T07:08:09Z\xa0",
      absl::string_view("2024-05-06T07:08:09Z\0", 21),
      absl::string_view("2024-05-06T07:08:09Z \0", 22),
      absl::string_view("2024-05-06\0T07:08:09Z", 21),
  };

  auto check_tz_variant =
      [&](absl::string_view label, absl::string_view ref_fmt,
          absl::string_view fast_ptr_fmt, absl::string_view fast_copy_fmt,
          absl::string_view input, absl::TimeZone tz) {
        absl::Time expected_t = sentinel;
        std::string expected_err(kErrSentinel);
        const bool expected_ok =
            absl::ParseTime(ref_fmt, input, tz, &expected_t, &expected_err);

        for (const absl::string_view fmt : {fast_ptr_fmt, fast_copy_fmt}) {
          absl::Time actual_t = sentinel;
          std::string actual_err(kErrSentinel);
          const bool actual_ok =
              absl::ParseTime(fmt, input, tz, &actual_t, &actual_err);
          EXPECT_EQ(actual_ok, expected_ok)
              << label << " tz=" << tz.name()
              << " input=" << testing::PrintToString(input);
          EXPECT_EQ(actual_t, expected_t)
              << label << " tz=" << tz.name()
              << " input=" << testing::PrintToString(input);
          EXPECT_EQ(actual_err, expected_err)
              << label << " tz=" << tz.name()
              << " input=" << testing::PrintToString(input);

          absl::Time null_err_t = sentinel;
          EXPECT_EQ(absl::ParseTime(fmt, input, tz, &null_err_t, nullptr),
                    expected_ok)
              << label << " nullptr err tz=" << tz.name()
              << " input=" << testing::PrintToString(input);
          EXPECT_EQ(null_err_t, expected_t)
              << label << " nullptr err tz=" << tz.name()
              << " input=" << testing::PrintToString(input);
        }
      };

  for (const absl::string_view input : kInputs) {
    for (const absl::TimeZone tz : {utc, lax}) {
      check_tz_variant("RFC3339_full", ref_full, absl::RFC3339_full, copy_full,
                       input, tz);
      check_tz_variant("RFC3339_sec", ref_sec, absl::RFC3339_sec, copy_sec,
                       input, tz);
    }

    // Exercise the 4-arg ParseTime overload and AbslParseFlag directly.
    absl::Time expected_full_t = sentinel;
    std::string expected_full_err(kErrSentinel);
    const bool expected_full_ok =
        absl::ParseTime(ref_full, input, &expected_full_t, &expected_full_err);

    absl::Time four_arg_full_t = sentinel;
    std::string four_arg_full_err(kErrSentinel);
    EXPECT_EQ(absl::ParseTime(absl::RFC3339_full, input, &four_arg_full_t,
                              &four_arg_full_err),
              expected_full_ok)
        << "4-arg RFC3339_full input=" << testing::PrintToString(input);
    EXPECT_EQ(four_arg_full_t, expected_full_t)
        << "4-arg RFC3339_full input=" << testing::PrintToString(input);
    EXPECT_EQ(four_arg_full_err, expected_full_err)
        << "4-arg RFC3339_full input=" << testing::PrintToString(input);

    absl::Time flag_t = sentinel;
    std::string flag_err(kErrSentinel);
    EXPECT_EQ(absl::AbslParseFlag(input, &flag_t, &flag_err), expected_full_ok)
        << "AbslParseFlag input=" << testing::PrintToString(input);
    EXPECT_EQ(flag_t, expected_full_t)
        << "AbslParseFlag input=" << testing::PrintToString(input);
    EXPECT_EQ(flag_err, expected_full_err)
        << "AbslParseFlag input=" << testing::PrintToString(input);

    absl::Time expected_sec_t = sentinel;
    std::string expected_sec_err(kErrSentinel);
    const bool expected_sec_ok =
        absl::ParseTime(ref_sec, input, &expected_sec_t, &expected_sec_err);

    absl::Time four_arg_sec_t = sentinel;
    std::string four_arg_sec_err(kErrSentinel);
    EXPECT_EQ(absl::ParseTime(absl::RFC3339_sec, input, &four_arg_sec_t,
                              &four_arg_sec_err),
              expected_sec_ok)
        << "4-arg RFC3339_sec input=" << testing::PrintToString(input);
    EXPECT_EQ(four_arg_sec_t, expected_sec_t)
        << "4-arg RFC3339_sec input=" << testing::PrintToString(input);
    EXPECT_EQ(four_arg_sec_err, expected_sec_err)
        << "4-arg RFC3339_sec input=" << testing::PrintToString(input);
  }
}

TEST(ParseTime, Rfc3339NullStringView) {
  const absl::Time sentinel = absl::FromUnixSeconds(42);
  absl::Time t = sentinel;
  std::string err;

  // Default-constructed string_view has data() == nullptr and size() == 0.
  const absl::string_view null_sv;
  ASSERT_EQ(null_sv.data(), nullptr);
  EXPECT_FALSE(absl::ParseTime(absl::RFC3339_full, null_sv, &t, &err));
  EXPECT_EQ(t, sentinel);
  EXPECT_FALSE(absl::ParseTime(absl::RFC3339_sec, null_sv, &t, &err));
  EXPECT_EQ(t, sentinel);
  EXPECT_FALSE(absl::AbslParseFlag(null_sv, &t, &err));
  EXPECT_EQ(t, sentinel);
}

TEST(ParseTime, Rfc3339NullTimeOnInvalidInput) {
  // Passing nullptr for `time` on invalid input (both <20-byte and >=20-byte
  // fast-path candidates) must not dereference `time` or form a null reference.
  static constexpr absl::string_view kInvalidInputs[] = {
      "not-a-time",
      "2024-05-06T07:08:09Z extra",
      "2024-02-30T07:08:09Z",
  };
  for (const absl::string_view input : kInvalidInputs) {
    std::string err;
    EXPECT_FALSE(absl::ParseTime(absl::RFC3339_full, input, nullptr, &err))
        << input;
    EXPECT_FALSE(err.empty()) << input;

    err.clear();
    EXPECT_FALSE(absl::ParseTime(absl::RFC3339_sec, input, absl::UTCTimeZone(),
                                 nullptr, &err))
        << input;
    EXPECT_FALSE(err.empty()) << input;

    err.clear();
    EXPECT_FALSE(
        absl::AbslParseFlag(input, static_cast<absl::Time*>(nullptr), &err))
        << input;
    EXPECT_FALSE(err.empty()) << input;
  }
}

TEST(ParseTime, Rfc3339EmbeddedNulFormatNotMatched) {
  // 24-byte and 22-byte formats with an embedded NUL must not match
  // RFC3339_full or RFC3339_sec.
  const absl::Time sentinel = absl::FromUnixSeconds(42);
  absl::Time t = sentinel;
  std::string err;
  const std::string nul_full_fmt("%Y-%m-%d%ET%H:%M:%E*S\0Ez", 24);
  const std::string nul_sec_fmt("%Y-%m-%d%ET%H:%M:%S\0Ez", 22);
  EXPECT_FALSE(absl::ParseTime(nul_full_fmt, "2024-05-06T07:08:09Z", &t, &err));
  EXPECT_EQ(t, sentinel);
  EXPECT_FALSE(absl::ParseTime(nul_sec_fmt, "2024-05-06T07:08:09Z", &t, &err));
  EXPECT_EQ(t, sentinel);
}

TEST(ParseTime, Rfc3339FormatSubsliceNotMatchedAsFull) {
  // Ensure pointer-based format matching still checks format.size() for both
  // RFC3339_full and RFC3339_sec.
  const absl::Time sentinel = absl::FromUnixSeconds(42);
  for (const absl::string_view date_only :
       {absl::string_view(absl::RFC3339_full, 8),
        absl::string_view(absl::RFC3339_sec, 8)}) {
    ASSERT_EQ(date_only, "%Y-%m-%d");
    absl::Time t = sentinel;
    std::string err;
    EXPECT_TRUE(absl::ParseTime(date_only, "2024-05-06", &t, &err)) << err;
    EXPECT_EQ(t,
              absl::FromCivil(absl::CivilDay(2024, 5, 6), absl::UTCTimeZone()));

    // A full >=20-byte RFC3339 timestamp must be rejected (not fast-pathed).
    t = sentinel;
    err.clear();
    EXPECT_FALSE(absl::ParseTime(date_only, "2024-05-06T07:08:09Z", &t, &err));
    EXPECT_EQ(t, sentinel);
    EXPECT_THAT(err, HasSubstr("Illegal trailing data"));
  }
}

TEST(ParseTime, Rfc3339CalendarExhaustive400YearCycle) {
  // Verify civil-to-Unix-seconds conversion across a full 400-year Gregorian
  // cycle (0000..0400) plus modern years (1968..2035).
  const absl::TimeZone utc = absl::UTCTimeZone();
  auto check_date = [&](int year, int month, int day) {
    const std::string input =
        absl::StrFormat("%04d-%02d-%02dT12:34:56Z", year, month, day);
    absl::Time t;
    std::string err;
    ASSERT_TRUE(absl::ParseTime(absl::RFC3339_sec, input, &t, &err))
        << input << ": " << err;
    EXPECT_EQ(t, absl::FromCivil(
                     absl::CivilSecond(year, month, day, 12, 34, 56), utc))
        << input;
  };

  for (int year = 0; year <= 400; ++year) {
    check_date(year, 1, 1);
    check_date(year, 2, 28);
    check_date(year, 3, 1);
    check_date(year, 12, 31);
    const bool is_leap = absl::CivilDay(year, 2, 29).month() == 2;
    const std::string feb29 = absl::StrFormat("%04d-02-29T00:00:00Z", year);
    absl::Time t;
    std::string err;
    if (is_leap) {
      ASSERT_TRUE(absl::ParseTime(absl::RFC3339_sec, feb29, &t, &err))
          << feb29 << ": " << err;
      EXPECT_EQ(t, absl::FromCivil(absl::CivilDay(year, 2, 29), utc)) << feb29;
    } else {
      EXPECT_FALSE(absl::ParseTime(absl::RFC3339_sec, feb29, &t, &err))
          << feb29;
    }
  }

  for (int year = 1968; year <= 2035; ++year) {
    for (int month = 1; month <= 12; ++month) {
      check_date(year, month, 1);
    }
  }
}

}  // namespace
