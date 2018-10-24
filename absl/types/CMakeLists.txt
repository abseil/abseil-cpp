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

# any library
absl_cc_library(
  NAME any
  HDRS "any.h"
  DEPS
    absl::bad_any_cast
    absl::config
    absl::core_headers
    absl::type_traits
    absl::utility
  PUBLIC
)

# bad_any_cast library
absl_cc_library(
  NAME bad_any_cast
  HDRS "bad_any_cast.h"
  DEPS
    absl::bad_any_cast_impl
    absl::config
  PUBLIC
)

absl_cc_library(
  NAME bad_any_cast_impl
  SRCS
    "bad_any_cast.cc"
    "bad_any_cast.h"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::base
    absl::config
  PUBLIC
)

# any test
absl_cc_test(
  NAME any_test
  SRCS "any_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::any
    absl::base
    absl::config
    absl::exception_testing
    absl::test_instance_tracker
    gtest_main
)

# any_test_noexceptions test
absl_cc_test(
  NAME any_test_noexceptions
  SRCS "any_test.cc"
  DEPS
    absl::any
    absl::base
    absl::config
    absl::exception_testing
    absl::test_instance_tracker
    gtest_main
)

# any_exception_safety_test
absl_cc_test(
  NAME any_exception_safety_test
  SRCS "any_exception_safety_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::any
    absl::exception_safety_testing
    gtest_main
)

# span library
absl_cc_library(
  NAME span
  HDRS "span.h"
  DEPS
    absl::algorithm
    absl::core_headers
    absl::throw_delegate
    absl::type_traits
  PUBLIC
)

absl_cc_test(
  NAME span_test
  SRCS "span_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::span
    absl::config
    absl::core_headers
    absl::exception_testing
    absl::fixed_array
    absl::inlined_vector
    absl::hash_testing
    absl::strings
    gmock
    gtest_main
)

# test span_test_noexceptions
absl_cc_test(
  NAME span_test_noexceptions
  SRCS "span_test.cc"
  DEPS
    absl::span
    absl::config
    absl::core_headers
    absl::exception_testing
    absl::fixed_array
    absl::inlined_vector
    absl::hash_testing
    absl::strings
    gmock
    gtest_main
)

# optional library
absl_cc_library(
  NAME optional
  SRCS "optional.cc"
  HDRS "optional.h"
  DEPS
    absl::bad_optional_access
    absl::config
    absl::memory
    absl::type_traits
    absl::utility
  PUBLIC
)

absl_cc_library(
  NAME bad_optional_access
  SRCS "bad_optional_access.cc"
  HDRS "bad_optional_access.h"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::base
    absl::config
  PUBLIC
)

absl_cc_library(
  NAME bad_variant_access
  SRCS "bad_variant_access.cc"
  HDRS "bad_variant_access.h"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::base
    absl::config
  PUBLIC
)

# optional test
absl_cc_test(
  NAME optional_test
  SRCS "optional_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::optional
    absl::base
    absl::config
    absl::type_traits
    absl::strings
    gtest_main
)

absl_cc_test(
  NAME optional_exception_safety_test
  SRCS "optional_exception_safety_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::optional
    absl::exception_safety_testing
    gtest_main
)

# variant library
absl_cc_library(
  NAME variant
  HDRS
    "variant.h"
    "internal/variant.h"
  DEPS
    absl::bad_variant_access
    absl::base_internal
    absl::config
    absl::core_headers
    absl::type_traits
    absl::utility
  PUBLIC
)

# variant test
absl_cc_test(
  NAME variant_test
  SRCS "variant_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::variant
    absl::config
    absl::core_headers
    absl::memory
    absl::type_traits
    absl::strings
    gmock
    gtest_main
)

absl_cc_test(
  NAME variant_exception_safety_test
  SRCS "variant_exception_safety_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::variant
    absl::exception_safety_testing
    absl::memory
    gmock
    gtest_main
)
