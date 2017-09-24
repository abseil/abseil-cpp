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

 * Defines ABSL_STACKTRACE_INL_HEADER to the *-inl.h containing
 * actual unwinder implementation.
 * This header is "private" to stacktrace.cc.
 * DO NOT include it into any other files.
*/
#ifndef ABSL_DEBUGGING_INTERNAL_STACKTRACE_CONFIG_H_
#define ABSL_DEBUGGING_INTERNAL_STACKTRACE_CONFIG_H_

// First, test platforms which only support a stub.
#if ABSL_STACKTRACE_INL_HEADER
#error ABSL_STACKTRACE_INL_HEADER cannot be directly set
#elif defined(__native_client__) || defined(__APPLE__) || \
    defined(__ANDROID__) || defined(__myriad2__) || defined(asmjs__) || \
    defined(__Fuchsia__)
#define ABSL_STACKTRACE_INL_HEADER \
    "absl/debugging/internal/stacktrace_unimplemented-inl.inc"

// Next, test for Mips and Windows.
// TODO(marmstrong): Mips case, remove the check for ABSL_STACKTRACE_INL_HEADER
#elif defined(__mips__) && !defined(ABSL_STACKTRACE_INL_HEADER)
#define ABSL_STACKTRACE_INL_HEADER \
    "absl/debugging/internal/stacktrace_unimplemented-inl.inc"
#elif defined(_WIN32)  // windows
#define ABSL_STACKTRACE_INL_HEADER \
    "absl/debugging/internal/stacktrace_win32-inl.inc"

// Finally, test NO_FRAME_POINTER.
#elif !defined(NO_FRAME_POINTER)
# if defined(__i386__) || defined(__x86_64__)
#define ABSL_STACKTRACE_INL_HEADER \
    "absl/debugging/internal/stacktrace_x86-inl.inc"
# elif defined(__ppc__) || defined(__PPC__)
#define ABSL_STACKTRACE_INL_HEADER \
    "absl/debugging/internal/stacktrace_powerpc-inl.inc"
# elif defined(__aarch64__)
#define ABSL_STACKTRACE_INL_HEADER \
    "absl/debugging/internal/stacktrace_aarch64-inl.inc"
# elif defined(__arm__)
#define ABSL_STACKTRACE_INL_HEADER \
    "absl/debugging/internal/stacktrace_arm-inl.inc"
# endif
#else  // defined(NO_FRAME_POINTER)
# if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
#define ABSL_STACKTRACE_INL_HEADER \
    "absl/debugging/internal/stacktrace_unimplemented-inl.inc"
# elif defined(__ppc__) || defined(__PPC__)
//  Use glibc's backtrace.
#define ABSL_STACKTRACE_INL_HEADER \
    "absl/debugging/internal/stacktrace_generic-inl.inc"
# elif defined(__arm__)
#   error stacktrace without frame pointer is not supported on ARM
# endif
#endif  // NO_FRAME_POINTER

#if !defined(ABSL_STACKTRACE_INL_HEADER)
#error Not supported yet
#endif

#endif  // ABSL_DEBUGGING_INTERNAL_STACKTRACE_CONFIG_H_
