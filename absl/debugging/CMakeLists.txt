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

# stacktrace library
absl_cc_library(
  NAME stacktrace
  SRCS "stacktrace.cc"
  HDRS "stacktrace.h"
  DEPS
    absl::base
    absl::core_headers
    absl::debugging_internal
  PUBLIC
)

# symbolize library
set(SYMBOLIZE_HDRS
  "symbolize.h"
  "internal/symbolize.h"
)

set(SYMBOLIZE_SRCS
  "symbolize.cc"
  "symbolize_elf.inc"
  "symbolize_unimplemented.inc"
  "symbolize_win32.inc"
)

absl_cc_library(
  NAME symbolize
  SRCS ${SYMBOLIZE_SRCS}
  HDRS ${SYMBOLIZE_HDRS}
  DEPS
    absl::debugging_internal
    absl::demangle_internal
    absl::base
    absl::core_headers
    absl::malloc_internal
  PUBLIC
)

absl_cc_test(
  NAME symbolize_test
  SRCS "symbolize_test.cc"
  DEPS
    absl::stack_consumption
    absl::symbolize
    absl::base
    absl::core_headers
    absl::memory
    gmock
    gtest
)

# examine_stack library
absl_cc_library(
  NAME examine_stack
  SRCS "internal/examine_stack.cc"
  HDRS "internal/examine_stack.h"
  DEPS
    absl::stacktrace
    absl::symbolize
    absl::base
    absl::core_headers
  PUBLIC
)

# signal_handler library
absl_cc_library(
  NAME failure_signal_handler
  SRCS "failure_signal_handler.cc"
  HDRS "failure_signal_handler.h"
  DEPS
    absl::examine_stack
    absl::stacktrace
    absl::base
    absl::config
    absl::core_headers
  PUBLIC
)

absl_cc_test(
  NAME failure_signal_handler_test
  SRCS "failure_signal_handler_test.cc"
  DEPS
    absl::failure_signal_handler
    absl::stacktrace
    absl::symbolize
    absl::base
    absl::strings
    gtest
)

# debugging_internal library
set(DEBUGGING_INTERNAL_HRDS
  "internal/address_is_readable.h"
  "internal/elf_mem_image.h"
  "internal/stacktrace_aarch64-inl.inc"
  "internal/stacktrace_arm-inl.inc"
  "internal/stacktrace_config.h"
  "internal/stacktrace_generic-inl.inc"
  "internal/stacktrace_powerpc-inl.inc"
  "internal/stacktrace_unimplemented-inl.inc"
  "internal/stacktrace_win32-inl.inc"
  "internal/stacktrace_x86-inl.inc"
  "internal/vdso_support.h"
)

set(DEBUGGING_INTERNAL_SRCS
  "internal/address_is_readable.cc"
  "internal/elf_mem_image.cc"
  "internal/vdso_support.cc"
)

absl_cc_library(
  NAME debugging_internal
  SRCS ${DEBUGGING_INTERNAL_SRCS}
  HDRS ${DEBUGGING_INTERNAL_HRDS}
  DEPS
    absl::base
    absl::dynamic_annotations
  PUBLIC
)

# demangle_internal library
absl_cc_library(
  NAME demangle_internal
  SRCS "internal/demangle.cc"
  HDRS "internal/demangle.h"
  DEPS
    absl::base
    absl::core_headers
  PUBLIC
)

absl_cc_test(
  NAME demangle_test
  SRCS "internal/demangle_test.cc"
  DEPS
    absl::demangle_internal
    absl::stack_consumption
    absl::base
    absl::core_headers
    absl::memory
    gtest_main
)

# leak_check library
absl_cc_library(
  NAME leak_check
  SRCS "leak_check.cc"
  HDRS "leak_check.h"
  DEPS
    absl::core_headers
  PUBLIC
)

absl_cc_library(
  NAME leak_check_disable
  SRCS "leak_check_disable.cc"
  PUBLIC
)

absl_cc_library(
  NAME leak_check_api_enabled_for_testing
  SRCS "leak_check.cc"
  HDRS "leak_check.h"
  COPTS "-DLEAK_SANITIZER"
  TESTONLY
)

absl_cc_library(
  NAME leak_check_api_disabled_for_testing
  SRCS "leak_check.cc"
  HDRS "leak_check.h"
  COPTS "-ULEAK_SANITIZER"
  TESTONLY
)

# TODO Define ABSL_LSAN_LINKOPTS
#absl_cc_test(
#  NAME leak_check_test
#  SRCS "leak_check_test.cc"
#  COPTS "-DABSL_EXPECT_LEAK_SANITIZER"
#  LINKOPTS ${ABSL_LSAN_LINKOPTS}
#  DEPS
#    absl::leak_check_api_enabled_for_testing
#    absl::base
#    gtest_main
#)

absl_cc_test(
  NAME leak_check_no_lsan_test
  SRCS "leak_check_test.cc"
  COPTS "-UABSL_EXPECT_LEAK_SANITIZER"
  DEPS
    absl::leak_check_api_disabled_for_testing
    absl::base
    gtest_main
)

# TODO Define ABSL_LSAN_LINKOPTS
#absl_cc_test(
#  NAME disabled_leak_check_test
#  SRCS "leak_check_fail_test.cc"
#  LINKOPTS ${ABSL_LSAN_LINKOPTS}
#  DEPS
#    absl::leak_check_api_enabled_for_testing
#    absl::leak_check_disable
#    absl::base
#    gtest_main
#)

# stack_consumption library
absl_cc_library(
  NAME stack_consumption
  SRCS "internal/stack_consumption.cc"
  HDRS "internal/stack_consumption.h"
  DEPS
    absl::base
    absl::core_headers
  PUBLIC
)

absl_cc_test(
  NAME stack_consumption_test
  SRCS "internal/stack_consumption_test.cc"
  DEPS
    absl::stack_consumption
    absl::base
    absl::core_headers
    gtest_main
)
