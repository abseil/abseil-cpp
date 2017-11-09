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

list(APPEND ALGORITHM_PUBLIC_HEADERS
  "algorithm.h"
  "container.h"
)


#
## TESTS
#

# test algorithm_test
list(APPEND ALGORITHM_TEST_SRC
  "algorithm_test.cc"
  ${ALGORITHM_PUBLIC_HEADERS}
  ${ALGORITHM_INTERNAL_HEADERS}
)

absl_header_library(
  TARGET
    absl_algorithm
  EXPORT_NAME
    algorithm
)

absl_test(
  TARGET
    algorithm_test
  SOURCES
    ${ALGORITHM_TEST_SRC}
  PUBLIC_LIBRARIES
    absl::algorithm
)




# test container_test
set(CONTAINER_TEST_SRC "container_test.cc")

absl_test(
  TARGET
    container_test
  SOURCES
    ${CONTAINER_TEST_SRC}
  PUBLIC_LIBRARIES
    absl::algorithm
)
