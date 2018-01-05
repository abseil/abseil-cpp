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

// Utilities for testing exception-safety

#ifndef ABSL_BASE_INTERNAL_EXCEPTION_SAFETY_TESTING_H_
#define ABSL_BASE_INTERNAL_EXCEPTION_SAFETY_TESTING_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iosfwd>
#include <string>
#include <unordered_map>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/internal/pretty_function.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/optional.h"

namespace absl {
struct InternalAbslNamespaceFinder {};

struct AllocInspector;

// A configuration enum for Throwing*.  Operations whose flags are set will
// throw, everything else won't.  This isn't meant to be exhaustive, more flags
// can always be made in the future.
enum class NoThrow : uint8_t {
  kNone = 0,
  kMoveCtor = 1,
  kMoveAssign = 1 << 1,
  kAllocation = 1 << 2,
  kIntCtor = 1 << 3,
  kNoThrow = static_cast<uint8_t>(-1)
};

constexpr NoThrow operator|(NoThrow a, NoThrow b) {
  using T = absl::underlying_type_t<NoThrow>;
  return static_cast<NoThrow>(static_cast<T>(a) | static_cast<T>(b));
}

constexpr NoThrow operator&(NoThrow a, NoThrow b) {
  using T = absl::underlying_type_t<NoThrow>;
  return static_cast<NoThrow>(static_cast<T>(a) & static_cast<T>(b));
}

namespace exceptions_internal {
struct NoThrowTag {};

constexpr bool ThrowingAllowed(NoThrow flags, NoThrow flag) {
  return !static_cast<bool>(flags & flag);
}

// A simple exception class.  We throw this so that test code can catch
// exceptions specifically thrown by ThrowingValue.
class TestException {
 public:
  explicit TestException(absl::string_view msg) : msg_(msg) {}
  virtual ~TestException() {}
  virtual const char* what() const noexcept { return msg_.c_str(); }

 private:
  std::string msg_;
};

// TestBadAllocException exists because allocation functions must throw an
// exception which can be caught by a handler of std::bad_alloc.  We use a child
// class of std::bad_alloc so we can customise the error message, and also
// derive from TestException so we don't accidentally end up catching an actual
// bad_alloc exception in TestExceptionSafety.
class TestBadAllocException : public std::bad_alloc, public TestException {
 public:
  explicit TestBadAllocException(absl::string_view msg)
      : TestException(msg) {}
  using TestException::what;
};

extern int countdown;

void MaybeThrow(absl::string_view msg, bool throw_bad_alloc = false);

testing::AssertionResult FailureMessage(const TestException& e,
                                        int countdown) noexcept;

class TrackedObject {
 public:
  TrackedObject(const TrackedObject&) = delete;
  TrackedObject(TrackedObject&&) = delete;

 protected:
  explicit TrackedObject(const char* child_ctor) {
    if (!GetAllocs().emplace(this, child_ctor).second) {
      ADD_FAILURE() << "Object at address " << static_cast<void*>(this)
                    << " re-constructed in ctor " << child_ctor;
    }
  }

  static std::unordered_map<TrackedObject*, absl::string_view>& GetAllocs() {
    static auto* m =
        new std::unordered_map<TrackedObject*, absl::string_view>();
    return *m;
  }

  ~TrackedObject() noexcept {
    if (GetAllocs().erase(this) == 0) {
      ADD_FAILURE() << "Object at address " << static_cast<void*>(this)
                    << " destroyed improperly";
    }
  }

