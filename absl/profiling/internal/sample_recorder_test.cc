// Copyright 2018 The Abseil Authors.
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

#include "absl/profiling/internal/sample_recorder.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/thread_annotations.h"
#include "absl/random/random.h"
#include "absl/synchronization/internal/thread_pool.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {

namespace {
using ::absl::synchronization_internal::ThreadPool;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

struct Info : public Sample<Info> {
 public:
  void PrepareForSampling(int64_t w) { weight = w; }
  std::atomic<size_t> size;
  absl::Time create_time;
};

std::vector<size_t> GetSizes(SampleRecorder<Info>* s) {
  std::vector<size_t> res;
  EXPECT_EQ(s->Iterate([&](const Info& info) {
    res.push_back(info.size.load(std::memory_order_acquire));
  }),
            0);
  return res;
}

std::vector<int64_t> GetWeights(SampleRecorder<Info>* s) {
  std::vector<int64_t> res;
  EXPECT_EQ(s->Iterate([&](const Info& info) { res.push_back(info.weight); }),
            0);
  return res;
}

Info* Register(SampleRecorder<Info>* s, int64_t weight, size_t size) {
  auto* info = s->Register(weight);
  assert(info != nullptr);
  info->size.store(size);
  return info;
}

TEST(SampleRecorderTest, Registration) {
  SampleRecorder<Info> sampler;
  auto* info1 = Register(&sampler, 31, 1);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(1));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(31));

  auto* info2 = Register(&sampler, 32, 2);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(1, 2));
  info1->size.store(3);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(3, 2));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(31, 32));

  sampler.Unregister(info1);
  sampler.Unregister(info2);
}

TEST(SampleRecorderTest, Unregistration) {
  SampleRecorder<Info> sampler;
  std::vector<Info*> infos;
  for (size_t i = 0; i < 3; ++i) {
    infos.push_back(Register(&sampler, 33 + i, i));
  }
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(0, 1, 2));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(33, 34, 35));

  sampler.Unregister(infos[1]);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(0, 2));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(33, 35));

  infos.push_back(Register(&sampler, 36, 3));
  infos.push_back(Register(&sampler, 37, 4));
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(0, 2, 3, 4));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(33, 35, 36, 37));
  sampler.Unregister(infos[3]);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(0, 2, 4));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(33, 35, 37));

  sampler.Unregister(infos[0]);
  sampler.Unregister(infos[2]);
  sampler.Unregister(infos[4]);
  EXPECT_THAT(GetSizes(&sampler), IsEmpty());
}

TEST(SampleRecorderTest, MultiThreaded) {
  SampleRecorder<Info> sampler;
  Notification stop;
  ThreadPool pool(10);

  for (int i = 0; i < 10; ++i) {
    pool.Schedule([&sampler, &stop, i]() {
      absl::InsecureBitGen gen;
      std::vector<Info*> infoz;
      while (!stop.HasBeenNotified()) {
        if (infoz.empty()) {
          infoz.push_back(sampler.Register(i));
        }
        switch (std::uniform_int_distribution<>(0, 2)(gen)) {
          case 0: {
            infoz.push_back(sampler.Register(i));
            break;
          }
          case 1: {
            size_t p =
                std::uniform_int_distribution<>(0, infoz.size() - 1)(gen);
            Info* info = infoz[p];
            infoz[p] = infoz.back();
            infoz.pop_back();
            EXPECT_EQ(info->weight, i);
            sampler.Unregister(info);
            break;
          }
          case 2: {
            absl::Duration oldest = absl::ZeroDuration();
            EXPECT_EQ(sampler.Iterate([&](const Info& info) {
              oldest = std::max(oldest, absl::Now() - info.create_time);
            }),
                      0);
            ASSERT_GE(oldest, absl::ZeroDuration());
            break;
          }
        }
      }
    });
  }
  // The threads will hammer away.  Give it a little bit of time for tsan to
  // spot errors.
  absl::SleepFor(absl::Seconds(3));
  stop.Notify();
}

