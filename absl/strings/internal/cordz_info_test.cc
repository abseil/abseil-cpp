// Copyright 2019 The Abseil Authors.
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

#include "absl/strings/internal/cordz_info.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/internal/cordz_handle.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Ne;

struct TestCordRep {
  CordRepFlat* rep;

  TestCordRep() {
    rep = CordRepFlat::New(100);
    rep->length = 100;
    memset(rep->Data(), 1, 100);
  }
  ~TestCordRep() { CordRepFlat::Delete(rep); }
};

// Local less verbose helper
std::vector<const CordzHandle*> DeleteQueue() {
  return CordzHandle::DiagnosticsGetDeleteQueue();
}

std::string FormatStack(absl::Span<void* const> raw_stack) {
  static constexpr size_t buf_size = 1 << 14;
  std::unique_ptr<char[]> buf(new char[buf_size]);
  std::string output;
  for (void* stackp : raw_stack) {
    if (absl::Symbolize(stackp, buf.get(), buf_size)) {
      absl::StrAppend(&output, "    ", buf.get(), "\n");
    }
  }
  return output;
}

TEST(CordzInfoTest, TrackCord) {
  TestCordRep rep;
  CordzInfo* info = CordzInfo::TrackCord(rep.rep);
  ASSERT_THAT(info, Ne(nullptr));
  EXPECT_FALSE(info->is_snapshot());
  EXPECT_THAT(CordzInfo::Head(CordzSnapshot()), Eq(info));
  EXPECT_THAT(info->GetCordRepForTesting(), Eq(rep.rep));
  CordzInfo::UntrackCord(info);
}

TEST(CordzInfoTest, UntrackCord) {
  TestCordRep rep;
  CordzInfo* info = CordzInfo::TrackCord(rep.rep);

  CordzSnapshot snapshot;
  CordzInfo::UntrackCord(info);
  EXPECT_THAT(CordzInfo::Head(CordzSnapshot()), Eq(nullptr));
  EXPECT_THAT(info->GetCordRepForTesting(), Eq(nullptr));
  EXPECT_THAT(DeleteQueue(), ElementsAre(info, &snapshot));
}

TEST(CordzInfoTest, SetCordRep) {
  TestCordRep rep;
  CordzInfo* info = CordzInfo::TrackCord(rep.rep);

  TestCordRep rep2;
  {
    absl::MutexLock lock(&info->mutex());
    info->SetCordRep(rep2.rep);
  }
  EXPECT_THAT(info->GetCordRepForTesting(), Eq(rep2.rep));

  CordzInfo::UntrackCord(info);
}

#if GTEST_HAS_DEATH_TEST

TEST(CordzInfoTest, SetCordRepRequiresMutex) {
  TestCordRep rep;
  CordzInfo* info = CordzInfo::TrackCord(rep.rep);
  TestCordRep rep2;
  EXPECT_DEATH(info->SetCordRep(rep2.rep), ".*");
  CordzInfo::UntrackCord(info);
}

#endif  // GTEST_HAS_DEATH_TEST

TEST(CordzInfoTest, TrackUntrackHeadFirstV2) {
  TestCordRep rep;
  CordzSnapshot snapshot;
  EXPECT_THAT(CordzInfo::Head(snapshot), Eq(nullptr));

  CordzInfo* info1 = CordzInfo::TrackCord(rep.rep);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  CordzInfo* info2 = CordzInfo::TrackCord(rep.rep);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info2));
  EXPECT_THAT(info2->Next(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  CordzInfo::UntrackCord(info2);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  CordzInfo::UntrackCord(info1);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(nullptr));
}

