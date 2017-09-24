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

#include "absl/meta/type_traits.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace {

using ::testing::StaticAssertTypeEq;

struct Dummy {};

TEST(VoidTTest, BasicUsage) {
  StaticAssertTypeEq<void, absl::void_t<Dummy>>();
  StaticAssertTypeEq<void, absl::void_t<Dummy, Dummy, Dummy>>();
}

TEST(ConjunctionTest, BasicBooleanLogic) {
  EXPECT_TRUE(absl::conjunction<>::value);
  EXPECT_TRUE(absl::conjunction<std::true_type>::value);
  EXPECT_TRUE((absl::conjunction<std::true_type, std::true_type>::value));
  EXPECT_FALSE((absl::conjunction<std::true_type, std::false_type>::value));
  EXPECT_FALSE((absl::conjunction<std::false_type, std::true_type>::value));
  EXPECT_FALSE((absl::conjunction<std::false_type, std::false_type>::value));
}

struct MyTrueType {
  static constexpr bool value = true;
};

struct MyFalseType {
  static constexpr bool value = false;
};

TEST(ConjunctionTest, ShortCircuiting) {
  EXPECT_FALSE(
      (absl::conjunction<std::true_type, std::false_type, Dummy>::value));
  EXPECT_TRUE((std::is_base_of<MyFalseType,
                               absl::conjunction<std::true_type, MyFalseType,
                                                 std::false_type>>::value));
  EXPECT_TRUE(
      (std::is_base_of<MyTrueType,
                       absl::conjunction<std::true_type, MyTrueType>>::value));
}

TEST(DisjunctionTest, BasicBooleanLogic) {
  EXPECT_FALSE(absl::disjunction<>::value);
  EXPECT_FALSE(absl::disjunction<std::false_type>::value);
  EXPECT_TRUE((absl::disjunction<std::true_type, std::true_type>::value));
  EXPECT_TRUE((absl::disjunction<std::true_type, std::false_type>::value));
  EXPECT_TRUE((absl::disjunction<std::false_type, std::true_type>::value));
  EXPECT_FALSE((absl::disjunction<std::false_type, std::false_type>::value));
}

TEST(DisjunctionTest, ShortCircuiting) {
  EXPECT_TRUE(
      (absl::disjunction<std::false_type, std::true_type, Dummy>::value));
  EXPECT_TRUE((
      std::is_base_of<MyTrueType, absl::disjunction<std::false_type, MyTrueType,
                                                    std::true_type>>::value));
  EXPECT_TRUE((
      std::is_base_of<MyFalseType,
                      absl::disjunction<std::false_type, MyFalseType>>::value));
}

TEST(NegationTest, BasicBooleanLogic) {
  EXPECT_FALSE(absl::negation<std::true_type>::value);
  EXPECT_FALSE(absl::negation<MyTrueType>::value);
  EXPECT_TRUE(absl::negation<std::false_type>::value);
  EXPECT_TRUE(absl::negation<MyFalseType>::value);
}

// all member functions are trivial
class Trivial {
  int n_;
};

class TrivialDefaultCtor {
 public:
  TrivialDefaultCtor() = default;
  explicit TrivialDefaultCtor(int n) : n_(n) {}

 private:
  int n_;
};

class TrivialCopyCtor {
 public:
  explicit TrivialCopyCtor(int n) : n_(n) {}
  TrivialCopyCtor(const TrivialCopyCtor&) = default;
  TrivialCopyCtor& operator=(const TrivialCopyCtor& t) {
    n_ = t.n_;
    return *this;
  }

 private:
  int n_;
};

class TrivialCopyAssign {
 public:
  explicit TrivialCopyAssign(int n) : n_(n) {}
  TrivialCopyAssign(const TrivialCopyAssign& t) : n_(t.n_) {}
  TrivialCopyAssign& operator=(const TrivialCopyAssign& t) = default;
  ~TrivialCopyAssign() {}  // can have non trivial destructor
 private:
  int n_;
};

struct NonTrivialDestructor {
  ~NonTrivialDestructor() {}
};

struct TrivialDestructor {
  ~TrivialDestructor() = default;
};

struct NonCopyable {
  NonCopyable() = default;
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;
};

class Base {
 public:
  virtual ~Base() {}
};

