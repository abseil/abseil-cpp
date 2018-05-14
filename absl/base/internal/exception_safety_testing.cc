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

#include "gtest/gtest.h"
#include "absl/meta/type_traits.h"

namespace testing {

exceptions_internal::NoThrowTag nothrow_ctor;

bool nothrow_guarantee(const void*) {
  return ::testing::AssertionFailure()
         << "Exception thrown violating NoThrow Guarantee";
}
exceptions_internal::StrongGuaranteeTagType strong_guarantee;

namespace exceptions_internal {

int countdown = -1;

void MaybeThrow(absl::string_view msg, bool throw_bad_alloc) {
  if (countdown-- == 0) {
    if (throw_bad_alloc) throw TestBadAllocException(msg);
    throw TestException(msg);
  }
}

testing::AssertionResult FailureMessage(const TestException& e,
                                        int countdown) noexcept {
  return testing::AssertionFailure() << "Exception thrown from " << e.what();
}

}  // namespace exceptions_internal

}  // namespace testing