  friend struct ::absl::AllocInspector;
};

template <typename Factory>
using FactoryType = typename absl::result_of_t<Factory()>::element_type;

// Returns an optional with the result of the check if op fails, or an empty
// optional if op passes
template <typename Factory, typename Op, typename Checker>
absl::optional<testing::AssertionResult> TestCheckerAtCountdown(
    Factory factory, const Op& op, int count, const Checker& check) {
  auto t_ptr = factory();
  absl::optional<testing::AssertionResult> out;
  try {
    exceptions_internal::countdown = count;
    op(t_ptr.get());
  } catch (const exceptions_internal::TestException& e) {
    out.emplace(check(t_ptr.get()));
    if (!*out) {
      *out << " caused by exception thrown by " << e.what();
    }
  }
  return out;
}

template <typename Factory, typename Op, typename Checker>
int UpdateOut(Factory factory, const Op& op, int count, const Checker& checker,
              testing::AssertionResult* out) {
  if (*out) *out = *TestCheckerAtCountdown(factory, op, count, checker);
  return 0;
}

// Declare AbslCheckInvariants so that it can be found eventually via ADL.
// Taking `...` gives it the lowest possible precedence.
void AbslCheckInvariants(...);

// Returns an optional with the result of the check if op fails, or an empty
// optional if op passes
template <typename Factory, typename Op, typename... Checkers>
absl::optional<testing::AssertionResult> TestAtCountdown(
    Factory factory, const Op& op, int count, const Checkers&... checkers) {
  // Don't bother with the checkers if the class invariants are already broken.
  auto out = TestCheckerAtCountdown(
      factory, op, count, [](FactoryType<Factory>* t_ptr) {
        return AbslCheckInvariants(t_ptr, InternalAbslNamespaceFinder());
      });
  if (!out.has_value()) return out;

  // Run each checker, short circuiting after the first failure
  int dummy[] = {0, (UpdateOut(factory, op, count, checkers, &*out))...};
  static_cast<void>(dummy);
  return out;
}

template <typename T, typename EqualTo>
class StrongGuaranteeTester {
 public:
  explicit StrongGuaranteeTester(std::unique_ptr<T> t_ptr, EqualTo eq) noexcept
      : val_(std::move(t_ptr)), eq_(eq) {}

  testing::AssertionResult operator()(T* other) const {
    return eq_(*val_, *other) ? testing::AssertionSuccess()
                              : testing::AssertionFailure() << "State changed";
  }

 private:
  std::unique_ptr<T> val_;
  EqualTo eq_;
};
}  // namespace exceptions_internal

extern exceptions_internal::NoThrowTag no_throw_ctor;

// These are useful for tests which just construct objects and make sure there
// are no leaks.
inline void SetCountdown() { exceptions_internal::countdown = 0; }
inline void UnsetCountdown() { exceptions_internal::countdown = -1; }

// A test class which is contextually convertible to bool.  The conversion can
// be instrumented to throw at a controlled time.
class ThrowingBool {
 public:
  ThrowingBool(bool b) noexcept : b_(b) {}  // NOLINT(runtime/explicit)
  explicit operator bool() const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return b_;
  }

 private:
  bool b_;
};

