// Copyright 2026 The Abseil Authors.
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

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(ABSL_CI_EXPECT_COMPILER_CLANG)
#if !defined(__clang__) || defined(_MSC_VER)
#error "Expected Clang, but __clang__ is not defined or _MSC_VER is defined"
#endif
#endif

#if defined(ABSL_CI_EXPECT_COMPILER_GCC)
#if !defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#error "Expected GCC, but __clang__ or _MSC_VER is defined or __GNUC__ is not"
#endif
#endif

#if defined(ABSL_CI_EXPECT_COMPILER_MSVC)
#if !defined(_MSC_VER) || defined(__clang__)
#error "Expected MSVC, but _MSC_VER is not defined or __clang__ is defined"
#endif
#endif

#if defined(ABSL_CI_EXPECT_COMPILER_CLANG_CL)
#if !defined(_MSC_VER) || !defined(__clang__)
#error "Expected clang-cl compiler, but _MSC_VER or __clang__ is not defined"
#endif
#endif

#if defined(ABSL_CI_EXPECT_STDLIB_LIBCXX)
#if !defined(_LIBCPP_VERSION) || defined(__GLIBCXX__) || \
    defined(_MSVC_STL_UPDATE)
#error "Expected libc++, but _LIBCPP_VERSION is not defined"
#endif
#endif

#if defined(ABSL_CI_EXPECT_STDLIB_LIBSTDCXX)
#if !defined(__GLIBCXX__) || defined(_LIBCPP_VERSION) || \
    defined(_MSVC_STL_UPDATE)
#error "Expected libstdc++, but __GLIBCXX__ is not defined"
#endif
#endif

#if defined(ABSL_CI_EXPECT_STDLIB_MSVC)
#if !defined(_MSVC_STL_UPDATE) || defined(_LIBCPP_VERSION) || \
    defined(__GLIBCXX__)
#error "Expected MSVC STL, but _MSVC_STL_UPDATE is not defined"
#endif
#endif

#if defined(ABSL_CI_EXPECT_EXCEPTIONS_ENABLED)
#if !defined(__EXCEPTIONS) && !defined(_CPPUNWIND)
#error "Expected C++ exceptions, but __EXCEPTIONS and _CPPUNWIND are undefined"
#endif
#endif

#if defined(ABSL_CI_EXPECT_EXCEPTIONS_DISABLED)
#if defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#error "Expected no C++ exceptions, but __EXCEPTIONS or _CPPUNWIND is defined"
#endif
#endif

#if defined(ABSL_CI_EXPECT_SANITIZER_ASAN)
#if !defined(__SANITIZE_ADDRESS__)
#if !defined(__has_feature) || !__has_feature(address_sanitizer)
#error "Expected AddressSanitizer to be enabled"
#endif
#endif
#endif

#if defined(ABSL_CI_EXPECT_SANITIZER_TSAN)
#if !defined(__SANITIZE_THREAD__)
#if !defined(__has_feature) || !__has_feature(thread_sanitizer)
#error "Expected ThreadSanitizer to be enabled"
#endif
#endif
#endif

#if defined(ABSL_CI_EXPECT_SANITIZER_MSAN)
#if !defined(__SANITIZE_MEMORY__)
#if !defined(__has_feature) || !__has_feature(memory_sanitizer)
#error "Expected MemorySanitizer to be enabled"
#endif
#endif
#endif

namespace {

// Prevent the compiler from optimizing away memory access or sanitizer
// instrumentation.
#if defined(_MSC_VER) && !defined(__clang__)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void TouchMemory(int* ptr) {
  volatile int* vptr = ptr;
  *vptr = 42;
}

}  // namespace

int main(int argc, char** /*argv*/) {
  // 1. Long string to bypass Short String Optimization (SSO) and exercise
  // stdlib heap.
  std::string s =
      "abseil toolchain test with long string to bypass SSO allocation limit";
  std::cout << s << '\n';

  // 2. Dynamic heap allocation and pointer access to ensure ASAN/MSAN/TSAN
  // hooks.
  auto vec = std::make_unique<std::vector<int>>(100, 7);
  TouchMemory(vec->data());

  // 3. Volatile indexing and arithmetic to ensure UBSAN checks are retained.
  volatile std::size_t idx = argc > 0 ? 0 : 1;
  (*vec)[idx] += 1;

#if defined(ABSL_CI_EXPECT_EXCEPTIONS_ENABLED)
  bool caught = false;
  try {
    throw 42;
  } catch (int v) {
    caught = (v == 42);
  }
  if (!caught) {
    return 1;
  }
#endif

  return 0;
}
