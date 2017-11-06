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
  "clock.h"
  "time.h"
)


list(APPEND TIME_INTERNAL_HEADERS
  "internal/test_util.h"
)

list(APPEND TIME_SRC
  "time.cc"
  "clock.cc"
  "duration.cc"
  "format.cc"
  ${TIME_PUBLIC_HEADERS}
  ${TIME_INTERNAL_HEADERS}
)
set(TIME_PUBLIC_LIBRARIES absl::base absl::stacktrace absl::int128 cctz)

absl_library(
  TARGET
    absl_time
  SOURCES
    ${TIME_SRC}
  PUBLIC_LIBRARIES
    ${TIME_PUBLIC_LIBRARIES}
  PUBLIC_INCLUDE_DIRS
    ${CCTZ_INCLUDE_DIRS}
  EXPORT_NAME
    time
)



#
## TESTS
#

# test time_test
list(APPEND TIME_TEST_SRC
  "time_test.cc"
  "clock_test.cc"
  "duration_test.cc"
  "format_test.cc"
  "time_norm_test.cc"
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


