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

#include "absl/time/internal/cctz/include/cctz/time_zone.h"

#include "gtest/gtest.h"
#include "absl/time/internal/test_util.h"
#include "absl/time/time.h"

namespace cctz = absl::time_internal::cctz;

namespace {

TEST(TimeZone, ValueSemantics) {
  absl::TimeZone tz;
  absl::TimeZone tz2 = tz;  // Copy-construct
  EXPECT_EQ(tz, tz2);
  tz2 = tz;  // Copy-assign
  EXPECT_EQ(tz, tz2);
}

TEST(TimeZone, Equality) {
  absl::TimeZone a, b;
  EXPECT_EQ(a, b);
  EXPECT_EQ(a.name(), b.name());

  absl::TimeZone implicit_utc;
  absl::TimeZone explicit_utc = absl::UTCTimeZone();
  EXPECT_EQ(implicit_utc, explicit_utc);
  EXPECT_EQ(implicit_utc.name(), explicit_utc.name());

  absl::TimeZone la = absl::time_internal::LoadTimeZone("America/Los_Angeles");
  absl::TimeZone nyc = absl::time_internal::LoadTimeZone("America/New_York");
  EXPECT_NE(la, nyc);
}

TEST(TimeZone, CCTZConversion) {
  const cctz::time_zone cz = cctz::utc_time_zone();
  const absl::TimeZone tz(cz);
  EXPECT_EQ(cz, cctz::time_zone(tz));
}

TEST(TimeZone, DefaultTimeZones) {
  absl::TimeZone tz;
  EXPECT_EQ("UTC", absl::TimeZone().name());
  EXPECT_EQ("UTC", absl::UTCTimeZone().name());
}

TEST(TimeZone, FixedTimeZone) {
  const absl::TimeZone tz = absl::FixedTimeZone(123);
  const cctz::time_zone cz = cctz::fixed_time_zone(cctz::seconds(123));
  EXPECT_EQ(tz, absl::TimeZone(cz));
}

TEST(TimeZone, LocalTimeZone) {
  const absl::TimeZone local_tz = absl::LocalTimeZone();
  absl::TimeZone tz = absl::time_internal::LoadTimeZone("localtime");
  EXPECT_EQ(tz, local_tz);
}

TEST(TimeZone, NamedTimeZones) {
  absl::TimeZone nyc = absl::time_internal::LoadTimeZone("America/New_York");
  EXPECT_EQ("America/New_York", nyc.name());
  absl::TimeZone syd = absl::time_internal::LoadTimeZone("Australia/Sydney");
  EXPECT_EQ("Australia/Sydney", syd.name());
  absl::TimeZone fixed = absl::FixedTimeZone((((3 * 60) + 25) * 60) + 45);
  EXPECT_EQ("Fixed/UTC+03:25:45", fixed.name());
}

TEST(TimeZone, Failures) {
  absl::TimeZone tz = absl::time_internal::LoadTimeZone("America/Los_Angeles");
  EXPECT_FALSE(LoadTimeZone("Invalid/TimeZone", &tz));
  EXPECT_EQ(absl::UTCTimeZone(), tz);  // guaranteed fallback to UTC

  // Ensures that the load still fails on a subsequent attempt.
  tz = absl::time_internal::LoadTimeZone("America/Los_Angeles");
  EXPECT_FALSE(LoadTimeZone("Invalid/TimeZone", &tz));
  EXPECT_EQ(absl::UTCTimeZone(), tz);  // guaranteed fallback to UTC

  // Loading an empty string timezone should fail.
  tz = absl::time_internal::LoadTimeZone("America/Los_Angeles");
  EXPECT_FALSE(LoadTimeZone("", &tz));
  EXPECT_EQ(absl::UTCTimeZone(), tz);  // guaranteed fallback to UTC
}

TEST(TimeZone, StaticZoneInfoFiles) {
  // This is a copy of Etc/GMT-4.
  const std::vector<char> zoneinfo {
    0x54, 0x5a, 0x69, 0x66, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x38, 0x40,
    0x00, 0x00, 0x2b, 0x30, 0x34, 0x00, 0x54, 0x5a, 0x69, 0x66, 0x32, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x38, 0x40, 0x00, 0x00, 0x2b, 0x30, 0x34, 0x00,
    0x0a, 0x3c, 0x2b, 0x30, 0x34, 0x3e, 0x2d, 0x34, 0x0a
  };

  // Pick a time zone name we know isn't real and won't exist on the system.
  const std::string name = "Test/GMT-4";
  
  // Provide a zoneinfo file for Test/GMT-4.
  absl::LoadStaticZoneInfoFile(name, { zoneinfo.data(), zoneinfo.size() });

  // Now, loading the Test/GMT-4 zone should succeed.
  auto tz = absl::time_internal::LoadTimeZone("America/Los_Angeles");
  EXPECT_TRUE(LoadTimeZone(name, &tz));
  EXPECT_EQ(name, tz.name());

  // Remove the definition for Test/GMT-4.
  absl::UnloadStaticZoneInfoFiles();
}

}  // namespace
