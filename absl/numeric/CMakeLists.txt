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

set(INT128_SRCS
  "int128.cc"
  "int128_have_intrinsic.inc"
  "int128_no_intrinsic.inc"
)

absl_cc_library(
  NAME int128
  SRCS ${INT128_SRCS}
  HDRS "int128.h"
  DEPS
    absl::config
    absl::core_headers
  PUBLIC
)

absl_cc_test(
  NAME int128_test
  SRCS
    "int128_stream_test.cc"
    "int128_test.cc"
  DEPS
    absl::int128
    absl::base
    absl::core_headers
    absl::hash_testing
    absl::type_traits
    gmock
    gtest_main
)

