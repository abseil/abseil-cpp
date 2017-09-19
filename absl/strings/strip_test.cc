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

// This file contains functions that remove a defined part from the std::string,
// i.e., strip the std::string.

#include "absl/strings/strip.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace {

using testing::ElementsAre;
using testing::IsEmpty;

TEST(Strip, ConsumePrefixOneChar) {
  absl::string_view input("abc");
  EXPECT_TRUE(absl::ConsumePrefix(&input, "a"));
  EXPECT_EQ(input, "bc");

  EXPECT_FALSE(absl::ConsumePrefix(&input, "x"));
  EXPECT_EQ(input, "bc");

  EXPECT_TRUE(absl::ConsumePrefix(&input, "b"));
  EXPECT_EQ(input, "c");

  EXPECT_TRUE(absl::ConsumePrefix(&input, "c"));
  EXPECT_EQ(input, "");

  EXPECT_FALSE(absl::ConsumePrefix(&input, "a"));
  EXPECT_EQ(input, "");
}

TEST(Strip, ConsumePrefix) {
  absl::string_view input("abcdef");
  EXPECT_FALSE(absl::ConsumePrefix(&input, "abcdefg"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_FALSE(absl::ConsumePrefix(&input, "abce"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumePrefix(&input, ""));
  EXPECT_EQ(input, "abcdef");

  EXPECT_FALSE(absl::ConsumePrefix(&input, "abcdeg"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumePrefix(&input, "abcdef"));
  EXPECT_EQ(input, "");

  input = "abcdef";
  EXPECT_TRUE(absl::ConsumePrefix(&input, "abcde"));
  EXPECT_EQ(input, "f");
}

TEST(Strip, ConsumeSuffix) {
  absl::string_view input("abcdef");
  EXPECT_FALSE(absl::ConsumeSuffix(&input, "abcdefg"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumeSuffix(&input, ""));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumeSuffix(&input, "def"));
  EXPECT_EQ(input, "abc");

  input = "abcdef";
  EXPECT_FALSE(absl::ConsumeSuffix(&input, "abcdeg"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumeSuffix(&input, "f"));
  EXPECT_EQ(input, "abcde");

  EXPECT_TRUE(absl::ConsumeSuffix(&input, "abcde"));
  EXPECT_EQ(input, "");
}

TEST(Strip, StripPrefix) {
  const absl::string_view null_str;

  EXPECT_EQ(absl::StripPrefix("foobar", "foo"), "bar");
  EXPECT_EQ(absl::StripPrefix("foobar", ""), "foobar");
  EXPECT_EQ(absl::StripPrefix("foobar", null_str), "foobar");
  EXPECT_EQ(absl::StripPrefix("foobar", "foobar"), "");
  EXPECT_EQ(absl::StripPrefix("foobar", "bar"), "foobar");
  EXPECT_EQ(absl::StripPrefix("foobar", "foobarr"), "foobar");
  EXPECT_EQ(absl::StripPrefix("", ""), "");
}

TEST(Strip, StripSuffix) {
  const absl::string_view null_str;

  EXPECT_EQ(absl::StripSuffix("foobar", "bar"), "foo");
  EXPECT_EQ(absl::StripSuffix("foobar", ""), "foobar");
  EXPECT_EQ(absl::StripSuffix("foobar", null_str), "foobar");
  EXPECT_EQ(absl::StripSuffix("foobar", "foobar"), "");
  EXPECT_EQ(absl::StripSuffix("foobar", "foo"), "foobar");
  EXPECT_EQ(absl::StripSuffix("foobar", "ffoobar"), "foobar");
  EXPECT_EQ(absl::StripSuffix("", ""), "");
}

}  // namespace
