#
# Copyright 2018 The Abseil Authors.
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

absl_cc_library(
  NAME hash
  SRCS
    "internal/hash.h"
    "internal/hash.cc"
  HDRS "hash.h"
  DEPS
    absl::city
    absl::core_headers
    absl::endian
    absl::fixed_array
    absl::type_traits
    absl::int128
    absl::strings
    absl::optional
    absl::variant
    absl::utility
  PUBLIC
)

absl_cc_library(
  NAME hash_testing
  HDRS "hash_testing.h"
  DEPS
    absl::spy_hash_state
    absl::type_traits
    absl::strings
    absl::variant
    gtest
  TESTONLY
)

absl_cc_test(
  NAME hash_test
  SRCS "hash_test.cc"
  DEPS
    absl::hash
    absl::hash_testing
    absl::core_headers
    absl::flat_hash_set
    absl::spy_hash_state
    absl::type_traits
    absl::int128
    gmock
    gtest_main
)

absl_cc_library(
  NAME spy_hash_state
  HDRS "internal/spy_hash_state.h"
  DEPS
    absl::hash
    absl::strings
    absl::str_format
  TESTONLY
)

absl_cc_library(
  NAME city
  SRCS "internal/city.cc"
  HDRS "internal/city.h"
  DEPS
    absl::config
    absl::core_headers
    absl::endian
  PUBLIC
)

absl_cc_test(
  NAME city_test
  SRCS "internal/city_test.cc"
  DEPS
    absl::city
    gtest_main
)
