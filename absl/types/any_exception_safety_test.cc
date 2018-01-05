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

#include "absl/types/any.h"

#include <typeinfo>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/internal/exception_safety_testing.h"

using Thrower = absl::ThrowingValue<>;
using ThrowerList = std::initializer_list<Thrower>;
using ThrowerVec = std::vector<Thrower>;
using ThrowingAlloc = absl::ThrowingAllocator<Thrower>;
using ThrowingThrowerVec = std::vector<Thrower, ThrowingAlloc>;

namespace absl {

testing::AssertionResult AbslCheckInvariants(absl::any* a,
                                             InternalAbslNamespaceFinder) {
  using testing::AssertionFailure;
  using testing::AssertionSuccess;

  if (a->has_value()) {
    if (a->type() == typeid(void)) {
      return AssertionFailure()
             << "A non-empty any should not have type `void`";
    }
  } else {
    if (a->type() != typeid(void)) {
      return AssertionFailure()
             << "An empty any should have type void, but has type "
             << a->type().name();
    }
  }

  //  Make sure that reset() changes any to a valid state.
  a->reset();
  if (a->has_value()) {
    return AssertionFailure() << "A reset `any` should be valueless";
  }
  if (a->type() != typeid(void)) {
    return AssertionFailure() << "A reset `any` should have type() of `void`, "
                                 "but instead has type "
                              << a->type().name();
  }
  try {
    auto unused = absl::any_cast<Thrower>(*a);
    static_cast<void>(unused);
    return AssertionFailure()
           << "A reset `any` should not be able to be any_cast";
  } catch (absl::bad_any_cast) {
  } catch (...) {
    return AssertionFailure()
           << "Unexpected exception thrown from absl::any_cast";
  }
  return AssertionSuccess();
}

}  // namespace absl

namespace {

class AnyExceptionSafety : public ::testing::Test {
 private:
  absl::AllocInspector inspector_;
};

testing::AssertionResult AnyIsEmpty(absl::any* a) {
  if (!a->has_value()) return testing::AssertionSuccess();
  return testing::AssertionFailure()
         << "a should be empty, but instead has value "
         << absl::any_cast<Thrower>(*a).Get();
}

TEST_F(AnyExceptionSafety, Ctors) {
  Thrower val(1);
  auto with_val = absl::TestThrowingCtor<absl::any>(val);
  auto copy = absl::TestThrowingCtor<absl::any>(with_val);
  auto in_place =
      absl::TestThrowingCtor<absl::any>(absl::in_place_type_t<Thrower>(), 1);
  auto in_place_list = absl::TestThrowingCtor<absl::any>(
      absl::in_place_type_t<ThrowerVec>(), ThrowerList{val});
  auto in_place_list_again =
      absl::TestThrowingCtor<absl::any,
                             absl::in_place_type_t<ThrowingThrowerVec>,
                             ThrowerList, ThrowingAlloc>(
          absl::in_place_type_t<ThrowingThrowerVec>(), {val}, ThrowingAlloc());
}

struct OneFactory {
  std::unique_ptr<absl::any> operator()() const {
    return absl::make_unique<absl::any>(absl::in_place_type_t<Thrower>(), 1,
                                        absl::no_throw_ctor);
  }
};

struct EmptyFactory {
  std::unique_ptr<absl::any> operator()() const {
    return absl::make_unique<absl::any>();
  }
};

TEST_F(AnyExceptionSafety, Assignment) {
  auto thrower_comp = [](const absl::any& l, const absl::any& r) {
    return absl::any_cast<Thrower>(l) == absl::any_cast<Thrower>(r);
  };

  OneFactory one_factory;

  absl::ThrowingValue<absl::NoThrow::kMoveCtor | absl::NoThrow::kMoveAssign>
      moveable_val(2);
  Thrower val(2);
  absl::any any_val(val);

  EXPECT_TRUE(absl::TestExceptionSafety(
      one_factory, [&any_val](absl::any* ap) { *ap = any_val; },
      absl::StrongGuarantee(one_factory, thrower_comp)));

  EXPECT_TRUE(absl::TestExceptionSafety(
      one_factory, [&val](absl::any* ap) { *ap = val; },
      absl::StrongGuarantee(one_factory, thrower_comp)));

  EXPECT_TRUE(absl::TestExceptionSafety(
      one_factory, [&val](absl::any* ap) { *ap = std::move(val); },
      absl::StrongGuarantee(one_factory, thrower_comp)));

  EXPECT_TRUE(absl::TestExceptionSafety(
      one_factory,
      [&moveable_val](absl::any* ap) { *ap = std::move(moveable_val); },
      absl::StrongGuarantee(one_factory, thrower_comp)));

  EmptyFactory empty_factory;
  auto empty_comp = [](const absl::any& l, const absl::any& r) {
    return !(l.has_value() || r.has_value());
  };

  EXPECT_TRUE(absl::TestExceptionSafety(
      empty_factory, [&any_val](absl::any* ap) { *ap = any_val; },
      absl::StrongGuarantee(empty_factory, empty_comp)));

  EXPECT_TRUE(absl::TestExceptionSafety(
      empty_factory, [&val](absl::any* ap) { *ap = val; },
      absl::StrongGuarantee(empty_factory, empty_comp)));

  EXPECT_TRUE(absl::TestExceptionSafety(
      empty_factory, [&val](absl::any* ap) { *ap = std::move(val); },
      absl::StrongGuarantee(empty_factory, empty_comp)));
}
// libstdc++ std::any fails this test
#if !defined(ABSL_HAVE_STD_ANY)
TEST_F(AnyExceptionSafety, Emplace) {
  OneFactory one_factory;

  EXPECT_TRUE(absl::TestExceptionSafety(
      one_factory, [](absl::any* ap) { ap->emplace<Thrower>(2); }, AnyIsEmpty));

  EXPECT_TRUE(absl::TestExceptionSafety(
      one_factory,
      [](absl::any* ap) {
        ap->emplace<absl::ThrowingValue<absl::NoThrow::kMoveCtor |
                                        absl::NoThrow::kMoveAssign>>(2);
      },
      AnyIsEmpty));

  EXPECT_TRUE(absl::TestExceptionSafety(one_factory,
                                        [](absl::any* ap) {
                                          std::initializer_list<Thrower> il{
                                              Thrower(2, absl::no_throw_ctor)};
                                          ap->emplace<ThrowerVec>(il);
                                        },
                                        AnyIsEmpty));

  EmptyFactory empty_factory;
  EXPECT_TRUE(absl::TestExceptionSafety(
      empty_factory, [](absl::any* ap) { ap->emplace<Thrower>(2); },
      AnyIsEmpty));

  EXPECT_TRUE(absl::TestExceptionSafety(empty_factory,
                                        [](absl::any* ap) {
                                          std::initializer_list<Thrower> il{
                                              Thrower(2, absl::no_throw_ctor)};
                                          ap->emplace<ThrowerVec>(il);
                                        },
                                        AnyIsEmpty));
}
#endif  // ABSL_HAVE_STD_ANY

}  // namespace
