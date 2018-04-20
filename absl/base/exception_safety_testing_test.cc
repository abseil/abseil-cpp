// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/internal/exception_safety_testing.h"

#include <cstddef>
#include <exception>
#include <iostream>
#include <list>
#include <type_traits>
#include <vector>

#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"

namespace absl {
namespace {
using ::absl::exceptions_internal::SetCountdown;
using ::absl::exceptions_internal::TestException;
using ::absl::exceptions_internal::UnsetCountdown;

// EXPECT_NO_THROW can't inspect the thrown inspection in general.
template <typename F>
void ExpectNoThrow(const F& f) {
  try {
    f();
  } catch (TestException e) {
    ADD_FAILURE() << "Unexpected exception thrown from " << e.what();
  }
}

class ThrowingValueTest : public ::testing::Test {
 protected:
  void SetUp() override { UnsetCountdown(); }

 private:
  ConstructorTracker clouseau_;
};

TEST_F(ThrowingValueTest, Throws) {
  SetCountdown();
  EXPECT_THROW(ThrowingValue<> bomb, TestException);

  // It's not guaranteed that every operator only throws *once*.  The default
  // ctor only throws once, though, so use it to make sure we only throw when
  // the countdown hits 0
  SetCountdown(2);
  ExpectNoThrow([]() { ThrowingValue<> bomb; });
  ExpectNoThrow([]() { ThrowingValue<> bomb; });
  EXPECT_THROW(ThrowingValue<> bomb, TestException);
}

// Tests that an operation throws when the countdown is at 0, doesn't throw when
// the countdown doesn't hit 0, and doesn't modify the state of the
// ThrowingValue if it throws
template <typename F>
void TestOp(const F& f) {
  UnsetCountdown();
  ExpectNoThrow(f);

  SetCountdown();
  EXPECT_THROW(f(), TestException);
  UnsetCountdown();
}

TEST_F(ThrowingValueTest, ThrowingCtors) {
  ThrowingValue<> bomb;

  TestOp([]() { ThrowingValue<> bomb(1); });
  TestOp([&]() { ThrowingValue<> bomb1 = bomb; });
  TestOp([&]() { ThrowingValue<> bomb1 = std::move(bomb); });
}

TEST_F(ThrowingValueTest, ThrowingAssignment) {
  ThrowingValue<> bomb, bomb1;

  TestOp([&]() { bomb = bomb1; });
  TestOp([&]() { bomb = std::move(bomb1); });
}

TEST_F(ThrowingValueTest, ThrowingComparisons) {
  ThrowingValue<> bomb1, bomb2;
  TestOp([&]() { return bomb1 == bomb2; });
  TestOp([&]() { return bomb1 != bomb2; });
  TestOp([&]() { return bomb1 < bomb2; });
  TestOp([&]() { return bomb1 <= bomb2; });
  TestOp([&]() { return bomb1 > bomb2; });
  TestOp([&]() { return bomb1 >= bomb2; });
}

TEST_F(ThrowingValueTest, ThrowingArithmeticOps) {
  ThrowingValue<> bomb1(1), bomb2(2);

  TestOp([&bomb1]() { +bomb1; });
  TestOp([&bomb1]() { -bomb1; });
  TestOp([&bomb1]() { ++bomb1; });
  TestOp([&bomb1]() { bomb1++; });
  TestOp([&bomb1]() { --bomb1; });
  TestOp([&bomb1]() { bomb1--; });

  TestOp([&]() { bomb1 + bomb2; });
  TestOp([&]() { bomb1 - bomb2; });
  TestOp([&]() { bomb1* bomb2; });
  TestOp([&]() { bomb1 / bomb2; });
  TestOp([&]() { bomb1 << 1; });
  TestOp([&]() { bomb1 >> 1; });
}

TEST_F(ThrowingValueTest, ThrowingLogicalOps) {
  ThrowingValue<> bomb1, bomb2;

  TestOp([&bomb1]() { !bomb1; });
  TestOp([&]() { bomb1&& bomb2; });
  TestOp([&]() { bomb1 || bomb2; });
}

TEST_F(ThrowingValueTest, ThrowingBitwiseOps) {
  ThrowingValue<> bomb1, bomb2;

  TestOp([&bomb1]() { ~bomb1; });
  TestOp([&]() { bomb1& bomb2; });
  TestOp([&]() { bomb1 | bomb2; });
  TestOp([&]() { bomb1 ^ bomb2; });
}

TEST_F(ThrowingValueTest, ThrowingCompoundAssignmentOps) {
  ThrowingValue<> bomb1(1), bomb2(2);

  TestOp([&]() { bomb1 += bomb2; });
  TestOp([&]() { bomb1 -= bomb2; });
  TestOp([&]() { bomb1 *= bomb2; });
  TestOp([&]() { bomb1 /= bomb2; });
  TestOp([&]() { bomb1 %= bomb2; });
  TestOp([&]() { bomb1 &= bomb2; });
  TestOp([&]() { bomb1 |= bomb2; });
  TestOp([&]() { bomb1 ^= bomb2; });
  TestOp([&]() { bomb1 *= bomb2; });
}

TEST_F(ThrowingValueTest, ThrowingStreamOps) {
  ThrowingValue<> bomb;

  TestOp([&]() { std::cin >> bomb; });
  TestOp([&]() { std::cout << bomb; });
}

template <typename F>
void TestAllocatingOp(const F& f) {
  UnsetCountdown();
  ExpectNoThrow(f);

  SetCountdown();
  EXPECT_THROW(f(), exceptions_internal::TestBadAllocException);
  UnsetCountdown();
}

TEST_F(ThrowingValueTest, ThrowingAllocatingOps) {
  // make_unique calls unqualified operator new, so these exercise the
  // ThrowingValue overloads.
  TestAllocatingOp([]() { return absl::make_unique<ThrowingValue<>>(1); });
  TestAllocatingOp([]() { return absl::make_unique<ThrowingValue<>[]>(2); });
}

TEST_F(ThrowingValueTest, NonThrowingMoveCtor) {
  ThrowingValue<NoThrow::kMoveCtor> nothrow_ctor;

  SetCountdown();
  ExpectNoThrow([&nothrow_ctor]() {
    ThrowingValue<NoThrow::kMoveCtor> nothrow1 = std::move(nothrow_ctor);
  });
}

TEST_F(ThrowingValueTest, NonThrowingMoveAssign) {
  ThrowingValue<NoThrow::kMoveAssign> nothrow_assign1, nothrow_assign2;

  SetCountdown();
  ExpectNoThrow([&nothrow_assign1, &nothrow_assign2]() {
    nothrow_assign1 = std::move(nothrow_assign2);
  });
}

TEST_F(ThrowingValueTest, ThrowingSwap) {
  ThrowingValue<> bomb1, bomb2;
  TestOp([&]() { std::swap(bomb1, bomb2); });

  ThrowingValue<NoThrow::kMoveCtor> bomb3, bomb4;
  TestOp([&]() { std::swap(bomb3, bomb4); });

  ThrowingValue<NoThrow::kMoveAssign> bomb5, bomb6;
  TestOp([&]() { std::swap(bomb5, bomb6); });
}

TEST_F(ThrowingValueTest, NonThrowingSwap) {
  ThrowingValue<NoThrow::kMoveAssign | NoThrow::kMoveCtor> bomb1, bomb2;
  ExpectNoThrow([&]() { std::swap(bomb1, bomb2); });
}

TEST_F(ThrowingValueTest, NonThrowingAllocation) {
  ThrowingValue<NoThrow::kAllocation>* allocated;
  ThrowingValue<NoThrow::kAllocation>* array;

  ExpectNoThrow([&allocated]() {
    allocated = new ThrowingValue<NoThrow::kAllocation>(1);
    delete allocated;
  });
  ExpectNoThrow([&array]() {
    array = new ThrowingValue<NoThrow::kAllocation>[2];
    delete[] array;
  });
}

TEST_F(ThrowingValueTest, NonThrowingDelete) {
  auto* allocated = new ThrowingValue<>(1);
  auto* array = new ThrowingValue<>[2];

  SetCountdown();
  ExpectNoThrow([allocated]() { delete allocated; });
  SetCountdown();
  ExpectNoThrow([array]() { delete[] array; });
}

using Storage =
    absl::aligned_storage_t<sizeof(ThrowingValue<>), alignof(ThrowingValue<>)>;

TEST_F(ThrowingValueTest, NonThrowingPlacementDelete) {
  constexpr int kArrayLen = 2;
  // We intentionally create extra space to store the tag allocated by placement
  // new[].
  constexpr int kStorageLen = 4;

  Storage buf;
  Storage array_buf[kStorageLen];
  auto* placed = new (&buf) ThrowingValue<>(1);
  auto placed_array = new (&array_buf) ThrowingValue<>[kArrayLen];

  SetCountdown();
  ExpectNoThrow([placed, &buf]() {
    placed->~ThrowingValue<>();
    ThrowingValue<>::operator delete(placed, &buf);
  });

  SetCountdown();
  ExpectNoThrow([&, placed_array]() {
    for (int i = 0; i < kArrayLen; ++i) placed_array[i].~ThrowingValue<>();
    ThrowingValue<>::operator delete[](placed_array, &array_buf);
  });
}

TEST_F(ThrowingValueTest, NonThrowingDestructor) {
  auto* allocated = new ThrowingValue<>();
  SetCountdown();
  ExpectNoThrow([allocated]() { delete allocated; });
}

TEST(ThrowingBoolTest, ThrowingBool) {
  UnsetCountdown();
  ThrowingBool t = true;

  // Test that it's contextually convertible to bool
  if (t) {  // NOLINT(whitespace/empty_if_body)
  }
  EXPECT_TRUE(t);

  TestOp([&]() { (void)!t; });
}

class ThrowingAllocatorTest : public ::testing::Test {
 protected:
  void SetUp() override { UnsetCountdown(); }

