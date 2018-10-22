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

list(APPEND MEMORY_PUBLIC_HEADERS
  "memory.h"
)


absl_header_library(
  TARGET
    absl_memory
  EXPORT_NAME
    memory
)

#
## TESTS
#

# test memory_test
list(APPEND MEMORY_TEST_SRC
  "memory_test.cc"
  ${MEMORY_PUBLIC_HEADERS}
)
set(MEMORY_TEST_PUBLIC_LIBRARIES absl::base absl::memory)



absl_test(
  TARGET
    memory_test
  SOURCES
    ${MEMORY_TEST_SRC}
  PUBLIC_LIBRARIES
    ${MEMORY_TEST_PUBLIC_LIBRARIES}
)


# test memory_exception_safety_test
set(MEMORY_EXCEPTION_SAFETY_TEST_SRC "memory_exception_safety_test.cc")
set(MEMORY_EXCEPTION_SAFETY_TEST_PUBLIC_LIBRARIES
  absl::memory
  absl_internal_exception_safety_testing
)

absl_test(
  TARGET
    memory_exception_safety_test
  SOURCES
    ${MEMORY_EXCEPTION_SAFETY_TEST_SRC}
  PUBLIC_LIBRARIES
    ${MEMORY_EXCEPTION_SAFETY_TEST_PUBLIC_LIBRARIES}
  PRIVATE_COMPILE_FLAGS
    ${ABSL_EXCEPTIONS_FLAG}
)
