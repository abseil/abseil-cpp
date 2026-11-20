// Copyright 2022 The Abseil Authors.
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

// To prevent compiler memory exhaustion (OOM / Killed signal terminates
// cc1plus) during parallel builds with GCC, the test suite instantiations have
// been split into multiple compilation units.

// SKIP_ABSL_INLINE_NAMESPACE_CHECK

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/any_invocable_test.h"

namespace absl_any_invocable_test {
namespace {

template <size_t N>
struct ByteArrayFunctor {
  unsigned char bytes[N];

  int operator()(int seed) const noexcept {
    int sum = seed;
    for (size_t i = 0; i < N; ++i) {
      sum += static_cast<int>(bytes[i]) * static_cast<int>(i + 1);
    }
    return sum;
  }
};

template <size_t N>
void VerifyByteArrayFunctor() {
  static_assert(std::is_trivially_copyable_v<ByteArrayFunctor<N>>);
  if constexpr (N <= absl::internal_any_invocable::kStorageSize) {
    static_assert(
        absl::internal_any_invocable::IsStoredLocally<ByteArrayFunctor<N>>());
  }

  ByteArrayFunctor<N> f1{};
  ByteArrayFunctor<N> f2{};
  for (size_t i = 0; i < N; ++i) {
    f1.bytes[i] = static_cast<unsigned char>(i + 3);
    f2.bytes[i] = static_cast<unsigned char>(i + 17);
  }
  const int expected1 = f1(5);
  const int expected2 = f2(5);

  absl::AnyInvocable<int(int) const> a(f1);
  EXPECT_EQ(a(5), expected1);

  absl::AnyInvocable<int(int) const> b(std::move(a));
  EXPECT_EQ(b(5), expected1);

  absl::AnyInvocable<int(int) const> c(std::in_place_type<ByteArrayFunctor<N>>,
                                       f2);
  EXPECT_EQ(c(5), expected2);

  b.swap(c);
  EXPECT_EQ(b(5), expected2);
  EXPECT_EQ(c(5), expected1);

  b = std::move(c);
  EXPECT_EQ(b(5), expected1);
  b = nullptr;
  EXPECT_EQ(b, nullptr);
}

TEST(AnyInvTestTrivialStorage, SubWordAndMultiWordSizes) {
  VerifyByteArrayFunctor<1>();
  VerifyByteArrayFunctor<2>();
  VerifyByteArrayFunctor<3>();
  VerifyByteArrayFunctor<4>();
  VerifyByteArrayFunctor<5>();
  VerifyByteArrayFunctor<6>();
  VerifyByteArrayFunctor<7>();
  VerifyByteArrayFunctor<8>();
  VerifyByteArrayFunctor<9>();
  VerifyByteArrayFunctor<12>();
  VerifyByteArrayFunctor<15>();
  VerifyByteArrayFunctor<16>();
  // Also exercise remote trivially destructible storage
  // (`RemoteManagerTrivial`) with default alignment (alignof == 1 <=
  // ABSL_INTERNAL_DEFAULT_NEW_ALIGNMENT).
  VerifyByteArrayFunctor<24>();
}

struct CharIntPaddedFunctor {
  char c;
  int x;

  int operator()(int delta) const noexcept {
    return static_cast<int>(c) + x + delta;
  }
};

struct CharPtrPaddedFunctor {
  char c;
  const int* p;

  int operator()(int delta) const noexcept {
    return static_cast<int>(c) + *p + delta;
  }
};

struct PtrCharPaddedFunctor {
  const int* p;
  char c;

