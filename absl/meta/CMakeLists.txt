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

list(APPEND META_PUBLIC_HEADERS
  "type_traits.h"
)


#
## TESTS
#

# test type_traits_test
list(APPEND TYPE_TRAITS_TEST_SRC
  "type_traits_test.cc"
  ${META_PUBLIC_HEADERS}
)

absl_header_library(
  TARGET
    absl_meta
  PUBLIC_LIBRARIES
    absl::base
  EXPORT_NAME
    meta
 )

absl_test(
  TARGET
    type_traits_test
  SOURCES
    ${TYPE_TRAITS_TEST_SRC}
  PUBLIC_LIBRARIES
    absl::base
    absl::meta
)