// In GCC/Clang, std::is_trivially_constructible requires that the destructor is
// trivial. However, MSVC doesn't require that. This results in different
// behavior when checking is_trivially_constructible on any type with nontrivial
// destructor. Since absl::is_trivially_default_constructible and
// absl::is_trivially_copy_constructible both follows Clang/GCC's interpretation
// and check is_trivially_destructible, it results in inconsistency with
// std::is_trivially_xxx_constructible on MSVC. This macro is used to work
// around this issue in test. In practice, a trivially constructible type
// should also be trivially destructible.
// GCC bug 51452: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=51452
// LWG issue 2116: http://cplusplus.github.io/LWG/lwg-active.html#2116.
#ifdef _MSC_VER
#define ABSL_TRIVIALLY_CONSTRUCTIBLE_VERIFY_TRIVIALLY_DESTRUCTIBLE
#endif

TEST(TypeTraitsTest, TestTrivialDefaultCtor) {
  // arithmetic types and pointers have trivial default constructors.
  EXPECT_TRUE(absl::is_trivially_default_constructible<bool>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<char>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<unsigned char>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<signed char>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<wchar_t>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<int>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<unsigned int>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<int16_t>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<uint16_t>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<int64_t>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<uint64_t>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<float>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<double>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<long double>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<std::string*>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<Trivial*>::value);
  EXPECT_TRUE(
      absl::is_trivially_default_constructible<const TrivialCopyCtor*>::value);
  EXPECT_TRUE(
      absl::is_trivially_default_constructible<TrivialCopyCtor**>::value);

  // types with compiler generated default ctors
  EXPECT_TRUE(absl::is_trivially_default_constructible<Trivial>::value);
  EXPECT_TRUE(
      absl::is_trivially_default_constructible<TrivialDefaultCtor>::value);

#ifndef ABSL_TRIVIALLY_CONSTRUCTIBLE_VERIFY_TRIVIALLY_DESTRUCTIBLE
  // types with non trivial destructor are non trivial
  EXPECT_FALSE(
      absl::is_trivially_default_constructible<NonTrivialDestructor>::value);
#endif

  // types with vtables
  EXPECT_FALSE(absl::is_trivially_default_constructible<Base>::value);

  // Verify that arrays of such types are trivially default constructible
  typedef int int10[10];
  EXPECT_TRUE(absl::is_trivially_default_constructible<int10>::value);
  typedef Trivial Trivial10[10];
  EXPECT_TRUE(absl::is_trivially_default_constructible<Trivial10>::value);
  typedef Trivial TrivialDefaultCtor10[10];
  EXPECT_TRUE(
      absl::is_trivially_default_constructible<TrivialDefaultCtor10>::value);

  // Verify that std::pair has non-trivial constructors.
  EXPECT_FALSE(
      (absl::is_trivially_default_constructible<std::pair<int, char*>>::value));

  // Verify that types without trivial constructors are
  // correctly marked as such.
  EXPECT_FALSE(absl::is_trivially_default_constructible<std::string>::value);
  EXPECT_FALSE(
      absl::is_trivially_default_constructible<std::vector<int>>::value);
}

TEST(TypeTraitsTest, TestTrivialCopyCtor) {
  // Verify that arithmetic types and pointers have trivial copy
  // constructors.
  EXPECT_TRUE(absl::is_trivially_copy_constructible<bool>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<char>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<unsigned char>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<signed char>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<wchar_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<int>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<unsigned int>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<int16_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<uint16_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<int64_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<uint64_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<float>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<double>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<long double>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<std::string*>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<Trivial*>::value);
  EXPECT_TRUE(
      absl::is_trivially_copy_constructible<const TrivialCopyCtor*>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<TrivialCopyCtor**>::value);

  // types with compiler generated copy ctors
  EXPECT_TRUE(absl::is_trivially_copy_constructible<Trivial>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<TrivialCopyCtor>::value);

#ifndef ABSL_TRIVIALLY_CONSTRUCTIBLE_VERIFY_TRIVIALLY_DESTRUCTIBLE
  // type with non-trivial destructor are non-trivial copy construbtible
  EXPECT_FALSE(
      absl::is_trivially_copy_constructible<NonTrivialDestructor>::value);
#endif

  // types with vtables
  EXPECT_FALSE(absl::is_trivially_copy_constructible<Base>::value);

  // Verify that std pair of such types is trivially copy constructible
  EXPECT_TRUE(
      (absl::is_trivially_copy_constructible<std::pair<int, char*>>::value));
  EXPECT_TRUE(
      (absl::is_trivially_copy_constructible<std::pair<int, Trivial>>::value));
  EXPECT_TRUE((absl::is_trivially_copy_constructible<
               std::pair<int, TrivialCopyCtor>>::value));

  // Verify that arrays are not
  typedef int int10[10];
  EXPECT_FALSE(absl::is_trivially_copy_constructible<int10>::value);

  // Verify that pairs of types without trivial copy constructors
  // are not marked as trivial.
  EXPECT_FALSE((absl::is_trivially_copy_constructible<
                std::pair<int, std::string>>::value));
  EXPECT_FALSE((absl::is_trivially_copy_constructible<
                std::pair<std::string, int>>::value));

  // Verify that types without trivial copy constructors are
  // correctly marked as such.
  EXPECT_FALSE(absl::is_trivially_copy_constructible<std::string>::value);
  EXPECT_FALSE(absl::is_trivially_copy_constructible<std::vector<int>>::value);

  // types with deleted copy constructors are not copy constructible
  EXPECT_FALSE(absl::is_trivially_copy_constructible<NonCopyable>::value);
}