  int operator()(int delta) const noexcept {
    return static_cast<int>(c) + *p + delta;
  }
};

// Construct padded structs field-by-field in noinline helpers so padding bytes
// remain uninitialized under sanitizers such as MSan.
ABSL_ATTRIBUTE_NOINLINE CharIntPaddedFunctor MakeCharIntPadded(char c, int x) {
  CharIntPaddedFunctor f;
  f.c = c;
  f.x = x;
  return f;
}

ABSL_ATTRIBUTE_NOINLINE CharPtrPaddedFunctor MakeCharPtrPadded(char c,
                                                               const int* p) {
  CharPtrPaddedFunctor f;
  f.c = c;
  f.p = p;
  return f;
}

ABSL_ATTRIBUTE_NOINLINE PtrCharPaddedFunctor MakePtrCharPadded(const int* p,
                                                               char c) {
  PtrCharPaddedFunctor f;
  f.p = p;
  f.c = c;
  return f;
}

TEST(AnyInvTestTrivialStorage, InternalAndTailPadding) {
  static_assert(std::is_trivially_copyable_v<CharIntPaddedFunctor>);
  static_assert(std::is_trivially_copyable_v<CharPtrPaddedFunctor>);
  static_assert(std::is_trivially_copyable_v<PtrCharPaddedFunctor>);
  static_assert(
      absl::internal_any_invocable::IsStoredLocally<CharIntPaddedFunctor>());
  static_assert(
      absl::internal_any_invocable::IsStoredLocally<CharPtrPaddedFunctor>());
  static_assert(
      absl::internal_any_invocable::IsStoredLocally<PtrCharPaddedFunctor>());

  const int target = 100;
  {
    CharIntPaddedFunctor f = MakeCharIntPadded(3, 40);
    absl::AnyInvocable<int(int) const> a(f);
    absl::AnyInvocable<int(int) const> b(std::move(a));
    EXPECT_EQ(b(5), 48);
  }
  {
    CharPtrPaddedFunctor f = MakeCharPtrPadded(4, &target);
    absl::AnyInvocable<int(int) const> a(f);
    absl::AnyInvocable<int(int) const> b(std::move(a));
    EXPECT_EQ(b(5), 109);
  }
  {
    PtrCharPaddedFunctor f = MakePtrCharPadded(&target, 6);
    absl::AnyInvocable<int(int) const> a(f);
    absl::AnyInvocable<int(int) const> b(std::move(a));
    EXPECT_EQ(b(5), 111);
  }
}

struct MoveOnlySingleWordFunctor {
  int32_t value;

  explicit MoveOnlySingleWordFunctor(int32_t v) noexcept : value(v) {}
  MoveOnlySingleWordFunctor(const MoveOnlySingleWordFunctor&) = delete;
  MoveOnlySingleWordFunctor& operator=(const MoveOnlySingleWordFunctor&) =
      delete;
  MoveOnlySingleWordFunctor(MoveOnlySingleWordFunctor&&) noexcept = default;
  MoveOnlySingleWordFunctor& operator=(MoveOnlySingleWordFunctor&&) noexcept =
      default;

  int operator()(int x) const noexcept { return value + x; }
};

struct MoveOnlyMultiWordFunctor {
  const int* ptr;
  int32_t value;

  MoveOnlyMultiWordFunctor(const int* p, int32_t v) noexcept
      : ptr(p), value(v) {}
  MoveOnlyMultiWordFunctor(const MoveOnlyMultiWordFunctor&) = delete;
  MoveOnlyMultiWordFunctor& operator=(const MoveOnlyMultiWordFunctor&) = delete;
  MoveOnlyMultiWordFunctor(MoveOnlyMultiWordFunctor&&) noexcept = default;
  MoveOnlyMultiWordFunctor& operator=(MoveOnlyMultiWordFunctor&&) noexcept =
      default;

