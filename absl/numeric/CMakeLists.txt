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

list(APPEND NUMERIC_PUBLIC_HEADERS
  "int128.h"
)


# library 128
list(APPEND INT128_SRC
  "int128.cc"
  ${NUMERIC_PUBLIC_HEADERS}
)
absl_library(
  TARGET
    absl_int128
  SOURCES
    ${INT128_SRC}
  PUBLIC_LIBRARIES
    ${INT128_PUBLIC_LIBRARIES}
  EXPORT_NAME
    int128
)


absl_header_library(
  TARGET
    absl_numeric
  PUBLIC_LIBRARIES
    absl::int128
  EXPORT_NAME
    numeric
)

# test int128_test
set(INT128_TEST_SRC "int128_test.cc")
set(INT128_TEST_PUBLIC_LIBRARIES absl::numeric absl::base)

absl_test(
  TARGET
    int128_test
  SOURCES
    ${INT128_TEST_SRC}
  PUBLIC_LIBRARIES
    ${INT128_TEST_PUBLIC_LIBRARIES}
)



