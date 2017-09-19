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

// This file contains tests for FromDateTime() normalization, which is
// time-zone independent so we just use UTC throughout.

#include <cstdint>
#include <limits>

#include "gtest/gtest.h"
#include "absl/time/internal/test_util.h"
#include "absl/time/time.h"

namespace {

TEST(TimeNormCase, SimpleOverflow) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  absl::TimeConversion tc =
      absl::ConvertDateTime(2013, 11, 15, 16, 32, 59 + 1, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  absl::Time::Breakdown bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 11, 15, 16, 33, 0, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 15, 16, 59 + 1, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 11, 15, 17, 0, 14, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 15, 23 + 1, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 11, 16, 0, 32, 14, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 30 + 1, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 12, 1, 16, 32, 14, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 12 + 1, 15, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2014, 1, 15, 16, 32, 14, 0, false, "UTC");
}

TEST(TimeNormCase, SimpleUnderflow) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  absl::TimeConversion tc = ConvertDateTime(2013, 11, 15, 16, 32, 0 - 1, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  absl::Time::Breakdown bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 11, 15, 16, 31, 59, 0, false, "UTC");

  tc = ConvertDateTime(2013, 11, 15, 16, 0 - 1, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 11, 15, 15, 59, 14, 0, false, "UTC");

  tc = ConvertDateTime(2013, 11, 15, 0 - 1, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 11, 14, 23, 32, 14, 0, false, "UTC");

  tc = ConvertDateTime(2013, 11, 1 - 1, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 10, 31, 16, 32, 14, 0, false, "UTC");

  tc = ConvertDateTime(2013, 1 - 1, 15, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2012, 12, 15, 16, 32, 14, 0, false, "UTC");
}

TEST(TimeNormCase, MultipleOverflow) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  absl::TimeConversion tc = ConvertDateTime(2013, 12, 31, 23, 59, 59 + 1, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  absl::Time::Breakdown bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2014, 1, 1, 0, 0, 0, 0, false, "UTC");
}

TEST(TimeNormCase, MultipleUnderflow) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  absl::TimeConversion tc = absl::ConvertDateTime(2014, 1, 1, 0, 0, 0 - 1, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  absl::Time::Breakdown bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 12, 31, 23, 59, 59, 0, false, "UTC");
}

TEST(TimeNormCase, OverflowLimits) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  absl::TimeConversion tc;
  absl::Time::Breakdown bd;

  const int kintmax = std::numeric_limits<int>::max();
  tc = absl::ConvertDateTime(0, kintmax, kintmax, kintmax, kintmax, kintmax,
                             utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 185085715, 11, 27, 12, 21, 7, 0, false, "UTC");

  const int kintmin = std::numeric_limits<int>::min();
  tc = absl::ConvertDateTime(0, kintmin, kintmin, kintmin, kintmin, kintmin,
                             utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, -185085717, 10, 31, 10, 37, 52, 0, false,
                            "UTC");

  const int64_t max_year = std::numeric_limits<int64_t>::max();
  tc = absl::ConvertDateTime(max_year, 12, 31, 23, 59, 59, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  EXPECT_EQ(absl::InfiniteFuture(), tc.pre);

  const int64_t min_year = std::numeric_limits<int64_t>::min();
  tc = absl::ConvertDateTime(min_year, 1, 1, 0, 0, 0, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  EXPECT_EQ(absl::InfinitePast(), tc.pre);
}

TEST(TimeNormCase, ComplexOverflow) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  absl::TimeConversion tc =
      ConvertDateTime(2013, 11, 15, 16, 32, 14 + 123456789, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  absl::Time::Breakdown bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2017, 10, 14, 14, 5, 23, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 15, 16, 32 + 1234567, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2016, 3, 22, 0, 39, 14, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 15, 16 + 123456, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2027, 12, 16, 16, 32, 14, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 15 + 1234, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2017, 4, 2, 16, 32, 14, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11 + 123, 15, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2024, 2, 15, 16, 32, 14, 0, false, "UTC");
}

