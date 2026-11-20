//
//  Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/flags/marshalling.h"

#include <stddef.h>
#include <stdint.h>

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/numeric/int128.h"

namespace {

TEST(MarshallingTest, TestBoolParsing) {
  std::string err;
  bool value;

  // True values.
  EXPECT_TRUE(absl::ParseFlag("True", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("true", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("TRUE", &value, &err));
  EXPECT_TRUE(value);

  EXPECT_TRUE(absl::ParseFlag("Yes", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("yes", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("YES", &value, &err));
  EXPECT_TRUE(value);

  EXPECT_TRUE(absl::ParseFlag("t", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("T", &value, &err));
  EXPECT_TRUE(value);

  EXPECT_TRUE(absl::ParseFlag("y", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("Y", &value, &err));
  EXPECT_TRUE(value);

  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_TRUE(value);

  // False values.
  EXPECT_TRUE(absl::ParseFlag("False", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("false", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("FALSE", &value, &err));
  EXPECT_FALSE(value);

  EXPECT_TRUE(absl::ParseFlag("No", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("no", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("NO", &value, &err));
  EXPECT_FALSE(value);

  EXPECT_TRUE(absl::ParseFlag("f", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("F", &value, &err));
  EXPECT_FALSE(value);

  EXPECT_TRUE(absl::ParseFlag("n", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("N", &value, &err));
  EXPECT_FALSE(value);

  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_FALSE(value);

  // Whitespace handling.
  EXPECT_TRUE(absl::ParseFlag("  true", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("true  ", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("  true   ", &value, &err));
  EXPECT_TRUE(value);

  // Invalid input.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("11", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("tt", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt16Parsing) {
  std::string err;
  int16_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("-1", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("-18765", &value, &err));
  EXPECT_EQ(value, -18765);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("-001", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0X234", &value, &err));
  EXPECT_EQ(value, 564);
  EXPECT_TRUE(absl::ParseFlag("-0x7FFD", &value, &err));
  EXPECT_EQ(value, -32765);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x22    ", &value, &err));
  EXPECT_EQ(value, 34);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("40000", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint16Parsing) {
  std::string err;
  uint16_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0X234", &value, &err));
  EXPECT_EQ(value, 564);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x22    ", &value, &err));
  EXPECT_EQ(value, 34);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("70000", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt32Parsing) {
  std::string err;
  int32_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("-1", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("-98765", &value, &err));
  EXPECT_EQ(value, -98765);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("-001", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0X234", &value, &err));
  EXPECT_EQ(value, 564);

  EXPECT_TRUE(absl::ParseFlag("-0x7FFFFFFD", &value, &err));
  EXPECT_EQ(value, -2147483645);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x22    ", &value, &err));
  EXPECT_EQ(value, 34);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("70000000000", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint32Parsing) {
  std::string err;
  uint32_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0X234", &value, &err));
  EXPECT_EQ(value, 564);
  EXPECT_TRUE(absl::ParseFlag("0xFFFFFFFD", &value, &err));
  EXPECT_EQ(value, 4294967293);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x22    ", &value, &err));
  EXPECT_EQ(value, 34);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("140000000000", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt64Parsing) {
  std::string err;
  int64_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("-1", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("-98765", &value, &err));
  EXPECT_EQ(value, -98765);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0XFFFAAABBBCCCDDD", &value, &err));
  EXPECT_EQ(value, 1152827684197027293);
  EXPECT_TRUE(absl::ParseFlag("-0x7FFFFFFFFFFFFFFE", &value, &err));
  EXPECT_EQ(value, -9223372036854775806);
  EXPECT_TRUE(absl::ParseFlag("-0x02", &value, &err));
  EXPECT_EQ(value, -2);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x7F    ", &value, &err));
  EXPECT_EQ(value, 127);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("0xFFFFFFFFFFFFFFFFFF", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUInt64Parsing) {
  std::string err;
  uint64_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("+13", &value, &err));
  EXPECT_EQ(value, 13);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000300", &value, &err));
  EXPECT_EQ(value, 300);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0XFFFF", &value, &err));
  EXPECT_EQ(value, 65535);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("0xFFFFFFFFFFFFFFFFFF", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt128Parsing) {
  std::string err;
  absl::int128 value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("-1", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("-98765", &value, &err));
  EXPECT_EQ(value, -98765);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0xFFFAAABBBCCCDDD", &value, &err));
  EXPECT_EQ(value, 1152827684197027293);
  EXPECT_TRUE(absl::ParseFlag("0xFFF0FFFFFFFFFFFFFFF", &value, &err));
  EXPECT_EQ(value, absl::MakeInt128(0x000000000000fff, 0xFFFFFFFFFFFFFFF));

  EXPECT_TRUE(absl::ParseFlag("-0x10000000000000000", &value, &err));
  EXPECT_EQ(value, absl::MakeInt128(-1, 0));
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("16  ", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("  16", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("  0100  ", &value, &err));
  EXPECT_EQ(value, 100);
  EXPECT_TRUE(absl::ParseFlag(" 0x7B    ", &value, &err));
  EXPECT_EQ(value, 123);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint128Parsing) {
  std::string err;
  absl::uint128 value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0xFFFAAABBBCCCDDD", &value, &err));
  EXPECT_EQ(value, 1152827684197027293);
  EXPECT_TRUE(absl::ParseFlag("0xFFF0FFFFFFFFFFFFFFF", &value, &err));
  EXPECT_EQ(value, absl::MakeInt128(0x000000000000fff, 0xFFFFFFFFFFFFFFF));
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("16  ", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("  16", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("  0100  ", &value, &err));
  EXPECT_EQ(value, 100);
  EXPECT_TRUE(absl::ParseFlag(" 0x7B    ", &value, &err));
  EXPECT_EQ(value, 123);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-0x10000000000000000", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestFloatParsing) {
  std::string err;
  float value;

  // Ordinary values.
  EXPECT_TRUE(absl::ParseFlag("1.3", &value, &err));
  EXPECT_FLOAT_EQ(value, 1.3f);
  EXPECT_TRUE(absl::ParseFlag("-0.1", &value, &err));
  EXPECT_DOUBLE_EQ(value, -0.1f);
  EXPECT_TRUE(absl::ParseFlag("+0.01", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.01f);

  // Scientific values.
  EXPECT_TRUE(absl::ParseFlag("1.2e3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.2e3f);
  EXPECT_TRUE(absl::ParseFlag("9.8765402e-37", &value, &err));
  EXPECT_DOUBLE_EQ(value, 9.8765402e-37f);
  EXPECT_TRUE(absl::ParseFlag("0.11e+3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.11e+3f);
  EXPECT_TRUE(absl::ParseFlag("1.e-2300", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.f);
  EXPECT_TRUE(absl::ParseFlag("1.e+2300", &value, &err));
  EXPECT_TRUE(std::isinf(value));

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01.6", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.6f);
  EXPECT_TRUE(absl::ParseFlag("000.0001", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.0001f);

  // Trailing zero values.
  EXPECT_TRUE(absl::ParseFlag("-5.1000", &value, &err));
  EXPECT_DOUBLE_EQ(value, -5.1f);

  // Exceptional values.
  EXPECT_TRUE(absl::ParseFlag("NaN", &value, &err));
  EXPECT_TRUE(std::isnan(value));
  EXPECT_TRUE(absl::ParseFlag("Inf", &value, &err));
  EXPECT_TRUE(std::isinf(value));

  // Hex values
  EXPECT_TRUE(absl::ParseFlag("0x10.23p12", &value, &err));
  EXPECT_DOUBLE_EQ(value, 66096.f);
  EXPECT_TRUE(absl::ParseFlag("-0xF1.A3p-2", &value, &err));
  EXPECT_NEAR(value, -60.4092f, 5e-5f);
  EXPECT_TRUE(absl::ParseFlag("+0x0.0AAp-12", &value, &err));
  EXPECT_NEAR(value, 1.01328e-05f, 5e-11f);
  EXPECT_TRUE(absl::ParseFlag("0x.01p1", &value, &err));
  EXPECT_NEAR(value, 0.0078125f, 5e-8f);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10.1  ", &value, &err));
  EXPECT_DOUBLE_EQ(value, 10.1f);
  EXPECT_TRUE(absl::ParseFlag("  2.34", &value, &err));
  EXPECT_DOUBLE_EQ(value, 2.34f);
  EXPECT_TRUE(absl::ParseFlag("  5.7  ", &value, &err));
  EXPECT_DOUBLE_EQ(value, 5.7f);
  EXPECT_TRUE(absl::ParseFlag("  -0xE0.F3p01  ", &value, &err));
  EXPECT_NEAR(value, -449.8984375f, 5e-8f);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2.3xxx", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("0x0.1pAA", &value, &err));
  // TODO(rogeeff): below assertion should fail
  EXPECT_TRUE(absl::ParseFlag("0x0.1", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestDoubleParsing) {
  std::string err;
  double value;

  // Ordinary values.
  EXPECT_TRUE(absl::ParseFlag("1.3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.3);
  EXPECT_TRUE(absl::ParseFlag("-0.1", &value, &err));
  EXPECT_DOUBLE_EQ(value, -0.1);
  EXPECT_TRUE(absl::ParseFlag("+0.01", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.01);

  // Scientific values.
  EXPECT_TRUE(absl::ParseFlag("1.2e3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.2e3);
  EXPECT_TRUE(absl::ParseFlag("9.00000002e-123", &value, &err));
  EXPECT_DOUBLE_EQ(value, 9.00000002e-123);
  EXPECT_TRUE(absl::ParseFlag("0.11e+3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.11e+3);
  EXPECT_TRUE(absl::ParseFlag("1.e-2300", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("1.e+2300", &value, &err));
  EXPECT_TRUE(std::isinf(value));

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01.6", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.6);
  EXPECT_TRUE(absl::ParseFlag("000.0001", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.0001);

  // Trailing zero values.
  EXPECT_TRUE(absl::ParseFlag("-5.1000", &value, &err));
  EXPECT_DOUBLE_EQ(value, -5.1);

  // Exceptional values.
  EXPECT_TRUE(absl::ParseFlag("NaN", &value, &err));
  EXPECT_TRUE(std::isnan(value));
  EXPECT_TRUE(absl::ParseFlag("nan", &value, &err));
  EXPECT_TRUE(std::isnan(value));
  EXPECT_TRUE(absl::ParseFlag("Inf", &value, &err));
  EXPECT_TRUE(std::isinf(value));
  EXPECT_TRUE(absl::ParseFlag("inf", &value, &err));
  EXPECT_TRUE(std::isinf(value));

  // Hex values
  EXPECT_TRUE(absl::ParseFlag("0x10.23p12", &value, &err));
  EXPECT_DOUBLE_EQ(value, 66096);
  EXPECT_TRUE(absl::ParseFlag("-0xF1.A3p-2", &value, &err));
  EXPECT_NEAR(value, -60.4092, 5e-5);
  EXPECT_TRUE(absl::ParseFlag("+0x0.0AAp-12", &value, &err));
  EXPECT_NEAR(value, 1.01328e-05, 5e-11);
  EXPECT_TRUE(absl::ParseFlag("0x.01p1", &value, &err));
  EXPECT_NEAR(value, 0.0078125, 5e-8);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10.1  ", &value, &err));
  EXPECT_DOUBLE_EQ(value, 10.1);
  EXPECT_TRUE(absl::ParseFlag("  2.34", &value, &err));
  EXPECT_DOUBLE_EQ(value, 2.34);
  EXPECT_TRUE(absl::ParseFlag("  5.7  ", &value, &err));
  EXPECT_DOUBLE_EQ(value, 5.7);
  EXPECT_TRUE(absl::ParseFlag("  -0xE0.F3p01  ", &value, &err));
  EXPECT_NEAR(value, -449.8984375, 5e-8);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2.3xxx", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("0x0.1pAA", &value, &err));
  // TODO(rogeeff): below assertion should fail
  EXPECT_TRUE(absl::ParseFlag("0x0.1", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestStringParsing) {
  std::string err;
  std::string value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_EQ(value, "");
  EXPECT_TRUE(absl::ParseFlag(" ", &value, &err));
  EXPECT_EQ(value, " ");
  EXPECT_TRUE(absl::ParseFlag("   ", &value, &err));
  EXPECT_EQ(value, "   ");
  EXPECT_TRUE(absl::ParseFlag("\n", &value, &err));
  EXPECT_EQ(value, "\n");
  EXPECT_TRUE(absl::ParseFlag("\t", &value, &err));
  EXPECT_EQ(value, "\t");
  EXPECT_TRUE(absl::ParseFlag("asdfg", &value, &err));
  EXPECT_EQ(value, "asdfg");
  EXPECT_TRUE(absl::ParseFlag("asdf ghjk", &value, &err));
  EXPECT_EQ(value, "asdf ghjk");
  EXPECT_TRUE(absl::ParseFlag("a\nb\nc", &value, &err));
  EXPECT_EQ(value, "a\nb\nc");
  EXPECT_TRUE(absl::ParseFlag("asd\0fgh", &value, &err));
  EXPECT_EQ(value, "asd");
  EXPECT_TRUE(absl::ParseFlag("\\\\", &value, &err));
  EXPECT_EQ(value, "\\\\");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestVectorOfStringParsing) {
  std::string err;
  std::vector<std::string> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>{});
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"1"}));
  EXPECT_TRUE(absl::ParseFlag("a,b", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"a", "b"}));
  EXPECT_TRUE(absl::ParseFlag("a,b,c,", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"a", "b", "c", ""}));
  EXPECT_TRUE(absl::ParseFlag("a,,", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"a", "", ""}));
  EXPECT_TRUE(absl::ParseFlag(",", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"", ""}));
  EXPECT_TRUE(absl::ParseFlag("a, b,c ", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"a", " b", "c "}));
}

// --------------------------------------------------------------------

template <typename IntType>
void TestIntegralVectorParsingCommon() {
  std::string err;
  std::vector<IntType> value = {IntType{99}, IntType{100}};

  // Empty string clears pre-populated vector.
  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_EQ(value, std::vector<IntType>{});

  // Single and multiple values overwrite existing contents.
  value = {IntType{99}};
  EXPECT_TRUE(absl::ParseFlag("42", &value, &err));
  EXPECT_EQ(value, std::vector<IntType>({IntType{42}}));

  EXPECT_TRUE(absl::ParseFlag("1,0,+3,123", &value, &err));
  EXPECT_EQ(value, (std::vector<IntType>{IntType{1}, IntType{0}, IntType{3},
                                         IntType{123}}));

  // Leading zeros are decimal (base 10), not octal; 0x/0X is hexadecimal.
  EXPECT_TRUE(absl::ParseFlag("010,000100,0x10,0X20,+0x31", &value, &err));
  EXPECT_EQ(value, (std::vector<IntType>{IntType{10}, IntType{100}, IntType{16},
                                         IntType{32}, IntType{49}}));

  // Surrounding ASCII whitespace around elements is stripped.
  EXPECT_TRUE(absl::ParseFlag(" 10 ,  20,30  ,\t0x22\n", &value, &err));
  EXPECT_EQ(value, (std::vector<IntType>{IntType{10}, IntType{20}, IntType{30},
                                         IntType{34}}));

  // Whitespace-only non-empty strings, empty tokens, internal whitespace, and
  // malformed tokens are rejected.
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("   ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\r", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(",", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(",,", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1,,2", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1, ,2", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(",1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1,2,", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1 2,3", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1,typo,3", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1,2U,3", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1,FFF,3", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1,--2,3", &value, &err));
}

template <typename SignedIntType>
void TestSignedIntegralVectorParsing() {
  TestIntegralVectorParsingCommon<SignedIntType>();

  std::string err;
  std::vector<SignedIntType> value;
  EXPECT_TRUE(absl::ParseFlag("-1,-010,-0x10", &value, &err));
  EXPECT_EQ(value, (std::vector<SignedIntType>{
                       SignedIntType{-1}, SignedIntType{-10},
                       SignedIntType{-16}}));
}

template <typename UnsignedIntType>
void TestUnsignedIntegralVectorParsing() {
  TestIntegralVectorParsingCommon<UnsignedIntType>();

  std::string err;
  std::vector<UnsignedIntType> value;
  EXPECT_FALSE(absl::ParseFlag("-1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1,-2,3", &value, &err));
}

TEST(MarshallingTest, TestVectorOfIntegralTypesParsing) {
  // Fundamental integral types.
  TestSignedIntegralVectorParsing<short>();                 // NOLINT
  TestUnsignedIntegralVectorParsing<unsigned short>();      // NOLINT
  TestSignedIntegralVectorParsing<int>();
  TestUnsignedIntegralVectorParsing<unsigned int>();
  TestSignedIntegralVectorParsing<long>();                  // NOLINT
  TestUnsignedIntegralVectorParsing<unsigned long>();       // NOLINT
  TestSignedIntegralVectorParsing<long long>();             // NOLINT
  TestUnsignedIntegralVectorParsing<unsigned long long>();  // NOLINT
  TestSignedIntegralVectorParsing<absl::int128>();
  TestUnsignedIntegralVectorParsing<absl::uint128>();

  // Fixed-width and platform typedefs.
  TestSignedIntegralVectorParsing<int16_t>();
  TestUnsignedIntegralVectorParsing<uint16_t>();
  TestSignedIntegralVectorParsing<int32_t>();
  TestUnsignedIntegralVectorParsing<uint32_t>();
  TestSignedIntegralVectorParsing<int64_t>();
  TestUnsignedIntegralVectorParsing<uint64_t>();
  TestUnsignedIntegralVectorParsing<size_t>();

  // Range bounds checks.
  std::string err;
  std::vector<int16_t> i16;
  EXPECT_TRUE(absl::ParseFlag("-32768,32767", &i16, &err));
  EXPECT_EQ(i16, (std::vector<int16_t>{-32768, 32767}));
  EXPECT_FALSE(absl::ParseFlag("1,40000", &i16, &err));
  EXPECT_FALSE(absl::ParseFlag("-40000,1", &i16, &err));

  std::vector<uint16_t> u16;
  EXPECT_TRUE(absl::ParseFlag("0,65535", &u16, &err));
  EXPECT_EQ(u16, (std::vector<uint16_t>{0, 65535}));
  EXPECT_FALSE(absl::ParseFlag("1,70000", &u16, &err));

  std::vector<int64_t> i64;
  EXPECT_FALSE(absl::ParseFlag("0xFFFFFFFFFFFFFFFFFF", &i64, &err));

  std::vector<uint64_t> u64;
  EXPECT_FALSE(absl::ParseFlag("0xFFFFFFFFFFFFFFFFFF", &u64, &err));

  std::vector<absl::int128> i128;
  EXPECT_TRUE(absl::ParseFlag(
      "-0x10000000000000000,0xFFF0FFFFFFFFFFFFFFF", &i128, &err));
  EXPECT_EQ(
      i128,
      (std::vector<absl::int128>{
          absl::MakeInt128(-1, 0),
          absl::MakeInt128(0x000000000000fff, 0xFFFFFFFFFFFFFFF)}));

  std::vector<absl::uint128> u128;
  EXPECT_TRUE(absl::ParseFlag("0,0xFFF0FFFFFFFFFFFFFFF", &u128, &err));
  EXPECT_EQ(
      u128,
      (std::vector<absl::uint128>{
          0, absl::MakeUint128(0x000000000000fff, 0xFFFFFFFFFFFFFFF)}));
}

// --------------------------------------------------------------------

template <typename FloatType>
void TestFloatingPointVectorParsing() {
  std::string err;
  std::vector<FloatType> value = {FloatType{99}};

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_EQ(value, std::vector<FloatType>{});

  value = {FloatType{99}};
  EXPECT_TRUE(absl::ParseFlag("0.5, -1.25e-2, +0.25, 0x10p1", &value, &err));
  ASSERT_EQ(value.size(), 4u);
  EXPECT_EQ(value[0], static_cast<FloatType>(0.5));
  EXPECT_EQ(value[1], static_cast<FloatType>(-0.0125));
  EXPECT_EQ(value[2], static_cast<FloatType>(0.25));
  EXPECT_EQ(value[3], static_cast<FloatType>(32.0));

  EXPECT_TRUE(absl::ParseFlag("inf, -inf, NaN", &value, &err));
  ASSERT_EQ(value.size(), 3u);
  EXPECT_TRUE(std::isinf(value[0]) && value[0] > 0);
  EXPECT_TRUE(std::isinf(value[1]) && value[1] < 0);
  EXPECT_TRUE(std::isnan(value[2]));

  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("   ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(",", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1.0,,2.0", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1.0, ,2.0", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(",1.0", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1.0,2.0,", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1.0 2.0", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("1.0,bad,2.0", &value, &err));
}

TEST(MarshallingTest, TestVectorOfFloatingPointTypesParsing) {
  TestFloatingPointVectorParsing<float>();
  TestFloatingPointVectorParsing<double>();
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalBoolParsing) {
  std::string err;
  std::optional<bool> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(value.has_value());

  EXPECT_TRUE(absl::ParseFlag("true", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_TRUE(*value);

  EXPECT_TRUE(absl::ParseFlag("false", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_FALSE(*value);

  EXPECT_FALSE(absl::ParseFlag("nullopt", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalIntParsing) {
  std::string err;
  std::optional<int> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(value.has_value());

  EXPECT_TRUE(absl::ParseFlag("10", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 10);

  EXPECT_TRUE(absl::ParseFlag("0x1F", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 31);

  EXPECT_FALSE(absl::ParseFlag("nullopt", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalDoubleParsing) {
  std::string err;
  std::optional<double> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(value.has_value());

  EXPECT_TRUE(absl::ParseFlag("1.11", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 1.11);

  EXPECT_TRUE(absl::ParseFlag("-0.12", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, -0.12);

  EXPECT_FALSE(absl::ParseFlag("nullopt", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalStringParsing) {
  std::string err;
  std::optional<std::string> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(value.has_value());

  EXPECT_TRUE(absl::ParseFlag(" ", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, " ");

  EXPECT_TRUE(absl::ParseFlag("aqswde", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, "aqswde");

  EXPECT_TRUE(absl::ParseFlag("nullopt", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, "nullopt");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestBoolUnparsing) {
  EXPECT_EQ(absl::UnparseFlag(true), "true");
  EXPECT_EQ(absl::UnparseFlag(false), "false");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt16Unparsing) {
  int16_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -1;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
  value = 9876;
  EXPECT_EQ(absl::UnparseFlag(value), "9876");
  value = -987;
  EXPECT_EQ(absl::UnparseFlag(value), "-987");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint16Unparsing) {
  uint16_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = 19876;
  EXPECT_EQ(absl::UnparseFlag(value), "19876");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt32Unparsing) {
  int32_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -1;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
  value = 12345;
  EXPECT_EQ(absl::UnparseFlag(value), "12345");
  value = -987;
  EXPECT_EQ(absl::UnparseFlag(value), "-987");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint32Unparsing) {
  uint32_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = 1234500;
  EXPECT_EQ(absl::UnparseFlag(value), "1234500");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt64Unparsing) {
  int64_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -1;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
  value = 123456789L;
  EXPECT_EQ(absl::UnparseFlag(value), "123456789");
  value = -987654321L;
  EXPECT_EQ(absl::UnparseFlag(value), "-987654321");
  value = 0x7FFFFFFFFFFFFFFF;
  EXPECT_EQ(absl::UnparseFlag(value), "9223372036854775807");
  value = 0xFFFFFFFFFFFFFFFF;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint64Unparsing) {
  uint64_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = 123456789L;
  EXPECT_EQ(absl::UnparseFlag(value), "123456789");
  value = 0xFFFFFFFFFFFFFFFF;
  EXPECT_EQ(absl::UnparseFlag(value), "18446744073709551615");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt128Unparsing) {
  absl::int128 value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -1;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
  value = 123456789L;
  EXPECT_EQ(absl::UnparseFlag(value), "123456789");
  value = -987654321L;
  EXPECT_EQ(absl::UnparseFlag(value), "-987654321");
  value = 0x7FFFFFFFFFFFFFFF;
  EXPECT_EQ(absl::UnparseFlag(value), "9223372036854775807");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint128Unparsing) {
  absl::uint128 value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = 123456789L;
  EXPECT_EQ(absl::UnparseFlag(value), "123456789");
  value = absl::MakeUint128(0, 0xFFFFFFFFFFFFFFFF);
  EXPECT_EQ(absl::UnparseFlag(value), "18446744073709551615");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestFloatUnparsing) {
  float value;

  value = 1.1f;
  EXPECT_EQ(absl::UnparseFlag(value), "1.1");
  value = 0.01f;
  EXPECT_EQ(absl::UnparseFlag(value), "0.01");
  value = 1.23e-2f;
  EXPECT_EQ(absl::UnparseFlag(value), "0.0123");
  value = -0.71f;
  EXPECT_EQ(absl::UnparseFlag(value), "-0.71");
  value = 0.0f;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -0.0f;
  EXPECT_EQ(absl::UnparseFlag(value), "-0");
  value = std::nanf("");
  EXPECT_EQ(absl::UnparseFlag(value), "nan");
  value = -std::nanf("");
  EXPECT_EQ(absl::UnparseFlag(value), "nan");
  value = std::numeric_limits<float>::infinity();
  EXPECT_EQ(absl::UnparseFlag(value), "inf");
  value = -std::numeric_limits<float>::infinity();
  EXPECT_EQ(absl::UnparseFlag(value), "-inf");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestDoubleUnparsing) {
  double value;

  value = 1.1;
  EXPECT_EQ(absl::UnparseFlag(value), "1.1");
  value = 0.01;
  EXPECT_EQ(absl::UnparseFlag(value), "0.01");
  value = 1.23e-2;
  EXPECT_EQ(absl::UnparseFlag(value), "0.0123");
  value = -0.71;
  EXPECT_EQ(absl::UnparseFlag(value), "-0.71");
  value = -0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -0.0;
  EXPECT_EQ(absl::UnparseFlag(value), "-0");
  value = std::nan("");
  EXPECT_EQ(absl::UnparseFlag(value), "nan");
  value = -std::nan("");
  EXPECT_EQ(absl::UnparseFlag(value), "nan");
  value = std::numeric_limits<double>::infinity();
  EXPECT_EQ(absl::UnparseFlag(value), "inf");
  value = -std::numeric_limits<double>::infinity();
  EXPECT_EQ(absl::UnparseFlag(value), "-inf");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestStringUnparsing) {
  EXPECT_EQ(absl::UnparseFlag(""), "");
  EXPECT_EQ(absl::UnparseFlag(" "), " ");
  EXPECT_EQ(absl::UnparseFlag("qwerty"), "qwerty");
  EXPECT_EQ(absl::UnparseFlag("ASDFGH"), "ASDFGH");
  EXPECT_EQ(absl::UnparseFlag("\n\t  "), "\n\t  ");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestVectorUnparsing) {
  EXPECT_EQ(absl::UnparseFlag(std::vector<std::string>{}), "");
  EXPECT_EQ(absl::UnparseFlag(std::vector<std::string>{"a", "b", "c"}),
            "a,b,c");

  EXPECT_EQ(absl::UnparseFlag(std::vector<short>{}), "");  // NOLINT
  EXPECT_EQ(absl::UnparseFlag(std::vector<short>{-1, 0, 2}),  // NOLINT
            "-1,0,2");
  EXPECT_EQ(absl::UnparseFlag(std::vector<unsigned short>{}), "");  // NOLINT
  EXPECT_EQ(absl::UnparseFlag(std::vector<unsigned short>{0, 1, 2}),  // NOLINT
            "0,1,2");
  EXPECT_EQ(absl::UnparseFlag(std::vector<int>{}), "");
  EXPECT_EQ(absl::UnparseFlag(std::vector<int>{-10, 0, 20}), "-10,0,20");
  EXPECT_EQ(absl::UnparseFlag(std::vector<unsigned int>{}), "");
  EXPECT_EQ(absl::UnparseFlag(std::vector<unsigned int>{10, 20}), "10,20");
  EXPECT_EQ(absl::UnparseFlag(std::vector<long>{}), "");  // NOLINT
  EXPECT_EQ(absl::UnparseFlag(std::vector<long>{-100, 200}),  // NOLINT
            "-100,200");
  EXPECT_EQ(absl::UnparseFlag(std::vector<unsigned long>{}), "");  // NOLINT
  EXPECT_EQ(absl::UnparseFlag(std::vector<unsigned long>{100, 200}),  // NOLINT
            "100,200");
  EXPECT_EQ(absl::UnparseFlag(std::vector<long long>{}), "");  // NOLINT
  EXPECT_EQ(absl::UnparseFlag(std::vector<long long>{-1000, 2000}),  // NOLINT
            "-1000,2000");
  EXPECT_EQ(
      absl::UnparseFlag(std::vector<unsigned long long>{}),  // NOLINT
      "");
  EXPECT_EQ(
      absl::UnparseFlag(std::vector<unsigned long long>{1000, 2000}),  // NOLINT
      "1000,2000");
  EXPECT_EQ(absl::UnparseFlag(std::vector<absl::int128>{}), "");
  EXPECT_EQ(
      absl::UnparseFlag(
          std::vector<absl::int128>{-1, absl::MakeInt128(-1, 0)}),
      "-1,-18446744073709551616");
  EXPECT_EQ(absl::UnparseFlag(std::vector<absl::uint128>{}), "");
  EXPECT_EQ(absl::UnparseFlag(std::vector<absl::uint128>{
                0, absl::MakeUint128(0, 0xFFFFFFFFFFFFFFFF)}),
            "0,18446744073709551615");

  EXPECT_EQ(absl::UnparseFlag(std::vector<float>{}), "");
  EXPECT_EQ(absl::UnparseFlag(std::vector<float>{
                1.1f, -0.0f, std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(), std::nanf("")}),
            "1.1,-0,inf,-inf,nan");

  EXPECT_EQ(absl::UnparseFlag(std::vector<double>{}), "");
  EXPECT_EQ(absl::UnparseFlag(std::vector<double>{
                1.1, -0.0, std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(), std::nan("")}),
            "1.1,-0,inf,-inf,nan");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalBoolUnparsing) {
  std::optional<bool> value;

  EXPECT_EQ(absl::UnparseFlag(value), "");
  value = true;
  EXPECT_EQ(absl::UnparseFlag(value), "true");
  value = false;
  EXPECT_EQ(absl::UnparseFlag(value), "false");
  value = std::nullopt;
  EXPECT_EQ(absl::UnparseFlag(value), "");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalIntUnparsing) {
  std::optional<int> value;

  EXPECT_EQ(absl::UnparseFlag(value), "");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -12;
  EXPECT_EQ(absl::UnparseFlag(value), "-12");
  value = std::nullopt;
  EXPECT_EQ(absl::UnparseFlag(value), "");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalDoubleUnparsing) {
  std::optional<double> value;

  EXPECT_EQ(absl::UnparseFlag(value), "");
  value = 1.;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = -1.23;
  EXPECT_EQ(absl::UnparseFlag(value), "-1.23");
  value = std::nullopt;
  EXPECT_EQ(absl::UnparseFlag(value), "");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalStringUnparsing) {
  std::optional<std::string> strvalue;
  EXPECT_EQ(absl::UnparseFlag(strvalue), "");

  strvalue = "asdfg";
  EXPECT_EQ(absl::UnparseFlag(strvalue), "asdfg");

  strvalue = " ";
  EXPECT_EQ(absl::UnparseFlag(strvalue), " ");

  strvalue = "";  // It is UB to set an optional string flag to ""
  EXPECT_EQ(absl::UnparseFlag(strvalue), "");
}

// --------------------------------------------------------------------

#if defined(ABSL_HAVE_STD_OPTIONAL) && !defined(ABSL_USES_STD_OPTIONAL)

TEST(MarshallingTest, TestStdOptionalUnparsing) {
  std::optional<std::string> strvalue;
  EXPECT_EQ(absl::UnparseFlag(strvalue), "");

  strvalue = "asdfg";
  EXPECT_EQ(absl::UnparseFlag(strvalue), "asdfg");

  strvalue = " ";
  EXPECT_EQ(absl::UnparseFlag(strvalue), " ");

  strvalue = "";  // It is UB to set an optional string flag to ""
  EXPECT_EQ(absl::UnparseFlag(strvalue), "");

  std::optional<int> intvalue;
  EXPECT_EQ(absl::UnparseFlag(intvalue), "");

  intvalue = 10;
  EXPECT_EQ(absl::UnparseFlag(intvalue), "10");
}

// --------------------------------------------------------------------

#endif

template <typename T>
void TestRoundtrip(T v) {
  T new_v;
  std::string err;
  EXPECT_TRUE(absl::ParseFlag(absl::UnparseFlag(v), &new_v, &err));
  EXPECT_EQ(new_v, v);
}

TEST(MarshallingTest, TestFloatRoundTrip) {
  TestRoundtrip(0.1f);
  TestRoundtrip(0.12f);
  TestRoundtrip(0.123f);
  TestRoundtrip(0.1234f);
  TestRoundtrip(0.12345f);
  TestRoundtrip(0.123456f);
  TestRoundtrip(0.1234567f);
  TestRoundtrip(0.12345678f);

  TestRoundtrip(0.1e20f);
  TestRoundtrip(0.12e20f);
  TestRoundtrip(0.123e20f);
  TestRoundtrip(0.1234e20f);
  TestRoundtrip(0.12345e20f);
  TestRoundtrip(0.123456e20f);
  TestRoundtrip(0.1234567e20f);
  TestRoundtrip(0.12345678e20f);

  TestRoundtrip(0.1e-20f);
  TestRoundtrip(0.12e-20f);
  TestRoundtrip(0.123e-20f);
  TestRoundtrip(0.1234e-20f);
  TestRoundtrip(0.12345e-20f);
  TestRoundtrip(0.123456e-20f);
  TestRoundtrip(0.1234567e-20f);
  TestRoundtrip(0.12345678e-20f);

  TestRoundtrip(std::numeric_limits<float>::min());
  TestRoundtrip(std::numeric_limits<float>::max());
  TestRoundtrip(std::numeric_limits<float>::lowest());
  TestRoundtrip(std::numeric_limits<float>::denorm_min());
}

TEST(MarshallingTest, TestDoubleRoundTrip) {
  TestRoundtrip(0.1);
  TestRoundtrip(0.12);
  TestRoundtrip(0.123);
  TestRoundtrip(0.1234);
  TestRoundtrip(0.12345);
  TestRoundtrip(0.123456);
  TestRoundtrip(0.1234567);
  TestRoundtrip(0.12345678);
  TestRoundtrip(0.123456789);
  TestRoundtrip(0.1234567891);
  TestRoundtrip(0.12345678912);
  TestRoundtrip(0.123456789123);
  TestRoundtrip(0.1234567891234);
  TestRoundtrip(0.12345678912345);
  TestRoundtrip(0.123456789123456);
  TestRoundtrip(0.1234567891234567);
  TestRoundtrip(0.12345678912345678);

  TestRoundtrip(0.1e50);
  TestRoundtrip(0.12e50);
  TestRoundtrip(0.123e50);
  TestRoundtrip(0.1234e50);
  TestRoundtrip(0.12345e50);
  TestRoundtrip(0.123456e50);
  TestRoundtrip(0.1234567e50);
  TestRoundtrip(0.12345678e50);
  TestRoundtrip(0.123456789e50);
  TestRoundtrip(0.1234567891e50);
  TestRoundtrip(0.12345678912e50);
  TestRoundtrip(0.123456789123e50);
  TestRoundtrip(0.1234567891234e50);
  TestRoundtrip(0.12345678912345e50);
  TestRoundtrip(0.123456789123456e50);
  TestRoundtrip(0.1234567891234567e50);
  TestRoundtrip(0.12345678912345678e50);

  TestRoundtrip(0.1e-50);
  TestRoundtrip(0.12e-50);
  TestRoundtrip(0.123e-50);
  TestRoundtrip(0.1234e-50);
  TestRoundtrip(0.12345e-50);
  TestRoundtrip(0.123456e-50);
  TestRoundtrip(0.1234567e-50);
  TestRoundtrip(0.12345678e-50);
  TestRoundtrip(0.123456789e-50);
  TestRoundtrip(0.1234567891e-50);
  TestRoundtrip(0.12345678912e-50);
  TestRoundtrip(0.123456789123e-50);
  TestRoundtrip(0.1234567891234e-50);
  TestRoundtrip(0.12345678912345e-50);
  TestRoundtrip(0.123456789123456e-50);
  TestRoundtrip(0.1234567891234567e-50);
  TestRoundtrip(0.12345678912345678e-50);

  TestRoundtrip(std::numeric_limits<double>::min());
  TestRoundtrip(std::numeric_limits<double>::max());
  TestRoundtrip(std::numeric_limits<double>::lowest());
  TestRoundtrip(std::numeric_limits<double>::denorm_min());
}

TEST(MarshallingTest, TestVectorNumericRoundTrip) {
  TestRoundtrip(std::vector<int64_t>{});
  TestRoundtrip(std::vector<int64_t>{
      std::numeric_limits<int64_t>::min(), -1, 0, 1,
      std::numeric_limits<int64_t>::max()});
  TestRoundtrip(std::vector<float>{});
  TestRoundtrip(std::vector<float>{
      0.12345678f, 0.12345678e20f, 0.12345678e-20f,
      std::numeric_limits<float>::min(), std::numeric_limits<float>::max(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::denorm_min()});
  TestRoundtrip(std::vector<double>{});
  TestRoundtrip(std::vector<double>{
      0.12345678912345678, 0.12345678912345678e50, 0.12345678912345678e-50,
      std::numeric_limits<double>::min(), std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::denorm_min()});
}

}  // namespace
