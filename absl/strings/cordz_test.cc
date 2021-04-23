// Copyright 2021 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/strings/cord.h"
#include "absl/strings/cordz_test_helpers.h"
#include "absl/strings/internal/cordz_functions.h"
#include "absl/strings/internal/cordz_info.h"
#include "absl/strings/internal/cordz_sample_token.h"
#include "absl/strings/internal/cordz_statistics.h"
#include "absl/strings/internal/cordz_update_tracker.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#ifdef ABSL_INTERNAL_CORDZ_ENABLED

namespace absl {
ABSL_NAMESPACE_BEGIN

using cord_internal::CordzInfo;
using cord_internal::CordzSampleToken;
using cord_internal::CordzStatistics;
using cord_internal::CordzUpdateTracker;
using Method = CordzUpdateTracker::MethodIdentifier;

namespace {

std::string MakeString(int length) {
  std::string s(length, '.');
  for (int i = 4; i < length; i += 2) {
    s[i] = '\b';
  }
  return s;
}

TEST(CordzTest, ConstructSmallStringView) {
  CordzSamplingIntervalHelper sample_every(1);
  Cord cord(absl::string_view(MakeString(50)));
  EXPECT_THAT(cord, HasValidCordzInfoOf(Method::kConstructorString));
}

TEST(CordzTest, ConstructLargeStringView) {
  CordzSamplingIntervalHelper sample_every(1);
  Cord cord(absl::string_view(MakeString(5000)));
  EXPECT_THAT(cord, HasValidCordzInfoOf(Method::kConstructorString));
}

TEST(CordzTest, CopyConstruct) {
  CordzSamplingIntervalHelper sample_every(1);
  Cord src = UnsampledCord(MakeString(5000));
  Cord cord(src);
  EXPECT_THAT(cord, HasValidCordzInfoOf(Method::kConstructorCord));
}

TEST(CordzTest, AppendLargeCordToEmpty) {
  CordzSamplingIntervalHelper sample_every(1);
  Cord cord;
  Cord src = UnsampledCord(MakeString(5000));
  cord.Append(src);
  EXPECT_THAT(cord, HasValidCordzInfoOf(Method::kAppendCord));
}

TEST(CordzTest, MoveAppendLargeCordToEmpty) {
  CordzSamplingIntervalHelper sample_every(1);
  Cord cord;
  cord.Append(UnsampledCord(MakeString(5000)));
  EXPECT_THAT(cord, HasValidCordzInfoOf(Method::kAppendCord));
}

}  // namespace

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_CORDZ_ENABLED
