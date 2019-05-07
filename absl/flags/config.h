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

// Determine if we should strip string literals from the Flag objects.
#if !defined(ABSL_FLAGS_STRIP_NAMES)

// Non-mobile linux platforms don't strip string literals.
#if (defined(__linux__) || defined(__Fuchsia__)) && !defined(__ANDROID__)
#define ABSL_FLAGS_STRIP_NAMES 0

// So do Macs (not iOS or embedded Apple platforms).
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE && !TARGET_OS_EMBEDDED
#define ABSL_FLAGS_STRIP_NAMES 0
#endif

// And Windows.
#elif defined(_WIN32)
#define ABSL_FLAGS_STRIP_NAMES 0

// And Myriad.
#elif defined(__myriad2__)
#define ABSL_FLAGS_STRIP_NAMES 0

#endif
#endif  // !defined(ABSL_FLAGS_STRIP_NAMES)

#if ABSL_FLAGS_STRIP_NAMES
#if !defined(ABSL_FLAGS_STRIP_HELP)
#define ABSL_FLAGS_STRIP_HELP 1
#endif
#else
#if !defined(ABSL_FLAGS_STRIP_HELP)
#define ABSL_FLAGS_STRIP_HELP 0
#endif
#endif

#endif  // ABSL_FLAGS_CONFIG_H_
