#
# Copyright 2017 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set(TIME_HDRS
  "civil_time.h"
  "clock.h"
  "time.h"
  "internal/cctz/include/cctz/civil_time.h"
  "internal/cctz/include/cctz/civil_time_detail.h"
  "internal/cctz/include/cctz/time_zone.h"
  "internal/cctz/include/cctz/zone_info_source.h"
)

set(TIME_SRCS
  "civil_time.cc"
  "clock.cc"
  "duration.cc"
  "format.cc"
  "internal/get_current_time_chrono.inc"
  "internal/get_current_time_posix.inc"
  "time.cc"
  "internal/cctz/src/civil_time_detail.cc"
  "internal/cctz/src/time_zone_fixed.cc"
  "internal/cctz/src/time_zone_fixed.h"
  "internal/cctz/src/time_zone_format.cc"
  "internal/cctz/src/time_zone_if.cc"
  "internal/cctz/src/time_zone_if.h"
  "internal/cctz/src/time_zone_impl.cc"
  "internal/cctz/src/time_zone_impl.h"
  "internal/cctz/src/time_zone_info.cc"
  "internal/cctz/src/time_zone_info.h"
  "internal/cctz/src/time_zone_libc.cc"
  "internal/cctz/src/time_zone_libc.h"
  "internal/cctz/src/time_zone_lookup.cc"
  "internal/cctz/src/time_zone_posix.cc"
  "internal/cctz/src/time_zone_posix.h"
  "internal/cctz/src/tzfile.h"
  "internal/cctz/src/zone_info_source.cc"
)

absl_cc_library(
  NAME time
  SRCS ${TIME_SRCS}
  HDRS ${TIME_HDRS}
  DEPS
    absl::base
    absl::core_headers
    absl::int128
    absl::strings
  PUBLIC
)

#
## TESTS
#

absl_cc_library(
  NAME test_util
  SRCS
    "internal/test_util.cc"
    "internal/zoneinfo.inc"
  HDRS "internal/test_util.h"
  DEPS
    absl::time
    absl::base
    gtest
  TESTONLY
)

# test time_test
set(TIME_TEST_SRCS
  "civil_time_test.cc"
  "clock_test.cc"
  "duration_test.cc"
  "format_test.cc"
  "time_test.cc"
  "time_zone_test.cc"
)

absl_cc_test(
  NAME time_test
  SRCS ${TIME_TEST_SRCS}
  DEPS
    absl::test_util
    absl::time
    absl::base
    absl::config
    absl::core_headers
    gmock
    gtest_main
)