TEST(SampleRecorderTest, Callback) {
  SampleRecorder<Info> sampler;

  auto* info1 = Register(&sampler, 39, 1);
  auto* info2 = Register(&sampler, 40, 2);

  static const Info* expected;

  auto callback = [](const Info& info) {
    // We can't use `info` outside of this callback because the object will be
    // disposed as soon as we return from here.
    EXPECT_EQ(&info, expected);
  };

  // Set the callback.
  EXPECT_EQ(sampler.SetDisposeCallback(callback), nullptr);
  expected = info1;
  sampler.Unregister(info1);

  // Unset the callback.
  EXPECT_EQ(callback, sampler.SetDisposeCallback(nullptr));
  expected = nullptr;  // no more calls.
  sampler.Unregister(info2);
}

TEST(SampleRecorderTest, ConcurrentChurnResurrectionAndIteration) {
  SampleRecorder<Info> sampler;
  static std::atomic<int64_t> disposed_count{0};
  disposed_count.store(0, std::memory_order_relaxed);
  sampler.SetDisposeCallback([](const Info& info) {
    EXPECT_GE(info.weight, 100);
    disposed_count.fetch_add(1, std::memory_order_relaxed);
  });

  Notification stop;
  ThreadPool pool(8);
  for (int t = 0; t < 6; ++t) {
    pool.Schedule([&sampler, &stop, t]() {
      const int64_t w = 100 + t;
      while (!stop.HasBeenNotified()) {
        Info* a = sampler.Register(w);
        Info* b = sampler.Register(w);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        ASSERT_NE(a, b);
        a->size.store(static_cast<size_t>(t + 1), std::memory_order_release);
        b->size.store(static_cast<size_t>(t + 1), std::memory_order_release);
        sampler.Unregister(a);
        sampler.Unregister(b);
      }
    });
  }
  for (int t = 0; t < 2; ++t) {
    pool.Schedule([&sampler, &stop]() {
      while (!stop.HasBeenNotified()) {
        sampler.Iterate([](const Info& info) {
          EXPECT_GE(info.weight, 100);
          EXPECT_LT(info.weight, 106);
        });
      }
    });
  }
  absl::SleepFor(absl::Milliseconds(500));
  stop.Notify();
  sampler.SetDisposeCallback(nullptr);
  EXPECT_GT(disposed_count.load(std::memory_order_relaxed), 0);
}

TEST(SampleRecorderTest, CrossThreadShardStealingAndMaxSamples) {
  SampleRecorder<Info> sampler;
  // Register() checks `size > max_samples_` where `size` is pre-increment, so
  // max_samples = 7 allows indices 0..7 (8 concurrent live samples).
  sampler.SetMaxSamples(7);

  // Unregister samples from one worker thread and resurrect them all from
  // a different thread to exercise cross-shard graveyard stealing and soft
  // max_samples enforcement without leaking or duplicating nodes.
  std::vector<Info*> seeded;
  {
    ThreadPool producer_pool(1);
    Notification done;
    producer_pool.Schedule([&]() {
      for (int i = 0; i < 8; ++i) {
        Info* s = sampler.Register(200 + i);
        ASSERT_NE(s, nullptr);
        seeded.push_back(s);
      }
      for (Info* s : seeded) {
        sampler.Unregister(s);
      }
      done.Notify();
    });
    done.WaitForNotification();
  }

  std::vector<Info*> resurrected;
  for (int i = 0; i < 8; ++i) {
    Info* s = sampler.Register(300 + i);
    ASSERT_NE(s, nullptr);
    resurrected.push_back(s);
  }
  // Every sample must have been stolen from the producer's graveyard shard
  // rather than allocating duplicate heap entries.
  std::sort(seeded.begin(), seeded.end());
  std::vector<Info*> sorted_resurrected = resurrected;
  std::sort(sorted_resurrected.begin(), sorted_resurrected.end());
  EXPECT_EQ(seeded, sorted_resurrected);

  // Exceeding max_samples drops registrations cleanly and tracks dropped count.
  EXPECT_EQ(sampler.Register(999), nullptr);
  size_t live = 0;
  EXPECT_EQ(sampler.Iterate([&](const Info&) { ++live; }), 1);
  EXPECT_EQ(live, 8);

  for (Info* s : resurrected) {
    sampler.Unregister(s);
  }
}

}  // namespace
}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl
