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

list(APPEND TIME_PUBLIC_HEADERS
  "civil_time.h"
  "clock.h"
  "time.h"
)


list(APPEND TIME_INTERNAL_HEADERS
  "internal/test_util.h"
  "internal/cctz/include/cctz/civil_time.h"
  "internal/cctz/include/cctz/civil_time_detail.h"
  "internal/cctz/include/cctz/time_zone.h"
  "internal/cctz/include/cctz/zone_info_source.h"
)

list(APPEND TIME_SRC
  "civil_time.cc"
  "time.cc"
  "clock.cc"
  "duration.cc"
  "format.cc"
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
  ${TIME_PUBLIC_HEADERS}
  ${TIME_INTERNAL_HEADERS}
)
set(TIME_PUBLIC_LIBRARIES absl::base absl::stacktrace absl::int128 absl::strings)

absl_library(
  TARGET
    absl_time
  SOURCES
    ${TIME_SRC}
  PUBLIC_LIBRARIES
    ${TIME_PUBLIC_LIBRARIES}
  EXPORT_NAME
    time
)



#
## TESTS
#

# test time_test
list(APPEND TIME_TEST_SRC
  "civil_time_test.cc"
  "time_test.cc"
  "clock_test.cc"
  "duration_test.cc"
  "format_test.cc"
  "time_test.cc"
  "time_zone_test.cc"
  "internal/test_util.cc"
)
set(TIME_TEST_PUBLIC_LIBRARIES absl::time)

absl_test(
  TARGET
    time_test
  SOURCES
    ${TIME_TEST_SRC}
  PUBLIC_LIBRARIES
    ${TIME_TEST_PUBLIC_LIBRARIES}
)

