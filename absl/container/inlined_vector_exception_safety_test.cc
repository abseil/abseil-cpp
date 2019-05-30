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

constexpr size_t kInlinedCapacity = 4;
constexpr size_t kLargeSize = kInlinedCapacity * 2;
constexpr size_t kSmallSize = kInlinedCapacity / 2;

using Thrower = testing::ThrowingValue<>;
using MovableThrower = testing::ThrowingValue<testing::TypeSpec::kNoThrowMove>;
using ThrowAlloc = testing::ThrowingAllocator<Thrower>;

using ThrowerVec = absl::InlinedVector<Thrower, kInlinedCapacity>;
using MovableThrowerVec = absl::InlinedVector<MovableThrower, kInlinedCapacity>;

using ThrowAllocThrowerVec =
    absl::InlinedVector<Thrower, kInlinedCapacity, ThrowAlloc>;
using ThrowAllocMovableThrowerVec =
    absl::InlinedVector<MovableThrower, kInlinedCapacity, ThrowAlloc>;

template <typename TheVecT, size_t... TheSizes>
class TestParams {
 public:
  using VecT = TheVecT;
  constexpr static size_t GetSizeAt(size_t i) { return kSizes[1 + i]; }

 private:
  constexpr static size_t kSizes[1 + sizeof...(TheSizes)] = {1, TheSizes...};
};

using NoSizeTestParams =
    ::testing::Types<TestParams<ThrowerVec>, TestParams<MovableThrowerVec>,
                     TestParams<ThrowAllocThrowerVec>,
                     TestParams<ThrowAllocMovableThrowerVec>>;

using OneSizeTestParams =
    ::testing::Types<TestParams<ThrowerVec, kLargeSize>,
                     TestParams<ThrowerVec, kSmallSize>,
                     TestParams<MovableThrowerVec, kLargeSize>,
                     TestParams<MovableThrowerVec, kSmallSize>,
                     TestParams<ThrowAllocThrowerVec, kLargeSize>,
                     TestParams<ThrowAllocThrowerVec, kSmallSize>,
                     TestParams<ThrowAllocMovableThrowerVec, kLargeSize>,
                     TestParams<ThrowAllocMovableThrowerVec, kSmallSize>>;

template <typename>
struct NoSizeTest : ::testing::Test {};
TYPED_TEST_SUITE(NoSizeTest, NoSizeTestParams);

template <typename>
struct OneSizeTest : ::testing::Test {};
TYPED_TEST_SUITE(OneSizeTest, OneSizeTestParams);

// Function that always returns false is correct, but refactoring is required
// for clarity. It's needed to express that, as a contract, certain operations
// should not throw at all. Execution of this function means an exception was
// thrown and thus the test should fail.
// TODO(johnsoncj): Add `testing::NoThrowGuarantee` to the framework
template <typename VecT>
bool NoThrowGuarantee(VecT* /* vec */) {
  return false;
}

TYPED_TEST(NoSizeTest, DefaultConstructor) {
  using VecT = typename TypeParam::VecT;
  using allocator_type = typename VecT::allocator_type;

  testing::TestThrowingCtor<VecT>();

  testing::TestThrowingCtor<VecT>(allocator_type{});
}

TYPED_TEST(OneSizeTest, Clear) {
  using VecT = typename TypeParam::VecT;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT(size))
                    .WithContracts(NoThrowGuarantee<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    vec->clear();  //
  }));
}

}  // namespace
