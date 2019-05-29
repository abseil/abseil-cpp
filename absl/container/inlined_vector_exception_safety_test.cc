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

#include <memory>

#include "gtest/gtest.h"
#include "absl/base/internal/exception_safety_testing.h"
#include "absl/container/inlined_vector.h"

namespace {

constexpr size_t kInlined = 4;
constexpr size_t kSmallSize = kInlined / 2;
constexpr size_t kLargeSize = kInlined * 2;

using Thrower = testing::ThrowingValue<>;
using ThrowerAlloc = testing::ThrowingAllocator<Thrower>;

template <typename Allocator = std::allocator<Thrower>>
using InlVec = absl::InlinedVector<Thrower, kInlined, Allocator>;

TEST(InlinedVector, DefaultConstructor) {
  testing::TestThrowingCtor<InlVec<>>();

  testing::TestThrowingCtor<InlVec<ThrowerAlloc>>();
}

TEST(InlinedVector, AllocConstructor) {
  auto alloc = std::allocator<Thrower>();
  testing::TestThrowingCtor<InlVec<>>(alloc);

  auto throw_alloc = ThrowerAlloc();
  testing::TestThrowingCtor<InlVec<ThrowerAlloc>>(throw_alloc);
}

TEST(InlinedVector, Clear) {
  auto small_vec = InlVec<>(kSmallSize);
  EXPECT_TRUE(testing::TestNothrowOp([&]() { small_vec.clear(); }));

  auto large_vec = InlVec<>(kLargeSize);
  EXPECT_TRUE(testing::TestNothrowOp([&]() { large_vec.clear(); }));
}

}  // namespace
