#include "absl/base/internal/exception_safety_testing.h"

#include <cstddef>
#include <exception>
#include <iostream>
#include <list>
#include <vector>

#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"

namespace absl {
namespace {
using ::absl::exceptions_internal::TestException;

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
  AllocInspector clouseau_;
};

TEST_F(ThrowingValueTest, Throws) {
  SetCountdown();
  EXPECT_THROW(ThrowingValue<> bomb, TestException);

  // It's not guaranteed that every operator only throws *once*.  The default
  // ctor only throws once, though, so use it to make sure we only throw when
  // the countdown hits 0
  exceptions_internal::countdown = 2;
  ExpectNoThrow([]() { ThrowingValue<> bomb; });
  ExpectNoThrow([]() { ThrowingValue<> bomb; });
  EXPECT_THROW(ThrowingValue<> bomb, TestException);
}

// Tests that an operation throws when the countdown is at 0, doesn't throw when
// the countdown doesn't hit 0, and doesn't modify the state of the
// ThrowingValue if it throws
template <typename F>
void TestOp(F&& f) {
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

TEST_F(ThrowingValueTest, ThrowingAllocatingOps) {
  // make_unique calls unqualified operator new, so these exercise the
  // ThrowingValue overloads.
  TestOp([]() { return absl::make_unique<ThrowingValue<>>(1); });
  TestOp([]() { return absl::make_unique<ThrowingValue<>[]>(2); });
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
  AllocInspector borlu_;
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

struct CallOperator {
  template <typename T>
  void operator()(T* t) const {
    (*t)();
  }
};

struct FailsBasicGuarantee {
  void operator()() {
    --i;
    ThrowingValue<> bomb;
    ++i;
  }

  bool operator==(const FailsBasicGuarantee& other) const {
    return i == other.i;
  }

  friend testing::AssertionResult AbslCheckInvariants(
      const FailsBasicGuarantee& g) {
    if (g.i >= 0) return testing::AssertionSuccess();
    return testing::AssertionFailure()
           << "i should be non-negative but is " << g.i;
  }

  int i = 0;
};

TEST(ExceptionCheckTest, BasicGuaranteeFailure) {
  FailsBasicGuarantee g;
  EXPECT_FALSE(TestExceptionSafety(&g, CallOperator{}));
}

struct FollowsBasicGuarantee {
  void operator()() {
    ++i;
    ThrowingValue<> bomb;
  }

  bool operator==(const FollowsBasicGuarantee& other) const {
    return i == other.i;
  }

  friend testing::AssertionResult AbslCheckInvariants(
      const FollowsBasicGuarantee& g) {
    if (g.i >= 0) return testing::AssertionSuccess();
    return testing::AssertionFailure()
           << "i should be non-negative but is " << g.i;
  }

  int i = 0;
};

TEST(ExceptionCheckTest, BasicGuarantee) {
  FollowsBasicGuarantee g;
  EXPECT_TRUE(TestExceptionSafety(&g, CallOperator{}));
}

TEST(ExceptionCheckTest, StrongGuaranteeFailure) {
  {
    FailsBasicGuarantee g;
    EXPECT_FALSE(TestExceptionSafety(&g, CallOperator{}, StrongGuarantee(g)));
  }

  {
    FollowsBasicGuarantee g;
    EXPECT_FALSE(TestExceptionSafety(&g, CallOperator{}, StrongGuarantee(g)));
  }
}

struct BasicGuaranteeWithExtraInvariants {
  // After operator(), i is incremented.  If operator() throws, i is set to 9999
  void operator()() {
    int old_i = i;
    i = kExceptionSentinel;
    ThrowingValue<> bomb;
    i = ++old_i;
  }

  bool operator==(const FollowsBasicGuarantee& other) const {
    return i == other.i;
  }

  friend testing::AssertionResult AbslCheckInvariants(
      const BasicGuaranteeWithExtraInvariants& g) {
    if (g.i >= 0) return testing::AssertionSuccess();
    return testing::AssertionFailure()
           << "i should be non-negative but is " << g.i;
  }

  int i = 0;
  static constexpr int kExceptionSentinel = 9999;
};
constexpr int BasicGuaranteeWithExtraInvariants::kExceptionSentinel;

TEST(ExceptionCheckTest, BasicGuaranteeWithInvariants) {
  {
    BasicGuaranteeWithExtraInvariants g;
    EXPECT_TRUE(TestExceptionSafety(&g, CallOperator{}));
  }

  {
    BasicGuaranteeWithExtraInvariants g;
    EXPECT_TRUE(TestExceptionSafety(
        &g, CallOperator{}, [](const BasicGuaranteeWithExtraInvariants& w) {
          if (w.i == BasicGuaranteeWithExtraInvariants::kExceptionSentinel) {
            return testing::AssertionSuccess();
          }
          return testing::AssertionFailure()
                 << "i should be "
                 << BasicGuaranteeWithExtraInvariants::kExceptionSentinel
                 << ", but is " << w.i;
        }));
  }
}

struct FollowsStrongGuarantee {
  void operator()() { ThrowingValue<> bomb; }

  bool operator==(const FollowsStrongGuarantee& other) const {
    return i == other.i;
  }

  friend testing::AssertionResult AbslCheckInvariants(
      const FollowsStrongGuarantee& g) {
    if (g.i >= 0) return testing::AssertionSuccess();
    return testing::AssertionFailure()
           << "i should be non-negative but is " << g.i;
  }

  int i = 0;
};

TEST(ExceptionCheckTest, StrongGuarantee) {
  FollowsStrongGuarantee g;
  EXPECT_TRUE(TestExceptionSafety(&g, CallOperator{}));
  EXPECT_TRUE(TestExceptionSafety(&g, CallOperator{}, StrongGuarantee(g)));
}

struct NonCopyable {
  NonCopyable(const NonCopyable&) = delete;
  explicit NonCopyable(int ii) : i(ii) {}

  void operator()() { ThrowingValue<> bomb; }

  bool operator==(const NonCopyable& other) const { return i == other.i; }

  friend testing::AssertionResult AbslCheckInvariants(const NonCopyable& g) {
    if (g.i >= 0) return testing::AssertionSuccess();
    return testing::AssertionFailure()
           << "i should be non-negative but is " << g.i;
  }

  int i;
};

TEST(ExceptionCheckTest, NonCopyable) {
  NonCopyable g(0);
  EXPECT_TRUE(TestExceptionSafety(&g, CallOperator{}));
  EXPECT_TRUE(TestExceptionSafety(
      &g, CallOperator{},
      PointeeStrongGuarantee(absl::make_unique<NonCopyable>(g.i))));
}

struct NonEqualityComparable {
  void operator()() { ThrowingValue<> bomb; }

  void ModifyOnThrow() {
    ++i;
    ThrowingValue<> bomb;
    static_cast<void>(bomb);
    --i;
  }

  friend testing::AssertionResult AbslCheckInvariants(
      const NonEqualityComparable& g) {
    if (g.i >= 0) return testing::AssertionSuccess();
    return testing::AssertionFailure()
           << "i should be non-negative but is " << g.i;
  }

  int i = 0;
};

TEST(ExceptionCheckTest, NonEqualityComparable) {
  NonEqualityComparable g;
  auto comp = [](const NonEqualityComparable& a,
                 const NonEqualityComparable& b) { return a.i == b.i; };
  EXPECT_TRUE(TestExceptionSafety(&g, CallOperator{}));
  EXPECT_TRUE(
      TestExceptionSafety(&g, CallOperator{}, absl::StrongGuarantee(g, comp)));
  EXPECT_FALSE(TestExceptionSafety(
      &g, [&](NonEqualityComparable* n) { n->ModifyOnThrow(); },
      absl::StrongGuarantee(g, comp)));
}

template <typename T>
struct InstructionCounter {
  void operator()() {
    ++counter;
    T b1;
    static_cast<void>(b1);
    ++counter;
    T b2;
    static_cast<void>(b2);
    ++counter;
    T b3;
    static_cast<void>(b3);
    ++counter;
  }

  bool operator==(const InstructionCounter<ThrowingValue<>>&) const {
    return true;
  }

  friend testing::AssertionResult AbslCheckInvariants(
      const InstructionCounter&) {
    return testing::AssertionSuccess();
  }

  static int counter;
};
template <typename T>
int InstructionCounter<T>::counter = 0;

TEST(ExceptionCheckTest, Exhaustiveness) {
  InstructionCounter<int> int_factory;
  EXPECT_TRUE(TestExceptionSafety(&int_factory, CallOperator{}));
  EXPECT_EQ(InstructionCounter<int>::counter, 4);

  InstructionCounter<ThrowingValue<>> bomb_factory;
  EXPECT_TRUE(TestExceptionSafety(&bomb_factory, CallOperator{}));
  EXPECT_EQ(InstructionCounter<ThrowingValue<>>::counter, 10);

  InstructionCounter<ThrowingValue<>>::counter = 0;
  EXPECT_TRUE(TestExceptionSafety(&bomb_factory, CallOperator{},
                                  StrongGuarantee(bomb_factory)));
  EXPECT_EQ(InstructionCounter<ThrowingValue<>>::counter, 10);
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

TEST(AllocInspectorTest, Pass) {
  AllocInspector javert;
  Tracked t;
}

TEST(AllocInspectorTest, NotDestroyed) {
  absl::aligned_storage_t<sizeof(Tracked), alignof(Tracked)> storage;
  EXPECT_NONFATAL_FAILURE(
      {
        AllocInspector gadget;
        new (&storage) Tracked;
      },
      "not destroyed");
}

TEST(AllocInspectorTest, DestroyedTwice) {
  EXPECT_NONFATAL_FAILURE(
      {
        Tracked t;
        t.~Tracked();
      },
      "destroyed improperly");
}

TEST(AllocInspectorTest, ConstructedTwice) {
  absl::aligned_storage_t<sizeof(Tracked), alignof(Tracked)> storage;
  EXPECT_NONFATAL_FAILURE(
      {
        new (&storage) Tracked;
        new (&storage) Tracked;
      },
      "re-constructed");
}
}  // namespace
}  // namespace absl
