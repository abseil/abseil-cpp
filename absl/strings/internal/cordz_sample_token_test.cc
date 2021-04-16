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

#include "absl/strings/internal/cordz_sample_token.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/internal/cordz_handle.h"
#include "absl/strings/internal/cordz_info.h"
#include "absl/synchronization/internal/thread_pool.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
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

TEST(CordzSampleTokenTest, IteratorTraits) {
  static_assert(std::is_copy_constructible<CordzSampleToken::Iterator>::value,
                "");
  static_assert(std::is_copy_assignable<CordzSampleToken::Iterator>::value, "");
  static_assert(std::is_move_constructible<CordzSampleToken::Iterator>::value,
                "");
  static_assert(std::is_move_assignable<CordzSampleToken::Iterator>::value, "");
  static_assert(
      std::is_same<
          std::iterator_traits<CordzSampleToken::Iterator>::iterator_category,
          std::input_iterator_tag>::value,
      "");
  static_assert(
      std::is_same<std::iterator_traits<CordzSampleToken::Iterator>::value_type,
                   const CordzInfo&>::value,
      "");
  static_assert(
      std::is_same<
          std::iterator_traits<CordzSampleToken::Iterator>::difference_type,
          ptrdiff_t>::value,
      "");
  static_assert(
      std::is_same<std::iterator_traits<CordzSampleToken::Iterator>::pointer,
                   const CordzInfo*>::value,
      "");
  static_assert(
      std::is_same<std::iterator_traits<CordzSampleToken::Iterator>::reference,
                   const CordzInfo&>::value,
      "");
}

TEST(CordzSampleTokenTest, IteratorEmpty) {
  CordzSampleToken token;
  EXPECT_THAT(token.begin(), Eq(token.end()));
}

TEST(CordzSampleTokenTest, Iterator) {
  TestCordRep rep1;
  TestCordRep rep2;
  TestCordRep rep3;
  CordzInfo* info1 = CordzInfo::TrackCord(rep1.rep);
  CordzInfo* info2 = CordzInfo::TrackCord(rep2.rep);
  CordzInfo* info3 = CordzInfo::TrackCord(rep3.rep);

  CordzSampleToken token;
  std::vector<const CordzInfo*> found;
  for (const CordzInfo& cord_info : token) {
    found.push_back(&cord_info);
  }

  EXPECT_THAT(found, ElementsAre(info3, info2, info1));

  CordzInfo::UntrackCord(info1);
  CordzInfo::UntrackCord(info2);
  CordzInfo::UntrackCord(info3);
}

TEST(CordzSampleTokenTest, IteratorEquality) {
  TestCordRep rep1;
  TestCordRep rep2;
  TestCordRep rep3;
  CordzInfo* info1 = CordzInfo::TrackCord(rep1.rep);

  CordzSampleToken token1;
  // lhs starts with the CordzInfo corresponding to cord1 at the head.
  CordzSampleToken::Iterator lhs = token1.begin();

  CordzInfo* info2 = CordzInfo::TrackCord(rep2.rep);

  CordzSampleToken token2;
  // rhs starts with the CordzInfo corresponding to cord2 at the head.
  CordzSampleToken::Iterator rhs = token2.begin();

  CordzInfo* info3 = CordzInfo::TrackCord(rep3.rep);

  // lhs is on cord1 while rhs is on cord2.
  EXPECT_THAT(lhs, Ne(rhs));

  rhs++;
  // lhs and rhs are both on cord1, but they didn't come from the same
  // CordzSampleToken.
  EXPECT_THAT(lhs, Ne(rhs));

  lhs++;
  rhs++;
  // Both lhs and rhs are done, so they are on nullptr.
  EXPECT_THAT(lhs, Eq(rhs));

  CordzInfo::UntrackCord(info1);
  CordzInfo::UntrackCord(info2);
  CordzInfo::UntrackCord(info3);
}

TEST(CordzSampleTokenTest, MultiThreaded) {
  Notification stop;
  static constexpr int kNumThreads = 4;
  static constexpr int kNumCords = 3;
  static constexpr int kNumTokens = 3;
  absl::synchronization_internal::ThreadPool pool(kNumThreads);

  for (int i = 0; i < kNumThreads; ++i) {
    pool.Schedule([&stop]() {
      absl::BitGen gen;
      TestCordRep reps[kNumCords];
      CordzInfo* infos[kNumCords] = {nullptr};
      std::vector<std::unique_ptr<CordzSampleToken>> tokens;
      tokens.resize(kNumTokens);

      while (!stop.HasBeenNotified()) {
        // Randomly perform one of five actions:
        //   1) Untrack
        //   2) Track
        //   3) Iterate over Cords visible to a token.
        //   4) Unsample
        //   5) Sample
        int index = absl::Uniform(gen, 0, kNumCords);
        if (absl::Bernoulli(gen, 0.5)) {
          // Track/untrack.
          if (infos[index]) {
            // 1) Untrack
            CordzInfo::UntrackCord(infos[index]);
            infos[index] = nullptr;
          } else {
            // 2) Track
            infos[index] = CordzInfo::TrackCord(reps[index].rep);
          }
        } else {
          if (tokens[index]) {
            if (absl::Bernoulli(gen, 0.5)) {
              // 3) Iterate over Cords visible to a token.
              for (const CordzInfo& info : *tokens[index]) {
                // This is trivial work to allow us to compile the loop.
                EXPECT_THAT(info.Next(*tokens[index]), Ne(&info));
              }
            } else {
              // 4) Unsample
              tokens[index].reset();
            }
          } else {
            // 5) Sample
            tokens[index] = absl::make_unique<CordzSampleToken>();
          }
        }
      }
      for (CordzInfo* info : infos) {
        if (info != nullptr) {
          CordzInfo::UntrackCord(info);
        }
      }
    });
  }
  // The threads will hammer away.  Give it a little bit of time for tsan to
  // spot errors.
  absl::SleepFor(absl::Seconds(3));
  stop.Notify();
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