TEST(TimeNormCase, ComplexUnderflow) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  absl::TimeConversion tc =
      absl::ConvertDateTime(1999, 3, 0, 0, 0, 0, utc);  // year 400
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  absl::Time::Breakdown bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 1999, 2, 28, 0, 0, 0, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 15, 16, 32, 14 - 123456789, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2009, 12, 17, 18, 59, 5, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 15, 16, 32 - 1234567, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2011, 7, 12, 8, 25, 14, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 15, 16 - 123456, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 1999, 10, 16, 16, 32, 14, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11, 15 - 1234, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2010, 6, 30, 16, 32, 14, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11 - 123, 15, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2003, 8, 15, 16, 32, 14, 0, false, "UTC");
}

TEST(TimeNormCase, Mishmash) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  absl::TimeConversion tc =
      absl::ConvertDateTime(2013, 11 - 123, 15 + 1234, 16 - 123456,
                            32 + 1234567, 14 - 123456789, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  absl::Time::Breakdown bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 1991, 5, 9, 3, 6, 5, 0, false, "UTC");

  tc = absl::ConvertDateTime(2013, 11 + 123, 15 - 1234, 16 + 123456,
                             32 - 1234567, 14 + 123456789, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2036, 5, 24, 5, 58, 23, 0, false, "UTC");

  // Here is a normalization case we got wrong for a while.  Because the
  // day is converted to "1" within a 400-year (146097-day) period, we
  // didn't need to roll the month and so we didn't mark it as normalized.
  tc = absl::ConvertDateTime(2013, 11, -146097 + 1, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 1613, 11, 1, 16, 32, 14, 0, false, "UTC");

  // Even though the month overflow compensates for the day underflow,
  // this should still be marked as normalized.
  tc = absl::ConvertDateTime(2013, 11 + 400 * 12, -146097 + 1, 16, 32, 14, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 11, 1, 16, 32, 14, 0, false, "UTC");
}

TEST(TimeNormCase, LeapYears) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  absl::TimeConversion tc =
      absl::ConvertDateTime(2013, 2, 28 + 1, 0, 0, 0, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  absl::Time::Breakdown bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2013, 3, 1, 0, 0, 0, 0, false, "UTC");

  tc = absl::ConvertDateTime(2012, 2, 28 + 1, 0, 0, 0, utc);
  EXPECT_FALSE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2012, 2, 29, 0, 0, 0, 0, false, "UTC");

  tc = absl::ConvertDateTime(2000, 2, 28 + 1, 0, 0, 0, utc);
  EXPECT_FALSE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 2000, 2, 29, 0, 0, 0, 0, false, "UTC");

  tc = absl::ConvertDateTime(1900, 2, 28 + 1, 0, 0, 0, utc);
  EXPECT_TRUE(tc.normalized);
  EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
  bd = tc.pre.In(utc);
  ABSL_INTERNAL_EXPECT_TIME(bd, 1900, 3, 1, 0, 0, 0, 0, false, "UTC");
}

// Convert all the days from 1970-1-1 to 1970-1-146097 (aka 2369-12-31)
// and check that they normalize to the expected time.  146097 days span
// the 400-year Gregorian cycle used during normalization.
TEST(TimeNormCase, AllTheDays) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  absl::Time exp_time = absl::UnixEpoch();

  for (int day = 1; day <= 146097; ++day) {
    absl::TimeConversion tc = absl::ConvertDateTime(1970, 1, day, 0, 0, 0, utc);
    EXPECT_EQ(day > 31, tc.normalized);
    EXPECT_EQ(absl::TimeConversion::UNIQUE, tc.kind);
    EXPECT_EQ(exp_time, tc.pre);
    exp_time += absl::Hours(24);
  }
}

}  // namespace
