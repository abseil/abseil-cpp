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

#include "absl/utility/utility.h"

#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"

namespace {

#ifdef _MSC_VER
// Warnings for unused variables in this test are false positives.  On other
// platforms, they are suppressed by ABSL_ATTRIBUTE_UNUSED, but that doesn't
// work on MSVC.
// Both the unused variables and the name length warnings are due to calls
// to absl::make_index_sequence with very large values, creating very long type
// names. The resulting warnings are so long they make build output unreadable.
#pragma warning( push )
#pragma warning( disable : 4503 )  // decorated name length exceeded
#pragma warning( disable : 4101 )  // unreferenced local variable
#endif  // _MSC_VER

using testing::StaticAssertTypeEq;
using testing::ElementsAre;

TEST(IntegerSequenceTest, ValueType) {
  StaticAssertTypeEq<int, absl::integer_sequence<int>::value_type>();
  StaticAssertTypeEq<char, absl::integer_sequence<char>::value_type>();
}

TEST(IntegerSequenceTest, Size) {
  EXPECT_EQ(0, (absl::integer_sequence<int>::size()));
  EXPECT_EQ(1, (absl::integer_sequence<int, 0>::size()));
  EXPECT_EQ(1, (absl::integer_sequence<int, 1>::size()));
  EXPECT_EQ(2, (absl::integer_sequence<int, 1, 2>::size()));
  EXPECT_EQ(3, (absl::integer_sequence<int, 0, 1, 2>::size()));
  EXPECT_EQ(3, (absl::integer_sequence<int, -123, 123, 456>::size()));
  constexpr size_t sz = absl::integer_sequence<int, 0, 1>::size();
  EXPECT_EQ(2, sz);
}

TEST(IntegerSequenceTest, MakeIndexSequence) {
  StaticAssertTypeEq<absl::index_sequence<>, absl::make_index_sequence<0>>();
  StaticAssertTypeEq<absl::index_sequence<0>, absl::make_index_sequence<1>>();
  StaticAssertTypeEq<absl::index_sequence<0, 1>,
                     absl::make_index_sequence<2>>();
  StaticAssertTypeEq<absl::index_sequence<0, 1, 2>,
                     absl::make_index_sequence<3>>();
}

TEST(IntegerSequenceTest, MakeIntegerSequence) {
  StaticAssertTypeEq<absl::integer_sequence<int>,
                     absl::make_integer_sequence<int, 0>>();
  StaticAssertTypeEq<absl::integer_sequence<int, 0>,
                     absl::make_integer_sequence<int, 1>>();
  StaticAssertTypeEq<absl::integer_sequence<int, 0, 1>,
                     absl::make_integer_sequence<int, 2>>();
  StaticAssertTypeEq<absl::integer_sequence<int, 0, 1, 2>,
                     absl::make_integer_sequence<int, 3>>();
}

template <typename... Ts>
class Counter {};

template <size_t... Is>
void CountAll(absl::index_sequence<Is...>) {
  // We only need an alias here, but instantiate a variable to silence warnings
  // for unused typedefs in some compilers.
  ABSL_ATTRIBUTE_UNUSED Counter<absl::make_index_sequence<Is>...> seq;
}

// This test verifies that absl::make_index_sequence can handle large arguments
// without blowing up template instantiation stack, going OOM or taking forever
// to compile (there is hard 15 minutes limit imposed by forge).
TEST(IntegerSequenceTest, MakeIndexSequencePerformance) {
  // O(log N) template instantiations.
  // We only need an alias here, but instantiate a variable to silence warnings
  // for unused typedefs in some compilers.
  ABSL_ATTRIBUTE_UNUSED absl::make_index_sequence<(1 << 16) - 1> seq;
  // O(N) template instantiations.
  CountAll(absl::make_index_sequence<(1 << 8) - 1>());
}

template <typename F, typename Tup, size_t... Is>
auto ApplyFromTupleImpl(F f, const Tup& tup, absl::index_sequence<Is...>)
    -> decltype(f(std::get<Is>(tup)...)) {
  return f(std::get<Is>(tup)...);
}

template <typename Tup>
using TupIdxSeq = absl::make_index_sequence<std::tuple_size<Tup>::value>;

template <typename F, typename Tup>
auto ApplyFromTuple(F f, const Tup& tup)
    -> decltype(ApplyFromTupleImpl(f, tup, TupIdxSeq<Tup>{})) {
  return ApplyFromTupleImpl(f, tup, TupIdxSeq<Tup>{});
}

template <typename T>
std::string Fmt(const T& x) {
  std::ostringstream os;
  os << x;
  return os.str();
}

struct PoorStrCat {
  template <typename... Args>
  std::string operator()(const Args&... args) const {
    std::string r;
    for (const auto& e : {Fmt(args)...}) r += e;
    return r;
  }
};

template <typename Tup, size_t... Is>
std::vector<std::string> TupStringVecImpl(const Tup& tup,
                                     absl::index_sequence<Is...>) {
  return {Fmt(std::get<Is>(tup))...};
}

template <typename... Ts>
std::vector<std::string> TupStringVec(const std::tuple<Ts...>& tup) {
  return TupStringVecImpl(tup, absl::index_sequence_for<Ts...>());
}

TEST(MakeIndexSequenceTest, ApplyFromTupleExample) {
  PoorStrCat f{};
  EXPECT_EQ("12abc3.14", f(12, "abc", 3.14));
  EXPECT_EQ("12abc3.14", ApplyFromTuple(f, std::make_tuple(12, "abc", 3.14)));
}

TEST(IndexSequenceForTest, Basic) {
  StaticAssertTypeEq<absl::index_sequence<>, absl::index_sequence_for<>>();
  StaticAssertTypeEq<absl::index_sequence<0>, absl::index_sequence_for<int>>();
  StaticAssertTypeEq<absl::index_sequence<0, 1, 2, 3>,
                     absl::index_sequence_for<int, void, char, int>>();
}

TEST(IndexSequenceForTest, Example) {
  EXPECT_THAT(TupStringVec(std::make_tuple(12, "abc", 3.14)),
              ElementsAre("12", "abc", "3.14"));
}

}  // namespace