 private:
  ConstructorTracker borlu_;
};

TEST_F(ThrowingAllocatorTest, MemoryManagement) {
  // Just exercise the memory management capabilities under LSan to make sure we
  // don't leak.
  ThrowingAllocator<int> int_alloc;
  int* ip = int_alloc.allocate(1);
  int_alloc.deallocate(ip, 1);
  int* i_array = int_alloc.allocate(2);
  int_alloc.deallocate(i_array, 2);

  ThrowingAllocator<ThrowingValue<>> ef_alloc;
  ThrowingValue<>* efp = ef_alloc.allocate(1);
  ef_alloc.deallocate(efp, 1);
  ThrowingValue<>* ef_array = ef_alloc.allocate(2);
  ef_alloc.deallocate(ef_array, 2);
}

TEST_F(ThrowingAllocatorTest, CallsGlobalNew) {
  ThrowingAllocator<ThrowingValue<>, NoThrow::kNoThrow> nothrow_alloc;
  ThrowingValue<>* ptr;

  SetCountdown();
  // This will only throw if ThrowingValue::new is called.
  ExpectNoThrow([&]() { ptr = nothrow_alloc.allocate(1); });
  nothrow_alloc.deallocate(ptr, 1);
}

TEST_F(ThrowingAllocatorTest, ThrowingConstructors) {
  ThrowingAllocator<int> int_alloc;
  int* ip = nullptr;

  SetCountdown();
  EXPECT_THROW(ip = int_alloc.allocate(1), TestException);
  ExpectNoThrow([&]() { ip = int_alloc.allocate(1); });

  *ip = 1;
  SetCountdown();
  EXPECT_THROW(int_alloc.construct(ip, 2), TestException);
  EXPECT_EQ(*ip, 1);
  int_alloc.deallocate(ip, 1);
}

TEST_F(ThrowingAllocatorTest, NonThrowingConstruction) {
  {
    ThrowingAllocator<int, NoThrow::kNoThrow> int_alloc;
    int* ip = nullptr;

    SetCountdown();
    ExpectNoThrow([&]() { ip = int_alloc.allocate(1); });
    SetCountdown();
    ExpectNoThrow([&]() { int_alloc.construct(ip, 2); });
    EXPECT_EQ(*ip, 2);
    int_alloc.deallocate(ip, 1);
  }

  UnsetCountdown();
  {
    ThrowingAllocator<int> int_alloc;
    int* ip = nullptr;
    ExpectNoThrow([&]() { ip = int_alloc.allocate(1); });
    ExpectNoThrow([&]() { int_alloc.construct(ip, 2); });
    EXPECT_EQ(*ip, 2);
    int_alloc.deallocate(ip, 1);
  }

  UnsetCountdown();
  {
    ThrowingAllocator<ThrowingValue<NoThrow::kIntCtor>, NoThrow::kNoThrow>
        ef_alloc;
    ThrowingValue<NoThrow::kIntCtor>* efp;
    SetCountdown();
    ExpectNoThrow([&]() { efp = ef_alloc.allocate(1); });
    SetCountdown();
    ExpectNoThrow([&]() { ef_alloc.construct(efp, 2); });
    EXPECT_EQ(efp->Get(), 2);
    ef_alloc.destroy(efp);
    ef_alloc.deallocate(efp, 1);
  }

  UnsetCountdown();
  {
    ThrowingAllocator<int> a;
    SetCountdown();
    ExpectNoThrow([&]() { ThrowingAllocator<double> a1 = a; });
    SetCountdown();
    ExpectNoThrow([&]() { ThrowingAllocator<double> a1 = std::move(a); });
  }
}

TEST_F(ThrowingAllocatorTest, ThrowingAllocatorConstruction) {
  ThrowingAllocator<int> a;
  TestOp([]() { ThrowingAllocator<int> a; });
  TestOp([&]() { a.select_on_container_copy_construction(); });
}

TEST_F(ThrowingAllocatorTest, State) {
  ThrowingAllocator<int> a1, a2;
  EXPECT_NE(a1, a2);

  auto a3 = a1;
  EXPECT_EQ(a3, a1);
  int* ip = a1.allocate(1);
  EXPECT_EQ(a3, a1);
  a3.deallocate(ip, 1);
  EXPECT_EQ(a3, a1);
}

TEST_F(ThrowingAllocatorTest, InVector) {
  std::vector<ThrowingValue<>, ThrowingAllocator<ThrowingValue<>>> v;
  for (int i = 0; i < 20; ++i) v.push_back({});
  for (int i = 0; i < 20; ++i) v.pop_back();
}

TEST_F(ThrowingAllocatorTest, InList) {
  std::list<ThrowingValue<>, ThrowingAllocator<ThrowingValue<>>> l;
  for (int i = 0; i < 20; ++i) l.push_back({});
  for (int i = 0; i < 20; ++i) l.pop_back();
  for (int i = 0; i < 20; ++i) l.push_front({});
  for (int i = 0; i < 20; ++i) l.pop_front();
}

template <typename TesterInstance, typename = void>
struct NullaryTestValidator : public std::false_type {};

template <typename TesterInstance>
struct NullaryTestValidator<
    TesterInstance,
    absl::void_t<decltype(std::declval<TesterInstance>().Test())>>
    : public std::true_type {};

template <typename TesterInstance>
bool HasNullaryTest(const TesterInstance&) {
  return NullaryTestValidator<TesterInstance>::value;
}

void DummyOp(void*) {}

template <typename TesterInstance, typename = void>
struct UnaryTestValidator : public std::false_type {};

template <typename TesterInstance>
struct UnaryTestValidator<
    TesterInstance,
    absl::void_t<decltype(std::declval<TesterInstance>().Test(DummyOp))>>
    : public std::true_type {};

template <typename TesterInstance>
bool HasUnaryTest(const TesterInstance&) {
  return UnaryTestValidator<TesterInstance>::value;
}

TEST(ExceptionSafetyTesterTest, IncompleteTypesAreNotTestable) {
  using T = exceptions_internal::UninitializedT;
  auto op = [](T* t) {};
  auto inv = [](T*) { return testing::AssertionSuccess(); };
  auto fac = []() { return absl::make_unique<T>(); };

  // Test that providing operation and inveriants still does not allow for the
  // the invocation of .Test() and .Test(op) because it lacks a factory
  auto without_fac =
      absl::MakeExceptionSafetyTester().WithOperation(op).WithInvariants(
          inv, absl::strong_guarantee);
  EXPECT_FALSE(HasNullaryTest(without_fac));
  EXPECT_FALSE(HasUnaryTest(without_fac));

  // Test that providing invariants and factory allows the invocation of
  // .Test(op) but does not allow for .Test() because it lacks an operation
  auto without_op = absl::MakeExceptionSafetyTester()
                        .WithInvariants(inv, absl::strong_guarantee)
                        .WithFactory(fac);
  EXPECT_FALSE(HasNullaryTest(without_op));
  EXPECT_TRUE(HasUnaryTest(without_op));

  // Test that providing operation and factory still does not allow for the
  // the invocation of .Test() and .Test(op) because it lacks invariants
  auto without_inv =
      absl::MakeExceptionSafetyTester().WithOperation(op).WithFactory(fac);
  EXPECT_FALSE(HasNullaryTest(without_inv));
  EXPECT_FALSE(HasUnaryTest(without_inv));
}

struct ExampleStruct {};

std::unique_ptr<ExampleStruct> ExampleFunctionFactory() {
  return absl::make_unique<ExampleStruct>();
}

void ExampleFunctionOperation(ExampleStruct*) {}

testing::AssertionResult ExampleFunctionInvariant(ExampleStruct*) {
  return testing::AssertionSuccess();
}

struct {
  std::unique_ptr<ExampleStruct> operator()() const {
    return ExampleFunctionFactory();
  }
} example_struct_factory;

struct {
  void operator()(ExampleStruct*) const {}
} example_struct_operation;

struct {
  testing::AssertionResult operator()(ExampleStruct* example_struct) const {
    return ExampleFunctionInvariant(example_struct);
  }
} example_struct_invariant;

auto example_lambda_factory = []() { return ExampleFunctionFactory(); };

auto example_lambda_operation = [](ExampleStruct*) {};

auto example_lambda_invariant = [](ExampleStruct* example_struct) {
  return ExampleFunctionInvariant(example_struct);
};

// Testing that function references, pointers, structs with operator() and
// lambdas can all be used with ExceptionSafetyTester
TEST(ExceptionSafetyTesterTest, MixedFunctionTypes) {
  // function reference
  EXPECT_TRUE(absl::MakeExceptionSafetyTester()
                  .WithFactory(ExampleFunctionFactory)
                  .WithOperation(ExampleFunctionOperation)
                  .WithInvariants(ExampleFunctionInvariant)
                  .Test());

  // function pointer
  EXPECT_TRUE(absl::MakeExceptionSafetyTester()
                  .WithFactory(&ExampleFunctionFactory)
                  .WithOperation(&ExampleFunctionOperation)
                  .WithInvariants(&ExampleFunctionInvariant)
                  .Test());

  // struct
  EXPECT_TRUE(absl::MakeExceptionSafetyTester()
                  .WithFactory(example_struct_factory)
                  .WithOperation(example_struct_operation)
                  .WithInvariants(example_struct_invariant)
                  .Test());

  // lambda
  EXPECT_TRUE(absl::MakeExceptionSafetyTester()
                  .WithFactory(example_lambda_factory)
                  .WithOperation(example_lambda_operation)
                  .WithInvariants(example_lambda_invariant)
                  .Test());
}

struct NonNegative {
  bool operator==(const NonNegative& other) const { return i == other.i; }
  int i;
};

testing::AssertionResult CheckNonNegativeInvariants(NonNegative* g) {
  if (g->i >= 0) {
    return testing::AssertionSuccess();
  }
  return testing::AssertionFailure()
         << "i should be non-negative but is " << g->i;
}

struct {
  template <typename T>
  void operator()(T* t) const {
    (*t)();
  }
} invoker;

auto tester =
    absl::MakeExceptionSafetyTester().WithOperation(invoker).WithInvariants(
        CheckNonNegativeInvariants);
auto strong_tester = tester.WithInvariants(absl::strong_guarantee);

struct FailsBasicGuarantee : public NonNegative {
  void operator()() {
    --i;
    ThrowingValue<> bomb;
    ++i;
  }
};

TEST(ExceptionCheckTest, BasicGuaranteeFailure) {
  EXPECT_FALSE(tester.WithInitialValue(FailsBasicGuarantee{}).Test());
}

struct FollowsBasicGuarantee : public NonNegative {
  void operator()() {
    ++i;
    ThrowingValue<> bomb;
  }
};

TEST(ExceptionCheckTest, BasicGuarantee) {
  EXPECT_TRUE(tester.WithInitialValue(FollowsBasicGuarantee{}).Test());
}

TEST(ExceptionCheckTest, StrongGuaranteeFailure) {
  EXPECT_FALSE(strong_tester.WithInitialValue(FailsBasicGuarantee{}).Test());
  EXPECT_FALSE(strong_tester.WithInitialValue(FollowsBasicGuarantee{}).Test());
}

struct BasicGuaranteeWithExtraInvariants : public NonNegative {
  // After operator(), i is incremented.  If operator() throws, i is set to 9999
  void operator()() {
    int old_i = i;
    i = kExceptionSentinel;
    ThrowingValue<> bomb;
    i = ++old_i;
  }