TEST(CordzInfoTest, TrackUntrackTailFirstV2) {
  TestCordRep rep;
  CordzSnapshot snapshot;
  EXPECT_THAT(CordzInfo::Head(snapshot), Eq(nullptr));

  CordzInfo* info1 = CordzInfo::TrackCord(rep.rep);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  CordzInfo* info2 = CordzInfo::TrackCord(rep.rep);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info2));
  EXPECT_THAT(info2->Next(snapshot), Eq(info1));
  EXPECT_THAT(info1->Next(snapshot), Eq(nullptr));

  CordzInfo::UntrackCord(info1);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(info2));
  EXPECT_THAT(info2->Next(snapshot), Eq(nullptr));

  CordzInfo::UntrackCord(info2);
  ASSERT_THAT(CordzInfo::Head(snapshot), Eq(nullptr));
}

TEST(CordzInfoTest, StackV2) {
  TestCordRep rep;
  // kMaxStackDepth is intentionally less than 64 (which is the max depth that
  // Cordz will record) because if the actual stack depth is over 64
  // (which it is on Apple platforms) then the expected_stack will end up
  // catching a few frames at the end that the actual_stack didn't get and
  // it will no longer be subset. At the time of this writing 58 is the max
  // that will allow this test to pass (with a minimum os version of iOS 9), so
  // rounded down to 50 to hopefully not run into this in the future if Apple
  // makes small modifications to its testing stack. 50 is sufficient to prove
  // that we got a decent stack.
  static constexpr int kMaxStackDepth = 50;
  CordzInfo* info = CordzInfo::TrackCord(rep.rep);
  std::vector<void*> local_stack;
  local_stack.resize(kMaxStackDepth);
  // In some environments we don't get stack traces. For example in Android
  // absl::GetStackTrace will return 0 indicating it didn't find any stack. The
  // resultant formatted stack will be "", but that still equals the stack
  // recorded in CordzInfo, which is also empty. The skip_count is 1 so that the
  // line number of the current stack isn't included in the HasSubstr check.
  local_stack.resize(absl::GetStackTrace(local_stack.data(), kMaxStackDepth,
                                         /*skip_count=*/1));

  std::string got_stack = FormatStack(info->GetStack());
  std::string expected_stack = FormatStack(local_stack);
  // If TrackCord is inlined, got_stack should match expected_stack. If it isn't
  // inlined, got_stack should include an additional frame not present in
  // expected_stack. Either way, expected_stack should be a substring of
  // got_stack.
  EXPECT_THAT(got_stack, HasSubstr(expected_stack));

  CordzInfo::UntrackCord(info);
}

// Local helper functions to get different stacks for child and parent.
CordzInfo* TrackChildCord(CordRep* rep, const CordzInfo* parent) {
  return CordzInfo::TrackCord(rep, parent);
}
CordzInfo* TrackParentCord(CordRep* rep) {
  return CordzInfo::TrackCord(rep);
}

TEST(CordzInfoTest, ParentStackV2) {
  TestCordRep rep;
  CordzInfo* info_parent = TrackParentCord(rep.rep);
  CordzInfo* info_child = TrackChildCord(rep.rep, info_parent);

  std::string stack = FormatStack(info_parent->GetStack());
  std::string parent_stack = FormatStack(info_child->GetParentStack());
  EXPECT_THAT(stack, Eq(parent_stack));

  CordzInfo::UntrackCord(info_parent);
  CordzInfo::UntrackCord(info_child);
}

TEST(CordzInfoTest, ParentStackEmpty) {
  TestCordRep rep;
  CordzInfo* info = TrackChildCord(rep.rep, nullptr);
  EXPECT_TRUE(info->GetParentStack().empty());
  CordzInfo::UntrackCord(info);
}

TEST(CordzInfoTest, CordzStatisticsV2) {
  TestCordRep rep;
  CordzInfo* info = TrackParentCord(rep.rep);

  CordzStatistics expected;
  expected.size = 100;
  info->RecordMetrics(expected.size);

  CordzStatistics actual = info->GetCordzStatistics();
  EXPECT_EQ(actual.size, expected.size);

  CordzInfo::UntrackCord(info);
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
