// Copyright 2026 The Abseil Authors.
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

#include "absl/log/internal/object_pool.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <type_traits>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
namespace {

struct TestItem {
  struct Counters {
    std::atomic<int> constructions{0};
    std::atomic<int> resets{0};
    std::atomic<int> destructions{0};
  };

  TestItem(Counters* counters_arg, int value_arg, std::string label_arg)
      : counters(counters_arg), value(value_arg), label(std::move(label_arg)) {
    counters->constructions.fetch_add(1, std::memory_order_relaxed);
  }

  TestItem(const TestItem&) = delete;
  TestItem& operator=(const TestItem&) = delete;

  ~TestItem() {
    counters->destructions.fetch_add(1, std::memory_order_relaxed);
  }

  void Reset(Counters* counters_arg, int value_arg, std::string label_arg) {
    counters = counters_arg;
    value = value_arg;
    label = std::move(label_arg);
    counters->resets.fetch_add(1, std::memory_order_relaxed);
  }

  Counters* counters;
  int value;
  std::string label;
};

static_assert(std::is_trivially_destructible_v<ObjectPool<TestItem, 4>>);

ABSL_CONST_INIT ObjectPool<TestItem, 4> const_init_pool;

TEST(ObjectPoolTest, AcquireAndReleaseReusesPooledObject) {
  TestItem::Counters counters;
  std::unique_ptr<TestItem> first =
      const_init_pool.Acquire(&counters, 10, "first");
  ASSERT_NE(first, nullptr);
  const TestItem* first_ptr = first.get();
  EXPECT_EQ(first->value, 10);
  EXPECT_EQ(first->label, "first");
  EXPECT_EQ(counters.constructions.load(), 1);
  EXPECT_EQ(counters.resets.load(), 0);
  EXPECT_EQ(counters.destructions.load(), 0);

  const_init_pool.Release(std::move(first));
  EXPECT_EQ(counters.destructions.load(), 0);

  std::unique_ptr<TestItem> second =
      const_init_pool.Acquire(&counters, 20, "second");
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second.get(), first_ptr);
  EXPECT_EQ(second->value, 20);
  EXPECT_EQ(second->label, "second");
  EXPECT_EQ(counters.constructions.load(), 1);
  EXPECT_EQ(counters.resets.load(), 1);
  EXPECT_EQ(counters.destructions.load(), 0);

  const_init_pool.Release(std::move(second));
  EXPECT_EQ(counters.destructions.load(), 0);
}

TEST(ObjectPoolTest, OverflowAllocatesAndDeletesWhenPoolExhausted) {
  ABSL_CONST_INIT static ObjectPool<TestItem, 2> pool;
  TestItem::Counters counters;

  std::unique_ptr<TestItem> first = pool.Acquire(&counters, 1, "one");
  std::unique_ptr<TestItem> second = pool.Acquire(&counters, 2, "two");
  std::unique_ptr<TestItem> overflow = pool.Acquire(&counters, 3, "three");

  EXPECT_NE(first.get(), second.get());
  EXPECT_NE(first.get(), overflow.get());
  EXPECT_NE(second.get(), overflow.get());
  EXPECT_EQ(counters.constructions.load(), 3);
  EXPECT_EQ(counters.destructions.load(), 0);

  // Releasing the overflow item must destroy it immediately because it does not
  // belong to any of the pool's slots.
  pool.Release(std::move(overflow));
  EXPECT_EQ(counters.destructions.load(), 1);

  const TestItem* first_ptr = first.get();
  const TestItem* second_ptr = second.get();
  pool.Release(std::move(first));
  pool.Release(std::move(second));
  EXPECT_EQ(counters.destructions.load(), 1);

  // Subsequent acquisition reuses one of the pooled items.
  std::unique_ptr<TestItem> reused = pool.Acquire(&counters, 4, "four");
  EXPECT_TRUE(reused.get() == first_ptr || reused.get() == second_ptr);
  EXPECT_EQ(reused->value, 4);
  EXPECT_EQ(reused->label, "four");
  EXPECT_EQ(counters.constructions.load(), 3);
  EXPECT_EQ(counters.resets.load(), 1);
  pool.Release(std::move(reused));
}

TEST(ObjectPoolTest, ReleaseNullptrIsNoOp) {
  ABSL_CONST_INIT static ObjectPool<TestItem, 2> pool;
  pool.Release(nullptr);
}

TEST(ObjectPoolTest, ConcurrentAcquireAndRelease) {
  ABSL_CONST_INIT static ObjectPool<TestItem, 4> pool;
  TestItem::Counters counters;

  constexpr size_t kNumThreads = 16;
  static constexpr int kIterationsPerThread = 200;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (size_t t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&counters, t]() {
      const std::string expected_label = "thread_" + std::to_string(t);
      for (int i = 0; i < kIterationsPerThread; ++i) {
        const int expected_val = static_cast<int>(t) * 1000 + i;
        std::unique_ptr<TestItem> item =
            pool.Acquire(&counters, expected_val, expected_label);
        EXPECT_EQ(item->value, expected_val);
        EXPECT_EQ(item->label, expected_label);
        item->value = -expected_val;
        EXPECT_EQ(item->value, -expected_val);
        pool.Release(std::move(item));
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }

  const int total_acquires =
      static_cast<int>(kNumThreads) * kIterationsPerThread;
  EXPECT_EQ(counters.constructions.load() + counters.resets.load(),
            total_acquires);
  // Up to 4 items remain live in the pool; any overflow items were destroyed.
  EXPECT_LE(counters.constructions.load() - counters.destructions.load(), 4);
}

}  // namespace
}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