// A testing class instrumented to throw an exception at a controlled time.
//
// ThrowingValue implements a slightly relaxed version of the Regular concept --
// that is it's a value type with the expected semantics.  It also implements
// arithmetic operations.  It doesn't implement member and pointer operators
// like operator-> or operator[].
//
// ThrowingValue can be instrumented to have certain operations be noexcept by
// using compile-time bitfield flag template arguments.  That is, to make an
// ThrowingValue which has a noexcept move constructor and noexcept move
// assignment, use
// ThrowingValue<absl::NoThrow::kMoveCtor | absl::NoThrow::kMoveAssign>.
template <NoThrow Flags = NoThrow::kNone>
class ThrowingValue : private exceptions_internal::TrackedObject {
 public:
  ThrowingValue() : TrackedObject(ABSL_PRETTY_FUNCTION) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ = 0;
  }

  ThrowingValue(const ThrowingValue& other)
      : TrackedObject(ABSL_PRETTY_FUNCTION) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ = other.dummy_;
  }

  ThrowingValue(ThrowingValue&& other) noexcept(
      !exceptions_internal::ThrowingAllowed(Flags, NoThrow::kMoveCtor))
      : TrackedObject(ABSL_PRETTY_FUNCTION) {
    if (exceptions_internal::ThrowingAllowed(Flags, NoThrow::kMoveCtor)) {
      exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    }
    dummy_ = other.dummy_;
  }

  explicit ThrowingValue(int i) noexcept(
      !exceptions_internal::ThrowingAllowed(Flags, NoThrow::kIntCtor))
      : TrackedObject(ABSL_PRETTY_FUNCTION) {
    if (exceptions_internal::ThrowingAllowed(Flags, NoThrow::kIntCtor)) {
      exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    }
    dummy_ = i;
  }

  ThrowingValue(int i, exceptions_internal::NoThrowTag) noexcept
      : TrackedObject(ABSL_PRETTY_FUNCTION), dummy_(i) {}

  // absl expects nothrow destructors
  ~ThrowingValue() noexcept = default;

  ThrowingValue& operator=(const ThrowingValue& other) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ = other.dummy_;
    return *this;
  }

  ThrowingValue& operator=(ThrowingValue&& other) noexcept(
      !exceptions_internal::ThrowingAllowed(Flags, NoThrow::kMoveAssign)) {
    if (exceptions_internal::ThrowingAllowed(Flags, NoThrow::kMoveAssign)) {
      exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    }
    dummy_ = other.dummy_;
    return *this;
  }

  // Arithmetic Operators
  ThrowingValue operator+(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ + other.dummy_, no_throw_ctor);
  }

  ThrowingValue operator+() const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_, no_throw_ctor);
  }

  ThrowingValue operator-(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ - other.dummy_, no_throw_ctor);
  }

  ThrowingValue operator-() const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(-dummy_, no_throw_ctor);
  }

  ThrowingValue& operator++() {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    ++dummy_;
    return *this;
  }

  ThrowingValue operator++(int) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    auto out = ThrowingValue(dummy_, no_throw_ctor);
    ++dummy_;
    return out;
  }

  ThrowingValue& operator--() {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    --dummy_;
    return *this;
  }

  ThrowingValue operator--(int) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    auto out = ThrowingValue(dummy_, no_throw_ctor);
    --dummy_;
    return out;
  }

  ThrowingValue operator*(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ * other.dummy_, no_throw_ctor);
  }

  ThrowingValue operator/(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ / other.dummy_, no_throw_ctor);
  }

  ThrowingValue operator%(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ % other.dummy_, no_throw_ctor);
  }

  ThrowingValue operator<<(int shift) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ << shift, no_throw_ctor);
  }

  ThrowingValue operator>>(int shift) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ >> shift, no_throw_ctor);
  }

  // Comparison Operators
  friend ThrowingBool operator==(const ThrowingValue& a,
                                 const ThrowingValue& b) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return a.dummy_ == b.dummy_;
  }
  friend ThrowingBool operator!=(const ThrowingValue& a,
                                 const ThrowingValue& b) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return a.dummy_ != b.dummy_;
  }
  friend ThrowingBool operator<(const ThrowingValue& a,
                                const ThrowingValue& b) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return a.dummy_ < b.dummy_;
  }
  friend ThrowingBool operator<=(const ThrowingValue& a,
                                 const ThrowingValue& b) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return a.dummy_ <= b.dummy_;
  }
  friend ThrowingBool operator>(const ThrowingValue& a,
                                const ThrowingValue& b) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return a.dummy_ > b.dummy_;
  }
  friend ThrowingBool operator>=(const ThrowingValue& a,
                                 const ThrowingValue& b) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return a.dummy_ >= b.dummy_;
  }

  // Logical Operators
  ThrowingBool operator!() const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return !dummy_;
  }

  ThrowingBool operator&&(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return dummy_ && other.dummy_;
  }

  ThrowingBool operator||(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return dummy_ || other.dummy_;
  }

  // Bitwise Logical Operators
  ThrowingValue operator~() const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(~dummy_, no_throw_ctor);
  }

  ThrowingValue operator&(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ & other.dummy_, no_throw_ctor);
  }

  ThrowingValue operator|(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ | other.dummy_, no_throw_ctor);
  }

  ThrowingValue operator^(const ThrowingValue& other) const {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return ThrowingValue(dummy_ ^ other.dummy_, no_throw_ctor);
  }

  // Compound Assignment operators
  ThrowingValue& operator+=(const ThrowingValue& other) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ += other.dummy_;
    return *this;
  }

  ThrowingValue& operator-=(const ThrowingValue& other) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ -= other.dummy_;
    return *this;
  }

  ThrowingValue& operator*=(const ThrowingValue& other) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ *= other.dummy_;
    return *this;
  }

  ThrowingValue& operator/=(const ThrowingValue& other) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ /= other.dummy_;
    return *this;
  }

  ThrowingValue& operator%=(const ThrowingValue& other) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ %= other.dummy_;
    return *this;
  }

  ThrowingValue& operator&=(const ThrowingValue& other) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ &= other.dummy_;
    return *this;
  }

  ThrowingValue& operator|=(const ThrowingValue& other) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ |= other.dummy_;
    return *this;
  }

  ThrowingValue& operator^=(const ThrowingValue& other) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ ^= other.dummy_;
    return *this;
  }

  ThrowingValue& operator<<=(int shift) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ <<= shift;
    return *this;
  }

  ThrowingValue& operator>>=(int shift) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ >>= shift;
    return *this;
  }

  // Pointer operators
  void operator&() const = delete;  // NOLINT(runtime/operator)

  // Stream operators
  friend std::ostream& operator<<(std::ostream& os, const ThrowingValue&) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return os;
  }

  friend std::istream& operator>>(std::istream& is, const ThrowingValue&) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    return is;
  }

  // Memory management operators
  // Args.. allows us to overload regular and placement new in one shot
  template <typename... Args>
  static void* operator new(size_t s, Args&&... args) noexcept(
      !exceptions_internal::ThrowingAllowed(Flags, NoThrow::kAllocation)) {
    if (exceptions_internal::ThrowingAllowed(Flags, NoThrow::kAllocation)) {
      exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION, true);
    }
    return ::operator new(s, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void* operator new[](size_t s, Args&&... args) noexcept(
      !exceptions_internal::ThrowingAllowed(Flags, NoThrow::kAllocation)) {
    if (exceptions_internal::ThrowingAllowed(Flags, NoThrow::kAllocation)) {
      exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION, true);
    }
    return ::operator new[](s, std::forward<Args>(args)...);
  }

  // Abseil doesn't support throwing overloaded operator delete.  These are
  // provided so a throwing operator-new can clean up after itself.
  //
  // We provide both regular and templated operator delete because if only the
  // templated version is provided as we did with operator new, the compiler has
  // no way of knowing which overload of operator delete to call. See
  // http://en.cppreference.com/w/cpp/memory/new/operator_delete and
  // http://en.cppreference.com/w/cpp/language/delete for the gory details.
  void operator delete(void* p) noexcept { ::operator delete(p); }

  template <typename... Args>
  void operator delete(void* p, Args&&... args) noexcept {
    ::operator delete(p, std::forward<Args>(args)...);
  }

  void operator delete[](void* p) noexcept { return ::operator delete[](p); }

  template <typename... Args>
  void operator delete[](void* p, Args&&... args) noexcept {
    return ::operator delete[](p, std::forward<Args>(args)...);
  }

  // Non-standard access to the actual contained value.  No need for this to
  // throw.
  int& Get() noexcept { return dummy_; }
  const int& Get() const noexcept { return dummy_; }

 private:
  int dummy_;
};
// While not having to do with exceptions, explicitly delete comma operator, to
// make sure we don't use it on user-supplied types.
template <NoThrow N, typename T>
void operator,(const ThrowingValue<N>& ef, T&& t) = delete;
template <NoThrow N, typename T>
void operator,(T&& t, const ThrowingValue<N>& ef) = delete;

