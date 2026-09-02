//
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

#include "absl/log/initialize.h"

#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/internal/globals.h"

namespace {

TEST(InitializeLogTest, FirstCallInitializes) {
  absl::InitializeLog();
  EXPECT_TRUE(absl::log_internal::IsInitialized());
}

TEST(InitializeLogTest, SecondCallIsNoOp) {
  // The first call may have happened in a previous test in the same binary;
  // either way, calling again must not fail and must leave the library in the
  // initialized state.
  absl::InitializeLog();
  absl::InitializeLog();
  EXPECT_TRUE(absl::log_internal::IsInitialized());
}

TEST(InitializeLogTest, ConcurrentCallsAreSafe) {
  // Spawn several threads that race to initialize the logging library. Without
  // the call_once guard inside InitializeLog(), this would either crash via
  // ABSL_RAW_LOG(FATAL) inside SetTimeZone() or expose a data race on the
  // initialization globals. With the guard, exactly one underlying
  // initialization runs and the rest become no-ops.
  constexpr int kNumThreads = 8;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([] { absl::InitializeLog(); });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_TRUE(absl::log_internal::IsInitialized());
}

}  // namespace