  int operator()(int x) const noexcept { return *ptr + value + x; }
};

TEST(AnyInvTestTrivialStorage, MoveOnlyTriviallyCopyable) {
  static_assert(std::is_trivially_copyable_v<MoveOnlySingleWordFunctor>);
  static_assert(!std::is_copy_constructible_v<MoveOnlySingleWordFunctor>);
  static_assert(absl::internal_any_invocable::IsStoredLocally<
                MoveOnlySingleWordFunctor>());

  static_assert(std::is_trivially_copyable_v<MoveOnlyMultiWordFunctor>);
  static_assert(!std::is_copy_constructible_v<MoveOnlyMultiWordFunctor>);
  static_assert(absl::internal_any_invocable::IsStoredLocally<
                MoveOnlyMultiWordFunctor>());

  {
    absl::AnyInvocable<int(int) const> a(MoveOnlySingleWordFunctor(11));
    EXPECT_EQ(a(4), 15);
    absl::AnyInvocable<int(int) const> b(std::move(a));
    EXPECT_EQ(b(6), 17);
    absl::AnyInvocable<int(int) const> c(
        std::in_place_type<MoveOnlySingleWordFunctor>, 21);
    b.swap(c);
    EXPECT_EQ(b(1), 22);
    EXPECT_EQ(c(1), 12);
  }

  const int base = 100;
  {
    absl::AnyInvocable<int(int) const> a(MoveOnlyMultiWordFunctor(&base, 11));
    EXPECT_EQ(a(4), 115);
    absl::AnyInvocable<int(int) const> b(std::move(a));
    EXPECT_EQ(b(6), 117);
    absl::AnyInvocable<int(int) const> c(
        std::in_place_type<MoveOnlyMultiWordFunctor>, &base, 21);
    b.swap(c);
    EXPECT_EQ(b(1), 122);
    EXPECT_EQ(c(1), 112);
  }
}

TEST(AnyInvTestTrivialStorage, EmptyCallable) {
  struct EmptyFunctor {
    int operator()(int x) const noexcept { return x + 7; }
  };
  static_assert(std::is_empty_v<EmptyFunctor>);
  static_assert(std::is_trivially_copyable_v<EmptyFunctor>);
  static_assert(absl::internal_any_invocable::IsStoredLocally<EmptyFunctor>());

  absl::AnyInvocable<int(int) const> a(EmptyFunctor{});
  EXPECT_EQ(a(10), 17);
  absl::AnyInvocable<int(int) const> b(std::move(a));
  EXPECT_EQ(b(20), 27);
  absl::AnyInvocable<int(int) const> c;
  b.swap(c);
  EXPECT_EQ(b, nullptr);
  EXPECT_EQ(c(30), 37);
}

TEST(AnyInvTestTrivialStorage, MutableSubWordAndMultiWordState) {
  // 1-byte mutable capture.
  {
    std::uint8_t count = 1;
    absl::AnyInvocable<int()> a = [count]() mutable {
      count = static_cast<std::uint8_t>(count + 2);
      return static_cast<int>(count);
    };
    EXPECT_EQ(a(), 3);
    absl::AnyInvocable<int()> b = std::move(a);
    EXPECT_EQ(b(), 5);
    absl::AnyInvocable<int()> c = [x = std::uint8_t{10}]() mutable {
      x = static_cast<std::uint8_t>(x + 1);
      return static_cast<int>(x);
    };
    b.swap(c);
    EXPECT_EQ(b(), 11);
    EXPECT_EQ(c(), 7);
  }

  // 4-byte mutable capture.
  {
    int32_t count = 10;
    absl::AnyInvocable<int()> a = [count]() mutable {
      count += 3;
      return count;
    };
    EXPECT_EQ(a(), 13);
    absl::AnyInvocable<int()> b = std::move(a);
    EXPECT_EQ(b(), 16);
  }

  // Multi-word mutable capture (pointer + sub-word int).
  {
    int scale = 2;
    int32_t count = 1;
    absl::AnyInvocable<int()> a = [&scale, count]() mutable {
      count += 1;
      return count * scale;
    };
    EXPECT_EQ(a(), 4);
    absl::AnyInvocable<int()> b = std::move(a);
    EXPECT_EQ(b(), 6);
  }
}

}  // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(RemoteNonMovable, AnyInvTestBasic,
                               TestParameterListRemoteNonMovable);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestBasic, TestParameterListLocal);

INSTANTIATE_TYPED_TEST_SUITE_P(RemoteNonMovable, AnyInvTestCombinatoric,
                               TestParameterListRemoteNonMovable);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestCombinatoric,
                               TestParameterListLocal);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestMovable,
                               TestParameterListLocal);

INSTANTIATE_TYPED_TEST_SUITE_P(RemoteNonMovable, AnyInvTestNoexceptFalse,
                               TestParameterListRemoteNonMovable);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestNoexceptFalse,
                               TestParameterListLocal);

INSTANTIATE_TYPED_TEST_SUITE_P(RemoteNonMovable, AnyInvTestNonRvalue,
                               TestParameterListRemoteNonMovable);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestNonRvalue,
                               TestParameterListLocal);

}  // namespace absl_any_invocable_test
