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

#include "absl/flags/reflection.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "absl/flags/internal/commandlineflag.h"
#include "absl/flags/marshalling.h"
#include "absl/memory/memory.h"

ABSL_FLAG(int, int_flag, 1, "int_flag help");
ABSL_FLAG(std::string, string_flag, "dflt", "string_flag help");
ABSL_RETIRED_FLAG(bool, bool_retired_flag, false, "bool_retired_flag help");

namespace {

namespace flags = absl::flags_internal;

class ReflectionTest : public testing::Test {
 protected:
  void SetUp() override { flag_saver_ = absl::make_unique<absl::FlagSaver>(); }
  void TearDown() override { flag_saver_.reset(); }

 private:
  std::unique_ptr<absl::FlagSaver> flag_saver_;
};

// --------------------------------------------------------------------

TEST_F(ReflectionTest, TestFindCommandLineFlag) {
  auto* handle = absl::FindCommandLineFlag("some_flag");
  EXPECT_EQ(handle, nullptr);

  handle = absl::FindCommandLineFlag("int_flag");
  EXPECT_NE(handle, nullptr);

  handle = absl::FindCommandLineFlag("string_flag");
  EXPECT_NE(handle, nullptr);

  handle = absl::FindCommandLineFlag("bool_retired_flag");
  EXPECT_NE(handle, nullptr);
}

}  // namespace
