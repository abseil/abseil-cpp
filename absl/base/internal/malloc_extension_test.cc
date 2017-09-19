/*
 * Copyright 2017 The Abseil Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cstdlib>

#include "gtest/gtest.h"
#include "absl/base/internal/malloc_extension.h"
#include "absl/base/internal/malloc_extension_c.h"

namespace absl {
namespace base_internal {
namespace {

TEST(MallocExtension, MallocExtension) {
  void* a = malloc(1000);

  size_t cxx_bytes_used, c_bytes_used;
  if (!MallocExtension::instance()->GetNumericProperty(
          "generic.current_allocated_bytes", &cxx_bytes_used)) {
    EXPECT_TRUE(ABSL_MALLOC_EXTENSION_TEST_ALLOW_MISSING_EXTENSION);
  } else {
    ASSERT_TRUE(MallocExtension::instance()->GetNumericProperty(
        "generic.current_allocated_bytes", &cxx_bytes_used));
    ASSERT_TRUE(MallocExtension_GetNumericProperty(
        "generic.current_allocated_bytes", &c_bytes_used));
#ifndef MEMORY_SANITIZER
    EXPECT_GT(cxx_bytes_used, 1000);
    EXPECT_GT(c_bytes_used, 1000);
#endif

    EXPECT_TRUE(MallocExtension::instance()->VerifyAllMemory());
    EXPECT_TRUE(MallocExtension_VerifyAllMemory());

    EXPECT_EQ(MallocExtension::kOwned,
              MallocExtension::instance()->GetOwnership(a));
    // TODO(csilvers): this relies on undocumented behavior that
    // GetOwnership works on stack-allocated variables.  Use a better test.
    EXPECT_EQ(MallocExtension::kNotOwned,
              MallocExtension::instance()->GetOwnership(&cxx_bytes_used));
    EXPECT_EQ(MallocExtension::kNotOwned,
              MallocExtension::instance()->GetOwnership(nullptr));
    EXPECT_GE(MallocExtension::instance()->GetAllocatedSize(a), 1000);
    // This is just a sanity check.  If we allocated too much, tcmalloc is
    // broken
    EXPECT_LE(MallocExtension::instance()->GetAllocatedSize(a), 5000);
    EXPECT_GE(MallocExtension::instance()->GetEstimatedAllocatedSize(1000),
              1000);
    for (int i = 0; i < 10; ++i) {
      void* p = malloc(i);
      EXPECT_GE(MallocExtension::instance()->GetAllocatedSize(p),
                MallocExtension::instance()->GetEstimatedAllocatedSize(i));
      free(p);
    }

    // Check the c-shim version too.
    EXPECT_EQ(MallocExtension_kOwned, MallocExtension_GetOwnership(a));
    EXPECT_EQ(MallocExtension_kNotOwned,
              MallocExtension_GetOwnership(&cxx_bytes_used));
    EXPECT_EQ(MallocExtension_kNotOwned, MallocExtension_GetOwnership(nullptr));
    EXPECT_GE(MallocExtension_GetAllocatedSize(a), 1000);
    EXPECT_LE(MallocExtension_GetAllocatedSize(a), 5000);
    EXPECT_GE(MallocExtension_GetEstimatedAllocatedSize(1000), 1000);
  }

  free(a);
}

// Verify that the .cc file and .h file have the same enum values.
TEST(GetOwnership, EnumValuesEqualForCAndCXX) {
  EXPECT_EQ(static_cast<int>(MallocExtension::kUnknownOwnership),
            static_cast<int>(MallocExtension_kUnknownOwnership));
  EXPECT_EQ(static_cast<int>(MallocExtension::kOwned),
            static_cast<int>(MallocExtension_kOwned));
  EXPECT_EQ(static_cast<int>(MallocExtension::kNotOwned),
            static_cast<int>(MallocExtension_kNotOwned));
}

TEST(nallocx, SaneBehavior) {
  for (size_t size = 0; size < 64 * 1024; ++size) {
    size_t alloc_size = nallocx(size, 0);
    EXPECT_LE(size, alloc_size) << "size is " << size;
    EXPECT_LE(alloc_size, std::max(size + 100, 2 * size)) << "size is " << size;
  }
}

}  // namespace
}  // namespace base_internal
}  // namespace absl
