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

#ifndef ABSL_TIME_INTERNAL_TEST_UTIL_H_
#define ABSL_TIME_INTERNAL_TEST_UTIL_H_

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/time/time.h"

// This helper is a macro so that failed expectations show up with the
// correct line numbers.
//
// This is for internal testing of the Base Time library itself. This is not
// part of a public API.
#define ABSL_INTERNAL_EXPECT_TIME(bd, y, m, d, h, min, s, off, isdst)     \
  do {                                                                    \
    EXPECT_EQ(y, bd.year);                                                \
    EXPECT_EQ(m, bd.month);                                               \
    EXPECT_EQ(d, bd.day);                                                 \
    EXPECT_EQ(h, bd.hour);                                                \
    EXPECT_EQ(min, bd.minute);                                            \
    EXPECT_EQ(s, bd.second);                                              \
    EXPECT_EQ(off, bd.offset);                                            \
    EXPECT_EQ(isdst, bd.is_dst);                                          \
    EXPECT_THAT(bd.zone_abbr,                                             \
                testing::MatchesRegex(absl::time_internal::kZoneAbbrRE)); \
  } while (0)

namespace absl {
namespace time_internal {

// A regular expression that matches all zone abbreviations (%Z).
extern const char kZoneAbbrRE[];

// Loads the named timezone, but dies on any failure.
absl::TimeZone LoadTimeZone(const std::string& name);

}  // namespace time_internal
}  // namespace absl

#endif  // ABSL_TIME_INTERNAL_TEST_UTIL_H_
