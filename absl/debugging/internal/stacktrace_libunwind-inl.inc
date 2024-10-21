// Copyright 2017 The Abseil Authors.
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
//
// Min viable libunwind stacktrace implementation. Mostly based on the original
// glog implementation and absl/debugging/internal/stacktrace_generic-inl.inc.

#ifndef ABSL_DEBUGGING_INTERNAL_STACKTRACE_LIBUNWIND_INL_H_
#define ABSL_DEBUGGING_INTERNAL_STACKTRACE_LIBUNWIND_INL_H_

extern "C" {
#define UNW_LOCAL_ONLY
#include <libunwind.h>
}

#include <execinfo.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <unwind.h>

#include "absl/debugging/stacktrace.h"
#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"

// Sometimes, we can try to get a stack trace from within a stack
// trace, because we don't block signals inside this code (which would be too
// expensive: the two extra system calls per stack trace do matter here).
// That can cause a self-deadlock.
// Protect against such reentrant call by failing to get a stack trace.
//
// We use __thread here because the code here is extremely low level -- it is
// called while collecting stack traces from within malloc and mmap, and thus
// can not call anything which might call malloc or mmap itself.
static __thread int recursive = 0;

// The stack trace function might be invoked very early in the program's
// execution (e.g. from the very first malloc if using tcmalloc). Also, the
// glibc implementation itself will trigger malloc the first time it is called.
// As such, we suppress usage of backtrace during this early stage of execution.
static std::atomic<bool> disable_stacktraces(true);  // Disabled until healthy.
// Waiting until static initializers run seems to be late enough.
// This file is included into stacktrace.cc so this will only run once.
ABSL_ATTRIBUTE_UNUSED static int stacktraces_enabler = []() {
  void* unused_stack[1];
  // Force the first backtrace to happen early to get the one-time shared lib
  // loading (allocation) out of the way. After the first call it is much safer
  // to use backtrace from a signal handler if we crash somewhere later.
  backtrace(unused_stack, 1);
  disable_stacktraces.store(false, std::memory_order_relaxed);
  return 0;
}();

static int GetStackTrace(void** result, int max_depth, int skip_count) {
  void* ip;
  int n = 0;
  unw_cursor_t cursor;
  unw_context_t uc;
  unw_getcontext(&uc);
  int init = unw_init_local(&cursor, &uc);
  ABSL_RAW_CHECK(init >= 0, "unw_init_local failed");
  skip_count++;  // Do not include the "GetStackTrace" frame

  //max_depth = 10;
  while (n < max_depth) {
    int ret =
        unw_get_reg(&cursor, UNW_REG_IP, reinterpret_cast<unw_word_t*>(&ip));
    if (ret < 0) {
      break;
    }
    if (skip_count > 0) {
      skip_count--;
    } else {
      result[n++] = ip;
    }
    ret = unw_step(&cursor);
    if (ret <= 0) {
      break;
    }
  }

  return n;
}

template <bool IS_STACK_FRAMES, bool IS_WITH_CONTEXT>
static int UnwindImpl(void** result, int* sizes, int max_depth, int skip_count,
                      const void *ucp, int *min_dropped_frames) {
  if (recursive || disable_stacktraces.load(std::memory_order_relaxed)) {
    return 0;
  }
  ++recursive;

  int result_count = GetStackTrace(result, max_depth, skip_count);
  if (min_dropped_frames) {
    *min_dropped_frames = skip_count;
  }

  if (IS_STACK_FRAMES) {
    // No implementation for finding out the stack frame sizes yet.
    memset(sizes, 0, sizeof(*sizes) * static_cast<size_t>(result_count));
  }

  --recursive;

  return result_count;
}

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
bool StackTraceWorksForTest() {
  return true;
}
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_STACKTRACE_LIBUNWIND_INL_H_
