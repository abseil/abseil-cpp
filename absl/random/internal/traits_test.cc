// Copyright 2017 The Abseil Authors.
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

#include "absl/random/internal/traits.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <type_traits>

#include "gtest/gtest.h"

namespace {

using absl::random_internal::HasSeedSeqGenerate;
using absl::random_internal::HasSeedSeqSize;
using absl::random_internal::is_urbg;
using absl::random_internal::is_widening_convertible;
using absl::random_internal::IsSaltedSeedSeq;
using absl::random_internal::IsValidSeedSeqResultType;

static_assert(is_urbg<std::minstd_rand>::value);
static_assert(!is_urbg<uint64_t>::value);

// CheckWideningConvertsToSelf<T1, T2, ...>()
//
// For each type T, checks:
// - T IS widening-convertible to itself.
//
template <typename T>
void CheckWideningConvertsToSelf() {
  static_assert(is_widening_convertible<T, T>::value,
                "Type is not convertible to self!");
}

template <typename T, typename Next, typename... Args>
void CheckWideningConvertsToSelf() {
  CheckWideningConvertsToSelf<T>();
  CheckWideningConvertsToSelf<Next, Args...>();
}

// CheckNotWideningConvertibleWithSigned<T1, T2, ...>()
//
// For each unsigned-type T, checks that:
// - T is NOT widening-convertible to Signed(T)
// - Signed(T) is NOT widening-convertible to T
//
template <typename T>
void CheckNotWideningConvertibleWithSigned() {
  using signed_t = std::make_signed_t<T>;

  static_assert(!is_widening_convertible<T, signed_t>::value,
                "Unsigned type is convertible to same-sized signed-type!");
  static_assert(!is_widening_convertible<signed_t, T>::value,
                "Signed type is convertible to same-sized unsigned-type!");
}

template <typename T, typename Next, typename... Args>
void CheckNotWideningConvertibleWithSigned() {
  CheckNotWideningConvertibleWithSigned<T>();
  CheckWideningConvertsToSelf<Next, Args...>();
}

// CheckWideningConvertsToLargerType<T1, T2, ...>()
//
// For each successive unsigned-types {Ti, Ti+1}, checks that:
// - Ti IS widening-convertible to Ti+1
// - Ti IS widening-convertible to Signed(Ti+1)
// - Signed(Ti) is NOT widening-convertible to Ti
// - Signed(Ti) IS widening-convertible to Ti+1
template <typename T, typename Higher>
void CheckWideningConvertsToLargerTypes() {
  using signed_t = std::make_signed_t<T>;
  using higher_t = Higher;
  using signed_higher_t = std::make_signed_t<Higher>;

  static_assert(is_widening_convertible<T, higher_t>::value,
                "Type not embeddable into larger type!");
  static_assert(is_widening_convertible<T, signed_higher_t>::value,
                "Type not embeddable into larger signed type!");
  static_assert(!is_widening_convertible<signed_t, higher_t>::value,
                "Signed type is embeddable into larger unsigned type!");
  static_assert(is_widening_convertible<signed_t, signed_higher_t>::value,
                "Signed type not embeddable into larger signed type!");
}

template <typename T, typename Higher, typename Next, typename... Args>
void CheckWideningConvertsToLargerTypes() {
  CheckWideningConvertsToLargerTypes<T, Higher>();
  CheckWideningConvertsToLargerTypes<Higher, Next, Args...>();
}

// CheckWideningConvertsTo<T, U, [expect]>
//
// Checks that T DOES widening-convert to U.
// If "expect" is false, then asserts that T does NOT widening-convert to U.
template <typename T, typename U, bool expect = true>
void CheckWideningConvertsTo() {
  static_assert(is_widening_convertible<T, U>::value == expect,
                "Unexpected result for is_widening_convertible<T, U>!");
}

TEST(TraitsTest, IsWideningConvertibleTest) {
  constexpr bool kInvalid = false;

  CheckWideningConvertsToSelf<uint8_t, uint16_t, uint32_t, uint64_t, int8_t,
                              int16_t, int32_t, int64_t, float, double>();
  CheckNotWideningConvertibleWithSigned<uint8_t, uint16_t, uint32_t,
                                        uint64_t>();
  CheckWideningConvertsToLargerTypes<uint8_t, uint16_t, uint32_t, uint64_t>();

  CheckWideningConvertsTo<float, double>();
  CheckWideningConvertsTo<uint16_t, float>();
  CheckWideningConvertsTo<uint32_t, double>();
  CheckWideningConvertsTo<uint64_t, double, kInvalid>();
  CheckWideningConvertsTo<double, float, kInvalid>();

  CheckWideningConvertsTo<bool, int>();
  CheckWideningConvertsTo<bool, float>();
}

enum UnscopedU32Enum : uint32_t { kZero = 0 };
enum class ScopedU32Enum : uint32_t { kZero = 0 };
struct EmptyStruct {};

struct NoResultSeedSeq {
  size_t size() const { return 1; }
  void generate(uint32_t*, uint32_t*) {}
};

struct MutableOnlySeedSeq {
  using result_type = uint32_t;
  size_t size() { return 1; }
  void generate(uint32_t*, uint32_t*) {}
};

struct ConstSeedSeq {
  using result_type = uint64_t;
  size_t size() const { return 1; }
  void generate(uint32_t*, uint32_t*) const {}
};

struct InvalidResultAutoGenerateSeedSeq {
  using result_type = int;
  template <typename It>
  auto generate(It begin, It) {
    static_assert(sizeof(It) == 0,
                  "HasSeedSeqGenerate must short-circuit before generate()");
    return *begin;
  }
};

struct VoidResultAutoGenerateSeedSeq {
  using result_type = void;
  template <typename It>
  auto generate(It begin, It) {
    static_assert(sizeof(It) == 0,
                  "HasSeedSeqGenerate must short-circuit before generate()");
    return *begin;
  }
};

// IsValidSeedSeqResultType accepts unqualified unsigned integers >= 32 bits.
static_assert(IsValidSeedSeqResultType<uint32_t>());
static_assert(IsValidSeedSeqResultType<uint64_t>());
static_assert(IsValidSeedSeqResultType<unsigned int>());
static_assert(IsValidSeedSeqResultType<unsigned long>());  // NOLINT
static_assert(IsValidSeedSeqResultType<unsigned long long>());  // NOLINT
static_assert(IsValidSeedSeqResultType<std::seed_seq::result_type>());

// IsValidSeedSeqResultType rejects narrow, signed, cv-ref, and non-integers.
static_assert(!IsValidSeedSeqResultType<bool>());
static_assert(!IsValidSeedSeqResultType<uint8_t>());
static_assert(!IsValidSeedSeqResultType<uint16_t>());
static_assert(!IsValidSeedSeqResultType<int32_t>());
static_assert(!IsValidSeedSeqResultType<int64_t>());
static_assert(!IsValidSeedSeqResultType<const uint32_t>());
static_assert(!IsValidSeedSeqResultType<volatile uint32_t>());
static_assert(!IsValidSeedSeqResultType<uint32_t&>());
static_assert(!IsValidSeedSeqResultType<uint32_t&&>());
static_assert(!IsValidSeedSeqResultType<float>());
static_assert(!IsValidSeedSeqResultType<double>());
static_assert(!IsValidSeedSeqResultType<void>());
static_assert(!IsValidSeedSeqResultType<UnscopedU32Enum>());
static_assert(!IsValidSeedSeqResultType<ScopedU32Enum>());
static_assert(!IsValidSeedSeqResultType<EmptyStruct>());

// HasSeedSeqGenerate short-circuits invalid result_type and preserves const.
static_assert(!HasSeedSeqGenerate<NoResultSeedSeq>::value);
static_assert(!HasSeedSeqGenerate<InvalidResultAutoGenerateSeedSeq&>::value);
static_assert(!HasSeedSeqGenerate<VoidResultAutoGenerateSeedSeq&>::value);
static_assert(HasSeedSeqGenerate<MutableOnlySeedSeq&>::value);
static_assert(!HasSeedSeqGenerate<const MutableOnlySeedSeq&>::value);
static_assert(HasSeedSeqGenerate<const ConstSeedSeq&>::value);

// HasSeedSeqSize preserves const qualification.
static_assert(HasSeedSeqSize<MutableOnlySeedSeq&>::value);
static_assert(!HasSeedSeqSize<const MutableOnlySeedSeq&>::value);
static_assert(HasSeedSeqSize<const ConstSeedSeq&>::value);
static_assert(!HasSeedSeqSize<EmptyStruct>::value);

// IsSaltedSeedSeq defaults to false for non-SaltedSeedSeq types.
static_assert(!IsSaltedSeedSeq<std::seed_seq>::value);
static_assert(!IsSaltedSeedSeq<ConstSeedSeq>::value);
static_assert(!IsSaltedSeedSeq<EmptyStruct>::value);

}  // namespace
