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

set(STRINGS_HDRS
  "ascii.h"
  "charconv.h"
  "escaping.h"
  "match.h"
  "numbers.h"
  "str_cat.h"
  "str_join.h"
  "str_replace.h"
  "str_split.h"
  "string_view.h"
  "strip.h"
  "substitute.h"
)

set(STRINGS_SRCS
  "ascii.cc"
  "charconv.cc"
  "escaping.cc"
  "internal/charconv_bigint.cc"
  "internal/charconv_bigint.h"
  "internal/charconv_parse.cc"
  "internal/charconv_parse.h"
  "internal/memutil.cc"
  "internal/memutil.h"
  "internal/stl_type_traits.h"
  "internal/str_join_internal.h"
  "internal/str_split_internal.h"
  "match.cc"
  "numbers.cc"
  "str_cat.cc"
  "str_replace.cc"
  "str_split.cc"
  "string_view.cc"
  "substitute.cc"
)

absl_cc_library(
  NAME strings
  SRCS ${STRINGS_SRCS}
  HDRS ${STRINGS_HDRS}
  DEPS
    absl::strings_internal
    absl::base
    absl::bits
    absl::config
    absl::core_headers
    absl::endian
    absl::throw_delegate
    absl::memory
    absl::type_traits
    absl::int128
  PUBLIC
)

# strings_internal library
set(STRINGS_INTERNAL_HDRS
  "internal/char_map.h"
  "internal/ostringstream.h"
  "internal/resize_uninitialized.h"
  "internal/utf8.h"
)

set(STRINGS_INTERNAL_SRCS
  "internal/ostringstream.cc"
  "internal/utf8.cc"
)

absl_cc_library(
  NAME strings_internal
  SRCS ${STRINGS_INTERNAL_SRCS}
  HDRS ${STRINGS_INTERNAL_HDRS}
  DEPS
    absl::core_headers
    absl::endian
    absl::type_traits
  PUBLIC
)

# match test
absl_cc_test(
  NAME match_test
  SRCS "match_test.cc"
  DEPS
    absl::strings
    gtest_main
)

# escaping test
absl_cc_test(
  NAME escaping_test
  SRCS
    "escaping_test.cc"
    "internal/escaping_test_common.h"
  DEPS
    absl::strings
    absl::core_headers
    absl::fixed_array
    gmock
    gtest_main
)

# ascii test
absl_cc_test(
  NAME ascii_test
  SRCS "ascii_test.cc"
  DEPS
    absl::strings
    absl::core_headers
    gtest_main
)

# memutil test
absl_cc_test(
  NAME memutil_test
  SRCS
    "internal/memutil.h"
    "internal/memutil_test.cc"
  DEPS
    absl::strings
    absl::core_headers
    gtest_main
)

# utf8 test
absl_cc_test(
  NAME utf8_test
  SRCS "internal/utf8_test.cc"
  DEPS
    absl::strings_internal
    absl::core_headers
    gtest_main
)

absl_cc_test(
  NAME string_view_test
  SRCS "string_view_test.cc"
  COPTS ${ABSL_EXCEPTIONS_FLAG}
  LINKOPTS ${ABSL_EXCEPTIONS_FLAG_LINKOPTS}
  DEPS
    absl::strings
    absl::config
    absl::core_headers
    absl::dynamic_annotations
    gtest_main
)

absl_cc_test(
  NAME substitute_test
  SRCS "substitute_test.cc"
  DEPS
    absl::strings
    absl::core_headers
    gtest_main
  )

absl_cc_test(
  NAME str_replace_test
  SRCS "str_replace_test.cc"
  DEPS
    absl::strings
    gtest_main
)

absl_cc_test(
  NAME str_split_test
  SRCS "str_split_test.cc"
  DEPS
    absl::strings
    absl::core_headers
    absl::dynamic_annotations
    gmock
    gtest_main
)

absl_cc_test(
  NAME ostringstream_test
  SRCS "internal/ostringstream_test.cc"
  DEPS
    absl::strings_internal
    gtest_main
)

