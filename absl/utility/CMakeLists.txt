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

absl_cc_library(
  NAME utility
  HDRS "utility.h"
  DEPS
    absl::base_internal
    absl::config
    absl::type_traits
  PUBLIC
)

# utility test
absl_cc_test(
  NAME utility_test
  SRCS "utility_test.cc"
  DEPS
    absl::utility
    absl::core_headers
    absl::memory
    absl::strings
    gmock
    gtest_main
)
