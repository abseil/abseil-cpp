// Testing utilities for ABSL types which throw exceptions.

#ifndef ABSL_BASE_INTERNAL_EXCEPTION_TESTING_H_
#define ABSL_BASE_INTERNAL_EXCEPTION_TESTING_H_

#include "gtest/gtest.h"
#include "absl/base/config.h"

// ABSL_BASE_INTERNAL_EXPECT_FAIL tests either for a specified thrown exception
// if exceptions are enabled, or for death with a specified text in the error
// message
#ifdef ABSL_HAVE_EXCEPTIONS

#define ABSL_BASE_INTERNAL_EXPECT_FAIL(expr, exception_t, text) \
  EXPECT_THROW(expr, exception_t)

#else

#define ABSL_BASE_INTERNAL_EXPECT_FAIL(expr, exception_t, text) \
  EXPECT_DEATH(expr, text)

#endif

#endif  // ABSL_BASE_INTERNAL_EXCEPTION_TESTING_H_
