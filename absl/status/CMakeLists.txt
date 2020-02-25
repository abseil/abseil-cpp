#
# Copyright 2020 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
absl_cc_library(
  NAME
    status
  HDRS
    "status.h"
  SRCS
    "status.cc"
    "status_payload_printer.h"
    "status_payload_printer.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::config
    absl::core_headers
    absl::raw_logging_internal
    absl::inlined_vector
    absl::stacktrace
    absl::symbolize
    absl::strings
    absl::cord
    absl::str_format
    absl::optional
  PUBLIC
)

absl_cc_test(
  NAME
    status_test
  SRCS
   "status_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::status
    absl::strings
    gmock_main
)
