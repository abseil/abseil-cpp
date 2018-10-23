#
# Copyright 2017 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# spinlock wait library
set(SPINLOCK_WAIT_HDRS
  "internal/scheduling_mode.h"
  "internal/spinlock_wait.h"
  "internal/spinlock_akaros.inc"
  "internal/spinlock_linux.inc"
  "internal/spinlock_posix.inc"
  "internal/spinlock_win32.inc"
)

set(SPINLOCK_WAIT_SRCS
  "internal/spinlock_wait.cc"
)

absl_cc_library(
  NAME spinlock_wait
  SRCS ${SPINLOCK_WAIT_SRCS}
  HDRS ${SPINLOCK_WAIT_HDRS}
  DEPS
    absl::core_headers
  PUBLIC
)

# config library
set(CONFIG_PUBLIC_HDRS
  "config.h"
  "policy_checks.h"
)

absl_cc_library(
  NAME config
  HDRS ${CONFIG_HDRS}
  PUBLIC
)

# dynamic_annotations library
absl_cc_library(
  NAME dynamic_annotations
  SRCS "dynamic_annotations.cc"
  HDRS "dynamic_annotations.h"
  PUBLIC
)

# core headers
set(CORE_HEADERS_HDRS
  "attributes.h"
  "macros.h"
  "optimization.h"
  "port.h"
  "thread_annotations.h"
)

absl_cc_library(
  NAME core_headers
  HDRS ${CORE_HEADERS_HDRS}
  DEPS
    absl::config
    absl::dynamic_annotations
  PUBLIC
)

# malloc library
set(MALLOC_INTERNAL_HDRS
  "internal/direct_mmap.h"
  "internal/low_level_alloc.h"
)

set(MALLOC_INTERNAL_SRCS
  "internal/low_level_alloc.cc"
)

absl_cc_library(
  NAME malloc_internal
  SRCS ${MALLOC_INTERNAL_SRCS}
  HDRS ${MALLOC_INTERNAL_HDRS}
  DEPS
    absl::base
    absl::config
    absl::core_headers
    absl::dynamic_annotations
    absl::spinlock_wait
  PUBLIC
)

# base internal library
list(APPEND BASE_INTERNAL_HDRS
  "internal/hide_ptr.h"
  "internal/identity.h"
  "internal/inline_variable.h"
  "internal/invoke.h"
)

absl_cc_library(
  NAME base_internal
  HDRS ${BASE_INTERNAL_HDRS}
  PUBLIC
)

# base library
set(BASE_HDRS
  "call_once.h"
  "casts.h"
  "internal/atomic_hook.h"
  "internal/cycleclock.h"
  "internal/low_level_scheduling.h"
  "internal/per_thread_tls.h"
  "internal/raw_logging.h"
  "internal/spinlock.h"
  "internal/sysinfo.h"
  "internal/thread_identity.h"
  "internal/tsan_mutex_interface.h"
  "internal/unscaledcycleclock.h"
  "log_severity.h"
)

set(BASE_SRCS
  "internal/cycleclock.cc"
  "internal/raw_logging.cc"
  "internal/spinlock.cc"
  "internal/sysinfo.cc"
  "internal/thread_identity.cc"
  "internal/unscaledcycleclock.cc"
)

absl_cc_library(
  NAME base
  SRCS ${BASE_SRCS}
  HDRS ${BASE_HDRS}
  DEPS
    absl::base_internal
    absl::config
    absl::core_headers
    absl::dynamic_annotations
    absl::spinlock_wait
  PUBLIC
)

#
## TESTS
#

# atomic hook test
absl_cc_test(
  NAME atomic_hook_test
  SRCS "internal/atomic_hook_test.cc"
  DEPS
    absl::base
    absl::core_headers
    gtest_main
)

# bit_cast_test
absl_cc_test(
  NAME bit_cast_test
  SRCS "bit_cast_test.cc"
  DEPS
    absl::base
    absl::core_headers
    gtest_main
)

# throw delegate library
absl_cc_library(
  NAME throw_delegate
  SRCS "internal/throw_delegate.cc"
  HDRS "internal/throw_delegate.h"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::base
    absl::config
    absl::core_headers
  PUBLIC
)

