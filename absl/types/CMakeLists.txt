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

list(APPEND TYPES_PUBLIC_HEADERS
  "any.h"
  "bad_any_cast.h"
  "bad_optional_access.h"
  "optional.h"
  "span.h"
  "variant.h"
)


# any library
absl_header_library(
  TARGET
    absl_any
  PUBLIC_LIBRARIES
    absl::bad_any_cast
    absl::base
    absl::meta
    absl::utility
  PRIVATE_COMPILE_FLAGS
    ${ABSL_EXCEPTIONS_FLAG}
  EXPORT_NAME
    any
)

# span library
absl_header_library(
  TARGET
    absl_span
  PUBLIC_LIBRARIES
    absl::utility
  EXPORT_NAME
    span
)


# bad_any_cast library
list(APPEND BAD_ANY_CAST_SRC
  "bad_any_cast.cc"
  ${TYPES_PUBLIC_HEADERS}
)

absl_library(
  TARGET
    absl_bad_any_cast
  SOURCES
    ${BAD_ANY_CAST_SRC}
  PUBLIC_LIBRARIES
  EXPORT_NAME
    bad_any_cast
)


# optional library
list(APPEND OPTIONAL_SRC
  "optional.cc"
)

absl_library(
  TARGET
    absl_optional
  SOURCES
    ${OPTIONAL_SRC}
  PUBLIC_LIBRARIES
    absl::bad_optional_access
    absl::base
    absl::memory
    absl::meta
    absl::utility
  EXPORT_NAME
    optional
)


set(BAD_OPTIONAL_ACCESS_SRC "bad_optional_access.cc")
set(BAD_OPTIONAL_ACCESS_LIBRARIES absl::base)

absl_library(
  TARGET
    absl_bad_optional_access
  SOURCES
    ${BAD_OPTIONAL_ACCESS_SRC}
  PUBLIC_LIBRARIES
    ${BAD_OPTIONAL_ACCESS_PUBLIC_LIBRARIES}
  EXPORT_NAME
    bad_optional_access
)

# variant library
absl_library(
  TARGET
    absl_variant
  SOURCES
    "bad_variant_access.h" "bad_variant_access.cc" "variant.h" "internal/variant.h"
  PUBLIC_LIBRARIES
    absl::base absl::meta absl::utility
  PRIVATE_COMPILE_FLAGS
    ${ABSL_EXCEPTIONS_FLAG}
  EXPORT_NAME
    variant
)

#
## TESTS
#


# test any_test
set(ANY_TEST_SRC "any_test.cc")
set(ANY_TEST_PUBLIC_LIBRARIES absl::base absl_internal_throw_delegate absl::any absl::bad_any_cast absl::test_instance_tracker)

absl_test(
  TARGET
    any_test
  SOURCES
    ${ANY_TEST_SRC}
  PUBLIC_LIBRARIES
    ${ANY_TEST_PUBLIC_LIBRARIES}
  PRIVATE_COMPILE_FLAGS
    ${ABSL_EXCEPTIONS_FLAG}
)


# test any_test_noexceptions
absl_test(
  TARGET
    any_test_noexceptions
  SOURCES
    ${ANY_TEST_SRC}
  PUBLIC_LIBRARIES
    ${ANY_TEST_PUBLIC_LIBRARIES}
)

# test any_exception_safety_test
set(ANY_EXCEPTION_SAFETY_TEST_SRC "any_exception_safety_test.cc")
set(ANY_EXCEPTION_SAFETY_TEST_PUBLIC_LIBRARIES
  absl::any
  absl::base
  absl_internal_exception_safety_testing
)

absl_test(
  TARGET
    any_exception_safety_test
  SOURCES
    ${ANY_EXCEPTION_SAFETY_TEST_SRC}
  PUBLIC_LIBRARIES
    ${ANY_EXCEPTION_SAFETY_TEST_PUBLIC_LIBRARIES}
  PRIVATE_COMPILE_FLAGS
    ${ABSL_EXCEPTIONS_FLAG}
)


# test span_test
set(SPAN_TEST_SRC "span_test.cc")
set(SPAN_TEST_PUBLIC_LIBRARIES absl::base absl::strings absl_internal_throw_delegate absl::span absl::test_instance_tracker)

absl_test(
  TARGET
    span_test
  SOURCES
    ${SPAN_TEST_SRC}
  PUBLIC_LIBRARIES
    ${SPAN_TEST_PUBLIC_LIBRARIES}
  PRIVATE_COMPILE_FLAGS
    ${ABSL_EXCEPTIONS_FLAG}
)


# test span_test_noexceptions
absl_test(
  TARGET
    span_test_noexceptions
  SOURCES
    ${SPAN_TEST_SRC}
  PUBLIC_LIBRARIES
    ${SPAN_TEST_PUBLIC_LIBRARIES}
)



# test optional_test
set(OPTIONAL_TEST_SRC "optional_test.cc")
set(OPTIONAL_TEST_PUBLIC_LIBRARIES absl::base absl_internal_throw_delegate absl::optional absl_bad_optional_access)

absl_test(
  TARGET
    optional_test
  SOURCES
    ${OPTIONAL_TEST_SRC}
  PUBLIC_LIBRARIES
    ${OPTIONAL_TEST_PUBLIC_LIBRARIES}
)


# test optional_exception_safety_test
set(OPTIONAL_EXCEPTION_SAFETY_TEST_SRC "optional_exception_safety_test.cc")
set(OPTIONAL_EXCEPTION_SAFETY_TEST_PUBLIC_LIBRARIES
  absl::optional
  absl_internal_exception_safety_testing
)

absl_test(
  TARGET
    optional_exception_safety_test
  SOURCES
    ${OPTIONAL_EXCEPTION_SAFETY_TEST_SRC}
  PUBLIC_LIBRARIES
    ${OPTIONAL_EXCEPTION_SAFETY_TEST_PUBLIC_LIBRARIES}
  PRIVATE_COMPILE_FLAGS
    ${ABSL_EXCEPTIONS_FLAG}
)
