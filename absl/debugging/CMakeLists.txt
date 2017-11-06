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

list(APPEND DEBUGGING_PUBLIC_HEADERS
  "leak_check.h"
  "stacktrace.h"
)


list(APPEND DEBUGGING_INTERNAL_HEADERS
  "internal/address_is_readable.h"
  "internal/elf_mem_image.h"
  "internal/stacktrace_config.h"
  "internal/vdso_support.h"
)


list(APPEND STACKTRACE_SRC
  "stacktrace.cc"
  "internal/address_is_readable.cc"
  "internal/elf_mem_image.cc"
  "internal/vdso_support.cc"
  ${DEBUGGING_PUBLIC_HEADERS}
  ${DEBUGGING_INTERNAL_HEADERS}
)

absl_library(
  TARGET
    absl_stacktrace
  SOURCES
    ${STACKTRACE_SRC}
  EXPORT_NAME
    stacktrace
)


list(APPEND LEAK_CHECK_SRC
  "leak_check.cc"
)


# leak_check library
absl_library(
  TARGET
    absl_leak_check
  SOURCES
    ${LEAK_CHECK_SRC}
  PUBLIC_LIBRARIES
    absl_base
  EXPORT_NAME
    leak_check
)


# component target
absl_header_library(
  TARGET
    absl_debugging
  PUBLIC_LIBRARIES
    absl_stacktrace absl_leak_check
  EXPORT_NAME
    debugging
)

#
## TESTS
#

# test leak_check_test
list(APPEND LEAK_CHECK_TEST_SRC "leak_check_test.cc")

absl_test(
  TARGET
    leak_check_test
  SOURCES
    ${LEAK_CHECK_TEST_SRC}
  PUBLIC_LIBRARIES
    absl_leak_check
)

