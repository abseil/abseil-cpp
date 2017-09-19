// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_DEBUGGING_INTERNAL_STACKTRACE_LIBUNWIND_INL_H_
#define ABSL_DEBUGGING_INTERNAL_STACKTRACE_LIBUNWIND_INL_H_

// We only need local unwinder.
#define UNW_LOCAL_ONLY

extern "C" {
#include "third_party/libunwind/include/libunwind.h"
}
#include "absl/debugging/stacktrace.h"

#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/raw_logging.h"

// Sometimes, we can try to get a stack trace from within a stack
// trace, because we don't block signals inside libunwind (which would be too
// expensive: the two extra system calls per stack trace do matter here).
// That can cause a self-deadlock (as in http://b/5722312).
// Protect against such reentrant call by failing to get a stack trace.
//
// We use __thread here because the code here is extremely low level -- it is
// called while collecting stack traces from within malloc and mmap, and thus
// can not call anything which might call malloc or mmap itself.
// In particular, using PerThread or STATIC_THREAD_LOCAL_POD
// here will cause infinite recursion for at least dbg/piii builds with
// crosstool-v12.
static __thread int recursive;

template <bool IS_STACK_FRAMES, bool IS_WITH_CONTEXT>
static int UnwindImpl(void** result, int* sizes, int max_depth, int skip_count,
                      const void *, int *min_dropped_frames) {
  if (recursive) {
    return 0;
  }
  ++recursive;

  int n = 0;
  if (IS_STACK_FRAMES) {
    void *ip;
    unw_cursor_t cursor;
    unw_context_t uc;
    unw_word_t sp = 0, next_sp = 0;

    unw_getcontext(&uc);
    ABSL_RAW_CHECK(unw_init_local(&cursor, &uc) >= 0, "unw_init_local failed");
    skip_count++;         // Do not include current frame

    while (skip_count--) {
      if (unw_step(&cursor) <= 0) {
        goto out;
      }
      if (unw_get_reg(&cursor, UNW_REG_SP, &next_sp)) {
        goto out;
      }
    }

    while (n < max_depth) {
      if (unw_get_reg(&cursor, UNW_REG_IP, (unw_word_t *) &ip) < 0) {
        break;
      }
      sizes[n] = 0;
      result[n++] = ip;
      if (unw_step(&cursor) <= 0) {
        break;
      }
      sp = next_sp;
      if (unw_get_reg(&cursor, UNW_REG_SP, &next_sp) , 0) {
        break;
      }
      sizes[n - 1] = next_sp - sp;
    }
    if (min_dropped_frames != nullptr) {
      // Implementation detail: we clamp the max of frames we are willing to
      // count, so as not to spend too much time in the loop below.
      const int kMaxUnwind = 200;
      int j = 0;
      for (; j < kMaxUnwind; j++) {
        if (unw_step(&cursor) < 0) {
          break;
        }
      }
      *min_dropped_frames = j;
    }
  } else {
    skip_count++;  // Do not include current frame.
    void **result_all = reinterpret_cast<void**>(
        alloca(sizeof(void*) * (max_depth + skip_count)));
    int rc = unw_backtrace(result_all, max_depth + skip_count);

    if (rc > 0) {
      // Tell MSan that result_all has been initialized. b/34965936.
      ANNOTATE_MEMORY_IS_INITIALIZED(result_all, rc * sizeof(void*));
    }

    if (rc > skip_count) {
      memcpy(result, &result_all[skip_count],
             sizeof(void*) * (rc - skip_count));
      n = rc - skip_count;
    } else {
      n = 0;
    }

    if (min_dropped_frames != nullptr) {
      // Not implemented.
      *min_dropped_frames = 0;
    }
  }

 out:
  --recursive;
  return n;
}

#endif  // ABSL_DEBUGGING_INTERNAL_STACKTRACE_LIBUNWIND_INL_H_