  static constexpr int kExceptionSentinel = 9999;
};
constexpr int BasicGuaranteeWithExtraInvariants::kExceptionSentinel;

TEST(ExceptionCheckTest, BasicGuaranteeWithInvariants) {
  auto tester_with_val =
      tester.WithInitialValue(BasicGuaranteeWithExtraInvariants{});
  EXPECT_TRUE(tester_with_val.Test());
  EXPECT_TRUE(
      tester_with_val
          .WithInvariants([](BasicGuaranteeWithExtraInvariants* w) {
            if (w->i == BasicGuaranteeWithExtraInvariants::kExceptionSentinel) {
              return testing::AssertionSuccess();
            }
            return testing::AssertionFailure()
                   << "i should be "
                   << BasicGuaranteeWithExtraInvariants::kExceptionSentinel
                   << ", but is " << w->i;
          })
          .Test());
}

struct FollowsStrongGuarantee : public NonNegative {
  void operator()() { ThrowingValue<> bomb; }
};

TEST(ExceptionCheckTest, StrongGuarantee) {
  EXPECT_TRUE(tester.WithInitialValue(FollowsStrongGuarantee{}).Test());
  EXPECT_TRUE(strong_tester.WithInitialValue(FollowsStrongGuarantee{}).Test());
}

struct HasReset : public NonNegative {
  void operator()() {
    i = -1;
    ThrowingValue<> bomb;
    i = 1;
  }

