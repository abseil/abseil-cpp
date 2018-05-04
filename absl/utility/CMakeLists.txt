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


list(APPEND UTILITY_PUBLIC_HEADERS
  "utility.h"
)

absl_header_library(
  TARGET
    absl_utility
  PUBLIC_LIBRARIES
    absl::base
  EXPORT_NAME
    utility
)


#
## TESTS
#

# test utility_test
set(UTILITY_TEST_SRC "utility_test.cc")
set(UTILITY_TEST_PUBLIC_LIBRARIES
  absl::base
  absl::memory
  absl::strings
  absl::utility
)

absl_test(
  TARGET
    utility_test
  SOURCES
    ${UTILITY_TEST_SRC}
  PUBLIC_LIBRARIES
    ${UTILITY_TEST_PUBLIC_LIBRARIES}
)