# throw_delegate_test
absl_cc_test(
  NAME throw_delegate_test
  SRCS "throw_delegate_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::throw_delegate
    gtest_main
)

# exception_testing library
absl_cc_library(
  NAME exception_testing
  HDRS "internal/exception_testing.h"
  DEPS
    absl::config
    gtest
  TESTONLY
)

# pretty function library
absl_cc_library(
  NAME pretty_function
  HDRS "internal/pretty_function.h"
  PUBLIC
)

# exception_safety_testing library
absl_cc_library(
  NAME exception_safety_testing
  HDRS "internal/exception_safety_testing.h"
  SRCS "internal/exception_safety_testing.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::base
    absl::config
    absl::pretty_function
    absl::memory
    absl::type_traits
    absl::strings
    absl::optional
    gtest
  TESTONLY
)

# exceptions_safety_testing test
absl_cc_test(
  NAME exception_safety_testing_test
  SRCS "exception_safety_testing_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::exception_safety_testing
    absl::memory
    gtest_main
)

# inline_variable test
set(INLINE_VARIABLE_TEST_SRCS
  "inline_variable_test.cc"
  "inline_variable_test_a.cc"
  "inline_variable_test_b.cc"
  "internal/inline_variable_testing.h"
)

absl_cc_test(
  NAME inline_variable_test
  SRCS ${INLINE_VARIABLE_TEST_SRCS}
  DEPS
    absl::base_internal
    gtest_main
)

# invoke test
absl_cc_test(
  NAME invoke_test
  SRCS "invoke_test.cc"
  DEPS
    absl::base_internal
    absl::memory
    absl::strings
    gmock
    gtest_main
)

# spinlock_test_common library
absl_cc_library(
  NAME  spinlock_test_common
  SRCS "spinlock_test_common.cc"
  DEPS
    absl::base
    absl::core_headers
    absl::spinlock_wait
    absl::synchronization
    gtest
  TESTONLY
)

# spinlock test
absl_cc_test(
  NAME spinlock_test
  SRCS "spinlock_test_common.cc"
  DEPS
    absl::base
    absl::core_headers
    absl::spinlock_wait
    absl::synchronization
    gtest_main
)

# endian library
set(ENDIAN_HDRS
  "internal/endian.h"
  "internal/unaligned_access.h"
)

absl_cc_library(
  NAME endian
  HDRS ${ENDIAN_HDRS}
  DEPS
    absl::config
    absl::core_headers
  PUBLIC
)

# endian test
absl_cc_test(
  NAME endian_test
  SRCS "internal/endian_test.cc"
  DEPS
    absl::base
    absl::config
    absl::endian
    gtest_main
)

# config test
absl_cc_test(
  NAME config_test
  SRCS "config_test.cc"
  DEPS
    absl::config
    absl::synchronization
    gtest_main
)

# call_once test
absl_cc_test(
  NAME call_once_test
  SRCS "call_once_test.cc"
  DEPS
    absl::base
    absl::core_headers
    absl::synchronization
    gtest_main
)

# raw_logging test
absl_cc_test(
  NAME raw_logging_test
  SRCS "raw_logging_test.cc"
  DEPS
    absl::base
    absl::strings
    gtest_main
)

# sysinfo test
absl_cc_test(
  NAME sysinfo_test
  SRCS "internal/sysinfo_test.cc"
  DEPS
    absl::base
    absl::synchronization
    gtest_main
)

# low_level_alloc test
absl_cc_test(
  NAME low_level_alloc_test
  SRCS "internal/low_level_alloc_test.cc"
  DEPS
    absl::malloc_internal
    gtest_main
)

# thread_identity test
absl_cc_test(
  NAME thread_identity_test
  SRCS "internal/thread_identity_test.cc"
  DEPS
    absl::base
    absl::core_headers
    absl::synchronization
    gtest_main
)

# bits library
absl_cc_library(
  NAME bits
  HDRS "internal/bits.h"
  DEPS
    absl::core_headers
  PUBLIC
)

# bits test
absl_cc_test(
  NAME bits_test
  SRCS "internal/bits_test.cc"
  DEPS
    absl::bits
    gtest_main
)