  void reset() { i = 0; }
};

testing::AssertionResult CheckHasResetInvariants(HasReset* h) {
  h->reset();
  return testing::AssertionResult(h->i == 0);
}

TEST(ExceptionCheckTest, ModifyingChecker) {
  auto set_to_1000 = [](FollowsBasicGuarantee* g) {
    g->i = 1000;
    return testing::AssertionSuccess();
  };
  auto is_1000 = [](FollowsBasicGuarantee* g) {
    return testing::AssertionResult(g->i == 1000);
  };
  auto increment = [](FollowsStrongGuarantee* g) {
    ++g->i;
    return testing::AssertionSuccess();
  };

  EXPECT_FALSE(tester.WithInitialValue(FollowsBasicGuarantee{})
                   .WithInvariants(set_to_1000, is_1000)
                   .Test());
  EXPECT_TRUE(strong_tester.WithInitialValue(FollowsStrongGuarantee{})
                  .WithInvariants(increment)
                  .Test());
  EXPECT_TRUE(absl::MakeExceptionSafetyTester()
                  .WithInitialValue(HasReset{})
                  .WithInvariants(CheckHasResetInvariants)
                  .Test(invoker));
}

struct NonCopyable : public NonNegative {
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable() : NonNegative{0} {}

  void operator()() { ThrowingValue<> bomb; }
};

TEST(ExceptionCheckTest, NonCopyable) {
  auto factory = []() { return absl::make_unique<NonCopyable>(); };
  EXPECT_TRUE(tester.WithFactory(factory).Test());
  EXPECT_TRUE(strong_tester.WithFactory(factory).Test());
}

struct NonEqualityComparable : public NonNegative {
  void operator()() { ThrowingValue<> bomb; }

