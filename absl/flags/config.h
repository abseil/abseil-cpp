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

#ifndef ABSL_FLAGS_CONFIG_H_
#define ABSL_FLAGS_CONFIG_H_

// ABSL_FLAGS_STRIP_NAMES
//
// This macro controls whether flag registration is enabled. Despite its name
// (which refers to string literals being stripped), this macro has a broader
// effect: when ABSL_FLAGS_STRIP_NAMES is 1, flag registration is DISABLED.
//
// BEHAVIOR WHEN ABSL_FLAGS_STRIP_NAMES = 1:
//   - Flag names, types, and help text are stripped from the binary (saves size)
//   - Flag registration is DISABLED, so flags cannot be parsed from
//     command-line arguments
//   - absl::ParseCommandLine() will not recognize any ABSL_FLAG definitions
//   - Use absl::GetFlag() and absl::SetFlag() directly in code instead
//   - Calling ParseCommandLine() will print "ERROR: Unknown command line flag"
//     for any flags
//
// BEHAVIOR WHEN ABSL_FLAGS_STRIP_NAMES = 0:
//   - Flag names, types, and help text are included in the binary
//   - Flag registration is ENABLED, so flags can be parsed from command-line
//   - absl::ParseCommandLine() works as expected
//   - All standard flag functionality is available
//
// MOBILE PLATFORMS (DEFAULT):
// By default, this macro is set to 1 on mobile platforms (Android, iPhone,
// and embedded Apple devices) for binary size optimization, since mobile
// platforms typically don't use command-line argument passing. However, some
// applications (e.g., frameworks running on iOS) may want to use command-line
// flags for configuration.
//
// IF YOU NEED TO USE FLAGS ON MOBILE:
// If you need to use absl::ParseCommandLine() on iOS, Android, or other
// mobile platforms, you MUST explicitly set ABSL_FLAGS_STRIP_NAMES=0 when
// building. Examples:
//
//   For Bazel:
//     bazel build --define=ABSL_FLAGS_STRIP_NAMES=0 //your/target
//
//   For CMake:
//     cmake -DABSL_FLAGS_STRIP_NAMES=0 -B build
//
//   For direct compilation:
//     g++ -DABSL_FLAGS_STRIP_NAMES=0 your_file.cc
//
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
  A(double, double)                          \
  A(float, float)

#define ABSL_FLAGS_INTERNAL_SUPPORTED_TYPES(A) \
  ABSL_FLAGS_INTERNAL_BUILTIN_TYPES(A)         \
  A(std::string, std_string)                   \
  A(std::vector<std::string>, std_vector_of_string)

#endif  // ABSL_FLAGS_CONFIG_H_