absl_cc_test(
  NAME resize_uninitialized_test
  SRCS
    "internal/resize_uninitialized.h"
    "internal/resize_uninitialized_test.cc"
  DEPS
    absl::core_headers
    absl::type_traits
    gtest_main
)

absl_cc_test(
  NAME str_join_test
  SRCS "str_join_test.cc"
  DEPS
    absl::strings
    absl::core_headers
    absl::memory
    gtest_main
)

absl_cc_test(
  NAME str_cat_test
  SRCS "str_cat_test.cc"
  DEPS
    absl::strings
    absl::core_headers
    gtest_main
)

absl_cc_test(
  NAME numbers_test
  SRCS
    "internal/numbers_test_common.h"
    "numbers_test.cc"
  DEPS
    absl::strings
    absl::base
    absl::core_headers
    gmock
    gtest_main
)

absl_cc_test(
  NAME strip_test
  SRCS "strip_test.cc"
  DEPS
    absl::strings
    gmock
    gtest_main
)

absl_cc_test(
  NAME char_map_test
  SRCS "internal/char_map_test.cc"
  DEPS
    absl::strings_internal
    gmock
    gtest_main
)

absl_cc_test(
  NAME charconv_test
  SRCS "charconv_test.cc"
  DEPS
    absl::strings
    absl::base
    gmock
    gtest_main
)

absl_cc_test(
  NAME charconv_parse_test
  SRCS
    "internal/charconv_parse.h"
    "internal/charconv_parse_test.cc"
  DEPS
    absl::strings
    absl::base
    gmock
    gtest_main
)

absl_cc_test(
  NAME charconv_bigint_test
  SRCS
    "internal/charconv_bigint.h"
    "internal/charconv_bigint_test.cc"
    "internal/charconv_parse.h"
  DEPS
    absl::strings
    absl::base
    gtest_main
)

absl_cc_library(
  NAME str_format
  HDRS "str_format.h"
  DEPS
    absl::str_format_internal
  PUBLIC
)

absl_cc_library(
  NAME str_format_internal
  SRCS
    "internal/str_format/arg.cc"
    "internal/str_format/bind.cc"
    "internal/str_format/extension.cc"
    "internal/str_format/float_conversion.cc"
    "internal/str_format/output.cc"
    "internal/str_format/parser.cc"
  HDRS
    "internal/str_format/arg.h"
    "internal/str_format/bind.h"
    "internal/str_format/checker.h"
    "internal/str_format/extension.h"
    "internal/str_format/float_conversion.h"
    "internal/str_format/output.h"
    "internal/str_format/parser.h"
  DEPS
    absl::strings
    absl::core_headers
    absl::inlined_vector
    absl::type_traits
    absl::int128
    absl::span
  PUBLIC
)

absl_cc_test(
  NAME str_format_test
  SRCS "str_format_test.cc"
  DEPS
    absl::str_format
    absl::strings
    absl::core_headers
    gmock
    gtest_main
)

absl_cc_test(
  NAME str_format_extension_test
  SRCS "internal/str_format/extension_test.cc"
  DEPS
    absl::str_format
    absl::str_format_internal
    gtest_main
)

absl_cc_test(
  NAME str_format_arg_test
  SRCS "internal/str_format/arg_test.cc"
  DEPS
    absl::str_format
    absl::str_format_internal
    gtest_main
)

absl_cc_test(
  NAME str_format_bind_test
  SRCS "internal/str_format/bind_test.cc"
  DEPS
    absl::str_format_internal
    gtest_main
)

absl_cc_test(
  NAME str_format_checker_test
  SRCS "internal/str_format/checker_test.cc"
  DEPS
    absl::str_format
    gmock
    gtest_main
)

absl_cc_test(
  NAME str_format_convert_test
  SRCS "internal/str_format/convert_test.cc"
  DEPS
    absl::str_format_internal
    absl::int128
    gtest_main
)

absl_cc_test(
  NAME str_format_output_test
  SRCS "internal/str_format/output_test.cc"
  DEPS
    absl::str_format_internal
    gmock
    gtest_main
)

absl_cc_test(
  NAME str_format_parser_test
  SRCS "internal/str_format/parser_test.cc"
  DEPS
    absl::str_format_internal
    absl::core_headers
    gtest_main
)