  void ModifyOnThrow() {
    ++i;
    ThrowingValue<> bomb;
    static_cast<void>(bomb);
    --i;
  }
};

TEST(ExceptionCheckTest, NonEqualityComparable) {
  auto nec_is_strong = [](NonEqualityComparable* nec) {
    return testing::AssertionResult(nec->i == NonEqualityComparable().i);
  };
  auto strong_nec_tester = tester.WithInitialValue(NonEqualityComparable{})
                               .WithInvariants(nec_is_strong);

  EXPECT_TRUE(strong_nec_tester.Test());
  EXPECT_FALSE(strong_nec_tester.Test(
      [](NonEqualityComparable* n) { n->ModifyOnThrow(); }));
}

template <typename T>
struct ExhaustivenessTester {
  void operator()() {
    successes |= 1;
    T b1;
    static_cast<void>(b1);
    successes |= (1 << 1);
    T b2;
    static_cast<void>(b2);
    successes |= (1 << 2);
    T b3;
    static_cast<void>(b3);
    successes |= (1 << 3);
  }

  bool operator==(const ExhaustivenessTester<ThrowingValue<>>&) const {
    return true;
  }

  static unsigned char successes;
};

struct {
  template <typename T>
  testing::AssertionResult operator()(ExhaustivenessTester<T>*) const {
    return testing::AssertionSuccess();
  }
} CheckExhaustivenessTesterInvariants;

template <typename T>
unsigned char ExhaustivenessTester<T>::successes = 0;

TEST(ExceptionCheckTest, Exhaustiveness) {
  auto exhaust_tester = absl::MakeExceptionSafetyTester()
                            .WithInvariants(CheckExhaustivenessTesterInvariants)
                            .WithOperation(invoker);

  EXPECT_TRUE(
      exhaust_tester.WithInitialValue(ExhaustivenessTester<int>{}).Test());
  EXPECT_EQ(ExhaustivenessTester<int>::successes, 0xF);

  EXPECT_TRUE(
      exhaust_tester.WithInitialValue(ExhaustivenessTester<ThrowingValue<>>{})
          .WithInvariants(absl::strong_guarantee)
          .Test());
  EXPECT_EQ(ExhaustivenessTester<ThrowingValue<>>::successes, 0xF);
}

struct LeaksIfCtorThrows : private exceptions_internal::TrackedObject {
  LeaksIfCtorThrows() : TrackedObject(ABSL_PRETTY_FUNCTION) {
    ++counter;
    ThrowingValue<> v;
    static_cast<void>(v);
    --counter;
  }
  LeaksIfCtorThrows(const LeaksIfCtorThrows&) noexcept
      : TrackedObject(ABSL_PRETTY_FUNCTION) {}
  static int counter;
};
int LeaksIfCtorThrows::counter = 0;

TEST(ExceptionCheckTest, TestLeakyCtor) {
  absl::TestThrowingCtor<LeaksIfCtorThrows>();
  EXPECT_EQ(LeaksIfCtorThrows::counter, 1);
  LeaksIfCtorThrows::counter = 0;
}

struct Tracked : private exceptions_internal::TrackedObject {
  Tracked() : TrackedObject(ABSL_PRETTY_FUNCTION) {}
};

TEST(ConstructorTrackerTest, Pass) {
  ConstructorTracker javert;
  Tracked t;
}

TEST(ConstructorTrackerTest, NotDestroyed) {
  absl::aligned_storage_t<sizeof(Tracked), alignof(Tracked)> storage;
  EXPECT_NONFATAL_FAILURE(
      {
        ConstructorTracker gadget;
        new (&storage) Tracked;
      },
      "not destroyed");
}

TEST(ConstructorTrackerTest, DestroyedTwice) {
  EXPECT_NONFATAL_FAILURE(
      {
        Tracked t;
        t.~Tracked();
      },
      "destroyed improperly");
}

TEST(ConstructorTrackerTest, ConstructedTwice) {
  absl::aligned_storage_t<sizeof(Tracked), alignof(Tracked)> storage;
  EXPECT_NONFATAL_FAILURE(
      {
        new (&storage) Tracked;
        new (&storage) Tracked;
      },
      "re-constructed");
  reinterpret_cast<Tracked*>(&storage)->~Tracked();
}

TEST(ThrowingValueTraitsTest, RelationalOperators) {
  ThrowingValue<> a, b;
  EXPECT_TRUE((std::is_convertible<decltype(a == b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a != b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a < b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a <= b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a > b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a >= b), bool>::value));
}

TEST(ThrowingAllocatorTraitsTest, Assignablility) {
  EXPECT_TRUE(std::is_move_assignable<ThrowingAllocator<int>>::value);
  EXPECT_TRUE(std::is_copy_assignable<ThrowingAllocator<int>>::value);
  EXPECT_TRUE(std::is_nothrow_move_assignable<ThrowingAllocator<int>>::value);
  EXPECT_TRUE(std::is_nothrow_copy_assignable<ThrowingAllocator<int>>::value);
}

}  // namespace
}  // namespace absl