// An allocator type which is instrumented to throw at a controlled time, or not
// to throw, using NoThrow.  The supported settings are the default of every
// function which is allowed to throw in a conforming allocator possibly
// throwing, or nothing throws, in line with the ABSL_ALLOCATOR_THROWS
// configuration macro.
template <typename T, NoThrow Flags = NoThrow::kNone>
class ThrowingAllocator : private exceptions_internal::TrackedObject {
  static_assert(Flags == NoThrow::kNone || Flags == NoThrow::kNoThrow,
                "Invalid flag");

 public:
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using void_pointer = void*;
  using const_void_pointer = const void*;
  using value_type = T;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  using is_nothrow = std::integral_constant<bool, Flags == NoThrow::kNoThrow>;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  using is_always_equal = std::false_type;

  ThrowingAllocator() : TrackedObject(ABSL_PRETTY_FUNCTION) {
    exceptions_internal::MaybeThrow(ABSL_PRETTY_FUNCTION);
    dummy_ = std::make_shared<const int>(next_id_++);
  }

  template <typename U>
  ThrowingAllocator(  // NOLINT
      const ThrowingAllocator<U, Flags>& other) noexcept
      : TrackedObject(ABSL_PRETTY_FUNCTION), dummy_(other.State()) {}

  ThrowingAllocator(const ThrowingAllocator& other) noexcept
      : TrackedObject(ABSL_PRETTY_FUNCTION), dummy_(other.State()) {}

