// Copyright 2021 The Abseil Authors.
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

#include "absl/cleanup/cleanup.h"

#include <functional>
#include <type_traits>
#include <utility>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/utility/utility.h"

namespace {

using Tag = absl::cleanup_internal::Tag;

template <typename ExpectedType, typename ActualType>
constexpr bool IsSameType(const ActualType&) {
  return (std::is_same<ExpectedType, ActualType>::value);
}

struct IdentityFactory {
  template <typename Callback>
  static Callback AsCallback(Callback callback) {
    return Callback(std::move(callback));
  }
};

// `FunctorClass` is a type used for testing `absl::Cleanup`. It is intended to
// represent users that make their own move-only callback types outside of
// `std::function` and lambda literals.
class FunctorClass {
  using Callback = std::function<void()>;

 public:
  explicit FunctorClass(Callback callback) : callback_(std::move(callback)) {}

  FunctorClass(FunctorClass&& other)
      : callback_(std::exchange(other.callback_, Callback())) {}

  FunctorClass(const FunctorClass&) = delete;

  FunctorClass& operator=(const FunctorClass&) = delete;

  FunctorClass& operator=(FunctorClass&&) = delete;

  void operator()() const& = delete;

  void operator()() && {
    ASSERT_TRUE(callback_);
    callback_();
    callback_ = nullptr;
  }

 private:
  Callback callback_;
};

struct FunctorClassFactory {
  template <typename Callback>
  static FunctorClass AsCallback(Callback callback) {
    return FunctorClass(std::move(callback));
  }
};

struct StdFunctionFactory {
  template <typename Callback>
  static std::function<void()> AsCallback(Callback callback) {
    return std::function<void()>(std::move(callback));
  }
};

using CleanupTestParams =
    ::testing::Types<IdentityFactory, FunctorClassFactory, StdFunctionFactory>;
template <typename>
struct CleanupTest : public ::testing::Test {};
TYPED_TEST_SUITE(CleanupTest, CleanupTestParams);

bool fn_ptr_called = false;
void FnPtrFunction() { fn_ptr_called = true; }

TYPED_TEST(CleanupTest, CTADProducesCorrectType) {
  {
    auto callback = TypeParam::AsCallback([] {});

    using Expected = absl::Cleanup<Tag, decltype(callback)>;
    absl::Cleanup actual = std::move(callback);

    static_assert(IsSameType<Expected>(actual));
  }

  {
    using Expected = absl::Cleanup<Tag, void (*)()>;
    absl::Cleanup actual = &FnPtrFunction;

    static_assert(IsSameType<Expected>(actual));
  }

  {
    using Expected = absl::Cleanup<Tag, void (*)()>;
    absl::Cleanup actual = FnPtrFunction;

    static_assert(IsSameType<Expected>(actual));
  }
}

TYPED_TEST(CleanupTest, BasicUsage) {
  bool called = false;

  {
    absl::Cleanup cleanup = TypeParam::AsCallback([&called] { called = true; });
    EXPECT_FALSE(called);  // Constructor shouldn't invoke the callback
  }

  EXPECT_TRUE(called);  // Destructor should invoke the callback
}

TYPED_TEST(CleanupTest, BasicUsageWithFunctionPointer) {
  fn_ptr_called = false;

  {
    absl::Cleanup cleanup = TypeParam::AsCallback(&FnPtrFunction);
    EXPECT_FALSE(fn_ptr_called);  // Constructor shouldn't invoke the callback
  }

  EXPECT_TRUE(fn_ptr_called);  // Destructor should invoke the callback
}

TYPED_TEST(CleanupTest, Cancel) {
  bool called = false;

  {
    absl::Cleanup cleanup = TypeParam::AsCallback([&called] { called = true; });
    EXPECT_FALSE(called);  // Constructor shouldn't invoke the callback

    std::move(cleanup).Cancel();
    EXPECT_FALSE(called);  // Cancel shouldn't invoke the callback
  }

  EXPECT_FALSE(called);  // Destructor shouldn't invoke the callback
}

TYPED_TEST(CleanupTest, Invoke) {
  bool called = false;

  {
    absl::Cleanup cleanup = TypeParam::AsCallback([&called] { called = true; });
    EXPECT_FALSE(called);  // Constructor shouldn't invoke the callback

    std::move(cleanup).Invoke();
    EXPECT_TRUE(called);  // Invoke should invoke the callback

    called = false;  // Reset tracker before destructor runs
  }

  EXPECT_FALSE(called);  // Destructor shouldn't invoke the callback
}

TYPED_TEST(CleanupTest, Move) {
  bool called = false;

  {
    absl::Cleanup moved_from_cleanup =
      TypeParam::AsCallback([&called] { called = true; });
    EXPECT_FALSE(called);  // Constructor shouldn't invoke the callback

    {
      absl::Cleanup moved_to_cleanup = std::move(moved_from_cleanup);
      EXPECT_FALSE(called);  // Move shouldn't invoke the callback
    }

    EXPECT_TRUE(called);  // Destructor should invoke the callback

    called = false;  // Reset tracker before destructor runs
  }

  EXPECT_FALSE(called);  // Destructor shouldn't invoke the callback
}

int destruction_count = 0;

struct DestructionCounter {
  void operator()() {}

  ~DestructionCounter() { ++destruction_count; }
};

TYPED_TEST(CleanupTest, DestructorDestroys) {
  {
    absl::Cleanup cleanup = TypeParam::AsCallback(DestructionCounter());
    destruction_count = 0;
  }

  EXPECT_EQ(destruction_count, 1);  // Engaged cleanup destroys
}

TYPED_TEST(CleanupTest, CancelDestroys) {
  {
    absl::Cleanup cleanup = TypeParam::AsCallback(DestructionCounter());
    destruction_count = 0;

    std::move(cleanup).Cancel();
    EXPECT_EQ(destruction_count, 1);  // Cancel destroys
  }

  EXPECT_EQ(destruction_count, 1);  // Canceled cleanup does not double destroy
}

TYPED_TEST(CleanupTest, InvokeDestroys) {
  {
    absl::Cleanup cleanup = TypeParam::AsCallback(DestructionCounter());
    destruction_count = 0;

    std::move(cleanup).Invoke();
    EXPECT_EQ(destruction_count, 1);  // Invoke destroys
  }

  EXPECT_EQ(destruction_count, 1);  // Invoked cleanup does not double destroy
}

}  // namespace