TEST(TypeTraitsTest, TestTrivialCopyAssign) {
  // Verify that arithmetic types and pointers have trivial copy
  // constructors.
  EXPECT_TRUE(absl::is_trivially_copy_assignable<bool>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<char>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<unsigned char>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<signed char>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<wchar_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<int>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<unsigned int>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<int16_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<uint16_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<int64_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<uint64_t>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<float>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<double>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<long double>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<std::string*>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<Trivial*>::value);
  EXPECT_TRUE(
      absl::is_trivially_copy_assignable<const TrivialCopyCtor*>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<TrivialCopyCtor**>::value);

  // const qualified types are not assignable
  EXPECT_FALSE(absl::is_trivially_copy_assignable<const int>::value);

  // types with compiler generated copy assignment
  EXPECT_TRUE(absl::is_trivially_copy_assignable<Trivial>::value);
  EXPECT_TRUE(absl::is_trivially_copy_assignable<TrivialCopyAssign>::value);

  // types with vtables
  EXPECT_FALSE(absl::is_trivially_copy_assignable<Base>::value);

  // Verify that arrays are not trivially copy assignable
  typedef int int10[10];
  EXPECT_FALSE(absl::is_trivially_copy_assignable<int10>::value);

  // Verify that std::pair is not trivially assignable
  EXPECT_FALSE(
      (absl::is_trivially_copy_assignable<std::pair<int, char*>>::value));

  // Verify that types without trivial copy constructors are
  // correctly marked as such.
  EXPECT_FALSE(absl::is_trivially_copy_assignable<std::string>::value);
  EXPECT_FALSE(absl::is_trivially_copy_assignable<std::vector<int>>::value);

  // types with deleted copy assignment are not copy assignable
  EXPECT_FALSE(absl::is_trivially_copy_assignable<NonCopyable>::value);
}

TEST(TypeTraitsTest, TestTrivialDestructor) {
  // Verify that arithmetic types and pointers have trivial copy
  // constructors.
  EXPECT_TRUE(absl::is_trivially_destructible<bool>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<char>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<unsigned char>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<signed char>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<wchar_t>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<int>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<unsigned int>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<int16_t>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<uint16_t>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<int64_t>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<uint64_t>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<float>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<double>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<long double>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<std::string*>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<Trivial*>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<const TrivialCopyCtor*>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<TrivialCopyCtor**>::value);

  // classes with destructors
  EXPECT_TRUE(absl::is_trivially_destructible<Trivial>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<TrivialDestructor>::value);
  EXPECT_FALSE(absl::is_trivially_destructible<NonTrivialDestructor>::value);

  // std::pair of such types is trivial
  EXPECT_TRUE((absl::is_trivially_destructible<std::pair<int, int>>::value));
  EXPECT_TRUE((absl::is_trivially_destructible<
               std::pair<Trivial, TrivialDestructor>>::value));

  // array of such types is trivial
  typedef int int10[10];
  EXPECT_TRUE(absl::is_trivially_destructible<int10>::value);
  typedef TrivialDestructor TrivialDestructor10[10];
  EXPECT_TRUE(absl::is_trivially_destructible<TrivialDestructor10>::value);
  typedef NonTrivialDestructor NonTrivialDestructor10[10];
  EXPECT_FALSE(absl::is_trivially_destructible<NonTrivialDestructor10>::value);
}

#define ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(trait_name, ...)          \
  EXPECT_TRUE((std::is_same<typename std::trait_name<__VA_ARGS__>::type, \
                            absl::trait_name##_t<__VA_ARGS__>>::value))

TEST(TypeTraitsTest, TestRemoveCVAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_cv, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_cv, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_cv, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_cv, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_const, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_const, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_const, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_const, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_volatile, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_volatile, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_volatile, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_volatile, const volatile int);
}

TEST(TypeTraitsTest, TestAddCVAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_cv, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_cv, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_cv, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_cv, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_const, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_const, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_const, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_const, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_volatile, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_volatile, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_volatile, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_volatile, const volatile int);
}