  template <typename U>
  ThrowingAllocator(  // NOLINT
      ThrowingAllocator<U, Flags>&& other) noexcept
      : TrackedObject(ABSL_PRETTY_FUNCTION), dummy_(std::move(other.State())) {}

  ThrowingAllocator(ThrowingAllocator&& other) noexcept
      : TrackedObject(ABSL_PRETTY_FUNCTION), dummy_(std::move(other.State())) {}

  ~ThrowingAllocator() noexcept = default;

  template <typename U>
  ThrowingAllocator& operator=(
      const ThrowingAllocator<U, Flags>& other) noexcept {
    dummy_ = other.State();
    return *this;
  }

  template <typename U>
  ThrowingAllocator& operator=(ThrowingAllocator<U, Flags>&& other) noexcept {
    dummy_ = std::move(other.State());
    return *this;
  }

  template <typename U>
  struct rebind {
    using other = ThrowingAllocator<U, Flags>;
  };

  pointer allocate(size_type n) noexcept(
      !exceptions_internal::ThrowingAllowed(Flags, NoThrow::kNoThrow)) {
    ReadStateAndMaybeThrow(ABSL_PRETTY_FUNCTION);
    return static_cast<pointer>(::operator new(n * sizeof(T)));
  }
  pointer allocate(size_type n, const_void_pointer) noexcept(
      !exceptions_internal::ThrowingAllowed(Flags, NoThrow::kNoThrow)) {
    return allocate(n);
  }

  void deallocate(pointer ptr, size_type) noexcept {
    ReadState();
    ::operator delete(static_cast<void*>(ptr));
  }

  template <typename U, typename... Args>
  void construct(U* ptr, Args&&... args) noexcept(
      !exceptions_internal::ThrowingAllowed(Flags, NoThrow::kNoThrow)) {
    ReadStateAndMaybeThrow(ABSL_PRETTY_FUNCTION);
    ::new (static_cast<void*>(ptr)) U(std::forward<Args>(args)...);
  }

  template <typename U>
  void destroy(U* p) noexcept {
    ReadState();
    p->~U();
  }

  size_type max_size() const noexcept {
    return std::numeric_limits<difference_type>::max() / sizeof(value_type);
  }

  ThrowingAllocator select_on_container_copy_construction() noexcept(
      !exceptions_internal::ThrowingAllowed(Flags, NoThrow::kNoThrow)) {
    auto& out = *this;
    ReadStateAndMaybeThrow(ABSL_PRETTY_FUNCTION);
    return out;
  }

  template <typename U>
  bool operator==(const ThrowingAllocator<U, Flags>& other) const noexcept {
    return dummy_ == other.dummy_;
  }

  template <typename U>
  bool operator!=(const ThrowingAllocator<U, Flags>& other) const noexcept {
    return dummy_ != other.dummy_;
  }

  template <typename U, NoThrow B>
  friend class ThrowingAllocator;

 private:
  const std::shared_ptr<const int>& State() const { return dummy_; }
  std::shared_ptr<const int>& State() { return dummy_; }

  void ReadState() {
    // we know that this will never be true, but the compiler doesn't, so this
    // should safely force a read of the value.
    if (*dummy_ < 0) std::abort();
  }

  void ReadStateAndMaybeThrow(absl::string_view msg) const {
    if (exceptions_internal::ThrowingAllowed(Flags, NoThrow::kNoThrow)) {
      exceptions_internal::MaybeThrow(
          absl::Substitute("Allocator id $0 threw from $1", *dummy_, msg));
    }
  }

