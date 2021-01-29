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

template <typename Type1, typename Type2>
void AssertSameType() {
  static_assert(std::is_same<Type1, Type2>::value, "");
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
      : callback_(absl::exchange(other.callback_, Callback())) {}

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

bool function_pointer_called = false;
void FunctionPointerFunction() { function_pointer_called = true; }

TYPED_TEST(CleanupTest, FactoryProducesCorrectType) {
  {
    auto callback = TypeParam::AsCallback([] {});
    auto cleanup = absl::MakeCleanup(std::move(callback));

    AssertSameType<absl::Cleanup<Tag, decltype(callback)>, decltype(cleanup)>();
  }

  {
    auto cleanup = absl::MakeCleanup(&FunctionPointerFunction);

    AssertSameType<absl::Cleanup<Tag, void (*)()>, decltype(cleanup)>();
  }

  {
    auto cleanup = absl::MakeCleanup(FunctionPointerFunction);

    AssertSameType<absl::Cleanup<Tag, void (*)()>, decltype(cleanup)>();
  }
}

#if defined(ABSL_HAVE_CLASS_TEMPLATE_ARGUMENT_DEDUCTION)
TYPED_TEST(CleanupTest, CTADProducesCorrectType) {
  {
    auto callback = TypeParam::AsCallback([] {});
    absl::Cleanup cleanup = std::move(callback);

    AssertSameType<absl::Cleanup<Tag, decltype(callback)>, decltype(cleanup)>();
  }

  {
    absl::Cleanup cleanup = &FunctionPointerFunction;

    AssertSameType<absl::Cleanup<Tag, void (*)()>, decltype(cleanup)>();
  }

  {
    absl::Cleanup cleanup = FunctionPointerFunction;

    AssertSameType<absl::Cleanup<Tag, void (*)()>, decltype(cleanup)>();
  }
}

TYPED_TEST(CleanupTest, FactoryAndCTADProduceSameType) {
  {
    auto callback = IdentityFactory::AsCallback([] {});
    auto factory_cleanup = absl::MakeCleanup(callback);
    absl::Cleanup deduction_cleanup = callback;

    AssertSameType<decltype(factory_cleanup), decltype(deduction_cleanup)>();
  }

  {
    auto factory_cleanup =
        absl::MakeCleanup(FunctorClassFactory::AsCallback([] {}));
    absl::Cleanup deduction_cleanup = FunctorClassFactory::AsCallback([] {});

    AssertSameType<decltype(factory_cleanup), decltype(deduction_cleanup)>();
  }

  {
    auto factory_cleanup =
        absl::MakeCleanup(StdFunctionFactory::AsCallback([] {}));
    absl::Cleanup deduction_cleanup = StdFunctionFactory::AsCallback([] {});

    AssertSameType<decltype(factory_cleanup), decltype(deduction_cleanup)>();
  }

  {
    auto factory_cleanup = absl::MakeCleanup(&FunctionPointerFunction);
    absl::Cleanup deduction_cleanup = &FunctionPointerFunction;

    AssertSameType<decltype(factory_cleanup), decltype(deduction_cleanup)>();
  }

  {
    auto factory_cleanup = absl::MakeCleanup(FunctionPointerFunction);
    absl::Cleanup deduction_cleanup = FunctionPointerFunction;

    AssertSameType<decltype(factory_cleanup), decltype(deduction_cleanup)>();
  }
}
#endif  // defined(ABSL_HAVE_CLASS_TEMPLATE_ARGUMENT_DEDUCTION)

TYPED_TEST(CleanupTest, BasicUsage) {
  bool called = false;

  {
    EXPECT_FALSE(called);

    auto cleanup =
        absl::MakeCleanup(TypeParam::AsCallback([&called] { called = true; }));

    EXPECT_FALSE(called);
  }

  EXPECT_TRUE(called);
}

TYPED_TEST(CleanupTest, BasicUsageWithFunctionPointer) {
  function_pointer_called = false;

  {
    EXPECT_FALSE(function_pointer_called);

    auto cleanup =
        absl::MakeCleanup(TypeParam::AsCallback(&FunctionPointerFunction));

    EXPECT_FALSE(function_pointer_called);
  }

  EXPECT_TRUE(function_pointer_called);
}

TYPED_TEST(CleanupTest, Cancel) {
  bool called = false;

  {
    EXPECT_FALSE(called);

    auto cleanup =
        absl::MakeCleanup(TypeParam::AsCallback([&called] { called = true; }));
    std::move(cleanup).Cancel();

    EXPECT_FALSE(called);
  }

  EXPECT_FALSE(called);
}

TYPED_TEST(CleanupTest, Invoke) {
  bool called = false;

  {
    EXPECT_FALSE(called);

    auto cleanup =
        absl::MakeCleanup(TypeParam::AsCallback([&called] { called = true; }));
    std::move(cleanup).Invoke();

    EXPECT_TRUE(called);
  }

  EXPECT_TRUE(called);
}

}  // namespace
