// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/config.h"

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(ConfigTest, Endianness) {
  union
  {
    uint32_t value;
    uint8_t data[sizeof(uint32_t)];
  } number;
  number.data[0] = 0x00;
  number.data[1] = 0x01;
  number.data[2] = 0x02;
  number.data[3] = 0x03;
#if defined(ABSL_IS_LITTLE_ENDIAN) && defined(ABSL_IS_BIG_ENDIAN)
#error Both ABSL_IS_LITTLE_ENDIAN and ABSL_IS_BIG_ENDIAN are defined
#elif defined(ABSL_IS_LITTLE_ENDIAN)
  EXPECT_EQ(UINT32_C(0x03020100), number.value);
#elif defined(ABSL_IS_BIG_ENDIAN)
  EXPECT_EQ(UINT32_C(0x00010203), number.value);
#else
#error Unknown endianness
#endif
}

}  // namespace