  static int next_id_;
  std::shared_ptr<const int> dummy_;
};

template <typename T, NoThrow Throws>
int ThrowingAllocator<T, Throws>::next_id_ = 0;

// Inspects the constructions and destructions of anything inheriting from
// TrackedObject.  Place this as a member variable in a test fixture to ensure
// that every ThrowingValue was constructed and destroyed correctly.  This also
// allows us to safely "leak" TrackedObjects, as AllocInspector will destroy
// everything left over in its destructor.
struct AllocInspector {
  AllocInspector() = default;
  ~AllocInspector() {
    auto& allocs = exceptions_internal::TrackedObject::GetAllocs();
    for (const auto& kv : allocs) {
      ADD_FAILURE() << "Object at address " << static_cast<void*>(kv.first)
                    << " constructed from " << kv.second << " not destroyed";
    }
    allocs.clear();
  }
};

// Tests for resource leaks by attempting to construct a T using args repeatedly
// until successful, using the countdown method.  Side effects can then be
// tested for resource leaks.  If an AllocInspector is present in the test
// fixture, then this will also test that memory resources are not leaked as
// long as T allocates TrackedObjects.
template <typename T, typename... Args>
T TestThrowingCtor(Args&&... args) {
  struct Cleanup {
    ~Cleanup() { UnsetCountdown(); }
  };
  Cleanup c;
  for (int countdown = 0;; ++countdown) {
    exceptions_internal::countdown = countdown;
    try {
      return T(std::forward<Args>(args)...);
    } catch (const exceptions_internal::TestException&) {
    }
  }
}

// Tests that performing operation Op on a T follows exception safety
// guarantees.  By default only tests the basic guarantee. There must be a
// function, AbslCheckInvariants(T*, absl::InternalAbslNamespaceFinder) which
// returns anything convertible to bool and which makes sure the invariants of
// the type are upheld.  This is called before any of the checkers.  The
// InternalAbslNamespaceFinder is unused, and just helps find
// AbslCheckInvariants for absl types which become aliases to std::types in
// C++17.
//
// Parameters:
//   * TFactory: operator() returns a unique_ptr to the type under test (T).  It
//   should always return pointers to values which compare equal.
//   * FunctionFromTPtrToVoid: A functor exercising the function under test.  It
//   should take a T* and return void.
//   * Checkers: Any number of functions taking a T* and returning
//   anything contextually convertible to bool.  If a testing::AssertionResult
//   is used then the error message is kept.  These test invariants related to
//   the operation. To test the strong guarantee, pass
//   absl::StrongGuarantee(factory).  A checker may freely modify the passed-in
//   T, for example to make sure the T can be set to a known state.
template <typename TFactory, typename FunctionFromTPtrToVoid,
          typename... Checkers>
testing::AssertionResult TestExceptionSafety(TFactory factory,
                                             FunctionFromTPtrToVoid&& op,
                                             const Checkers&... checkers) {
  struct Cleanup {
    ~Cleanup() { UnsetCountdown(); }
  } c;
  for (int countdown = 0;; ++countdown) {
    auto out = exceptions_internal::TestAtCountdown(factory, op, countdown,
                                                    checkers...);
    if (!out.has_value()) {
      return testing::AssertionSuccess();
    }
    if (!*out) return *out;
  }
}

// Returns a functor to test for the strong exception-safety guarantee.
// Equality comparisons are made against the T provided by the factory and
// default to using operator==.
//
// Parameters:
//   * TFactory: operator() returns a unique_ptr to the type under test.  It
//   should always return pointers to values which compare equal.
template <typename TFactory, typename EqualTo = std::equal_to<
                                 exceptions_internal::FactoryType<TFactory>>>
exceptions_internal::StrongGuaranteeTester<
    exceptions_internal::FactoryType<TFactory>, EqualTo>
StrongGuarantee(TFactory factory, EqualTo eq = EqualTo()) {
  return exceptions_internal::StrongGuaranteeTester<
      exceptions_internal::FactoryType<TFactory>, EqualTo>(factory(), eq);
}

}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_EXCEPTION_SAFETY_TESTING_H_