TEST(TypeTraitsTest, TestReferenceAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, int&&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, volatile int&&);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, int&&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, volatile int&&);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, int&&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, volatile int&&);
}

TEST(TypeTraitsTest, TestPointerAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_pointer, int*);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_pointer, volatile int*);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_pointer, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_pointer, volatile int);
}

TEST(TypeTraitsTest, TestSignednessAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_signed, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_signed, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_signed, unsigned);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_signed, volatile unsigned);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_unsigned, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_unsigned, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_unsigned, unsigned);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_unsigned, volatile unsigned);
}

TEST(TypeTraitsTest, TestExtentAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_extent, int[]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_extent, int[1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_extent, int[1][1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_extent, int[][1]);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_all_extents, int[]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_all_extents, int[1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_all_extents, int[1][1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_all_extents, int[][1]);
}

TEST(TypeTraitsTest, TestAlignedStorageAlias) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 1);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 2);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 3);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 4);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 5);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 6);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 7);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 8);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 9);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 10);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 11);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 12);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 13);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 14);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 15);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 16);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 17);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 18);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 19);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 20);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 21);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 22);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 23);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 24);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 25);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 26);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 27);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 28);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 29);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 30);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 31);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 32);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 33);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 1, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 2, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 3, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 4, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 5, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 6, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 7, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 8, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 9, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 10, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 11, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 12, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 13, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 14, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 15, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 16, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 17, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 18, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 19, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 20, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 21, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 22, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 23, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 24, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 25, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 26, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 27, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 28, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 29, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 30, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 31, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 32, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 33, 128);
}

TEST(TypeTraitsTest, TestDecay) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const volatile int&);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const volatile int&);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int[1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int[1][1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int[][1]);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int());
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int(float));
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int(char, ...));
}

struct TypeA {};
struct TypeB {};
struct TypeC {};
struct TypeD {};

template <typename T>
struct Wrap {};

enum class TypeEnum { A, B, C, D };

struct GetTypeT {
  template <typename T,
            absl::enable_if_t<std::is_same<T, TypeA>::value, int> = 0>
  TypeEnum operator()(Wrap<T>) const {
    return TypeEnum::A;
  }

  template <typename T,
            absl::enable_if_t<std::is_same<T, TypeB>::value, int> = 0>
  TypeEnum operator()(Wrap<T>) const {
    return TypeEnum::B;
  }

  template <typename T,
            absl::enable_if_t<std::is_same<T, TypeC>::value, int> = 0>
  TypeEnum operator()(Wrap<T>) const {
    return TypeEnum::C;
  }

  // NOTE: TypeD is intentionally not handled
} constexpr GetType = {};

TEST(TypeTraitsTest, TestEnableIf) {
  EXPECT_EQ(TypeEnum::A, GetType(Wrap<TypeA>()));
  EXPECT_EQ(TypeEnum::B, GetType(Wrap<TypeB>()));
  EXPECT_EQ(TypeEnum::C, GetType(Wrap<TypeC>()));
}

TEST(TypeTraitsTest, TestConditional) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(conditional, true, int, char);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(conditional, false, int, char);
}

// TODO(calabrese) Check with specialized std::common_type
TEST(TypeTraitsTest, TestCommonType) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int, char);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int, char, int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int, char&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int, char, int&);
}

TEST(TypeTraitsTest, TestUnderlyingType) {
  enum class enum_char : char {};
  enum class enum_long_long : long long {};  // NOLINT(runtime/int)

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(underlying_type, enum_char);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(underlying_type, enum_long_long);
}

struct GetTypeExtT {
  template <typename T>
  absl::result_of_t<const GetTypeT&(T)> operator()(T&& arg) const {
    return GetType(std::forward<T>(arg));
  }

  TypeEnum operator()(Wrap<TypeD>) const { return TypeEnum::D; }
} constexpr GetTypeExt = {};

TEST(TypeTraitsTest, TestResultOf) {
  EXPECT_EQ(TypeEnum::A, GetTypeExt(Wrap<TypeA>()));
  EXPECT_EQ(TypeEnum::B, GetTypeExt(Wrap<TypeB>()));
  EXPECT_EQ(TypeEnum::C, GetTypeExt(Wrap<TypeC>()));
  EXPECT_EQ(TypeEnum::D, GetTypeExt(Wrap<TypeD>()));
}

}  // namespace
