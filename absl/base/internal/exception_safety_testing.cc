#include "absl/base/internal/exception_safety_testing.h"

#include "gtest/gtest.h"
#include "absl/meta/type_traits.h"

namespace absl {
exceptions_internal::NoThrowTag no_throw_ctor;
namespace exceptions_internal {

int countdown = -1;

void MaybeThrow(absl::string_view msg) {
  if (countdown-- == 0) throw TestException(msg);
}

testing::AssertionResult FailureMessage(const TestException& e,
                                        int countdown) noexcept {
  return testing::AssertionFailure() << "Exception thrown from " << e.what();
}
}  // namespace exceptions_internal
}  // namespace absl
