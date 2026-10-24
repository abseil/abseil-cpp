// Copyright 2025 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/attributes.h"

#include <cstddef>
#include <type_traits>

#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace {

TEST(Attributes, RequireExplicitInit) {
  struct Agg {
    int f1;
    int f2 ABSL_REQUIRE_EXPLICIT_INIT;
  };
  Agg good1 [[maybe_unused]] = {1, 2};
#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L
  Agg good2 [[maybe_unused]] (1, 2);
#endif
  Agg good3 [[maybe_unused]]{1, 2};
  Agg good4 [[maybe_unused]] = {1, 2};
  Agg good5 [[maybe_unused]] = Agg{1, 2};
  Agg good6 [[maybe_unused]][1] = {{1, 2}};
  Agg good7 [[maybe_unused]][1] = {Agg{1, 2}};
  union {
    Agg agg;
  } good8 [[maybe_unused]] = {{1, 2}};
  constexpr Agg good9 [[maybe_unused]] = {1, 2};
  constexpr Agg good10 [[maybe_unused]]{1, 2};
}

TEST(Attributes, MSVCBug) {
  struct ImplicitlyConstructible {
    // NOLINTNEXTLINE(google-explicit-constructor)
    ImplicitlyConstructible(const char*) {}
  };
  struct Agg {
    ImplicitlyConstructible f1 ABSL_REQUIRE_EXPLICIT_INIT;
  };
  static_assert(std::is_convertible_v<const char*, ImplicitlyConstructible>);
  Agg good1 [[maybe_unused]] = {ImplicitlyConstructible("hello")};
}

TEST(Attributes, UnsafeBuffers) {
  int buffer[10] = {0};
  int offset = 1;

  // Expression example
  int* p1 = ABSL_UNSAFE_BUFFERS(buffer + offset);
  EXPECT_EQ(p1, &buffer[1]);

  // Single-line statement example
  int* p2 = buffer;
  ABSL_UNSAFE_BUFFERS(p2 += offset;)
  EXPECT_EQ(p2, &buffer[1]);

  // Multi-line statement example
  ABSL_UNSAFE_BUFFERS(int* p3 = buffer + offset; p3[0] = 1;)
  EXPECT_EQ(buffer[1], 1);
}

TEST(Attributes, UnsafeBufferUsageInContainer) {
  struct IntSpan {
    ABSL_ATTRIBUTE_UNSAFE_BUFFER_USAGE_IN_CONTAINER
    IntSpan(int* p, size_t n) : ptr(p), size(n) {}

    int* ptr;
    size_t size;
  };
  int buffer[3] = {0};
  IntSpan span(buffer, 3);
  EXPECT_EQ(span.ptr, buffer);
  EXPECT_EQ(span.size, 3u);
}

}  // namespace
