//
//  Copyright 2019 The Abseil Authors.
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

// SKIP_ABSL_INLINE_NAMESPACE_CHECK

#ifndef ABSL_FLAGS_CONFIG_H_
#define ABSL_FLAGS_CONFIG_H_

// Determine if we should strip string literals from the Flag objects.
// By default we strip string literals on mobile platforms.
#if !defined(ABSL_FLAGS_STRIP_NAMES)

#if defined(__ANDROID__)
#define ABSL_FLAGS_STRIP_NAMES 1

#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define ABSL_FLAGS_STRIP_NAMES 1
#elif defined(TARGET_OS_EMBEDDED) && TARGET_OS_EMBEDDED
#define ABSL_FLAGS_STRIP_NAMES 1
#endif  // TARGET_OS_*
#endif

#endif  // !defined(ABSL_FLAGS_STRIP_NAMES)

#if !defined(ABSL_FLAGS_STRIP_NAMES)
// If ABSL_FLAGS_STRIP_NAMES wasn't set on the command line or above,
// the default is not to strip.
#define ABSL_FLAGS_STRIP_NAMES 0
#endif

#if !defined(ABSL_FLAGS_STRIP_HELP)
// By default, if we strip names, we also strip help.
#define ABSL_FLAGS_STRIP_HELP ABSL_FLAGS_STRIP_NAMES
#endif

// These macros represent the "source of truth" for the list of supported
// built-in types.
#define ABSL_FLAGS_INTERNAL_BUILTIN_TYPES(A) \
  A(bool, bool)                              \
  A(short, short)                            \
  A(unsigned short, unsigned_short)          \
  A(int, int)                                \
  A(unsigned int, unsigned_int)              \
  A(long, long)                              \
  A(unsigned long, unsigned_long)            \
  A(long long, long_long)                    \
  A(unsigned long long, unsigned_long_long)  \
  A(float, float)                            \
  A(double, double)

#define ABSL_FLAGS_INTERNAL_SUPPORTED_TYPES(A)                     \
  ABSL_FLAGS_INTERNAL_BUILTIN_TYPES(A)                             \
  A(absl::int128, absl_int128)                                     \
  A(absl::uint128, absl_uint128)                                   \
  A(std::string, std_string)                                       \
  A(std::vector<short>, std_vector_of_short) /* NOLINT */          \
  A(std::vector<unsigned short>, /* NOLINT */                      \
    std_vector_of_unsigned_short)                                  \
  A(std::vector<int>, std_vector_of_int)                           \
  A(std::vector<unsigned int>, std_vector_of_unsigned_int)         \
  A(std::vector<long>, std_vector_of_long) /* NOLINT */            \
  A(std::vector<unsigned long>, /* NOLINT */                       \
    std_vector_of_unsigned_long)                                   \
  A(std::vector<long long>, std_vector_of_long_long) /* NOLINT */  \
  A(std::vector<unsigned long long>, /* NOLINT */                  \
    std_vector_of_unsigned_long_long)                              \
  A(std::vector<absl::int128>, std_vector_of_absl_int128)          \
  A(std::vector<absl::uint128>, std_vector_of_absl_uint128)        \
  A(std::vector<float>, std_vector_of_float)                       \
  A(std::vector<double>, std_vector_of_double)                     \
  A(std::vector<std::string>, std_vector_of_string)

#endif  // ABSL_FLAGS_CONFIG_H_
