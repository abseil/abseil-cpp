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

// Produce stack trace.
//
// There are three different ways we can try to get the stack trace:
//
// 1) Our hand-coded stack-unwinder.  This depends on a certain stack
//    layout, which is used by gcc (and those systems using a
//    gcc-compatible ABI) on x86 systems, at least since gcc 2.95.
//    It uses the frame pointer to do its work.
//
// 2) The libunwind library.  This is still in development, and as a
//    separate library adds a new dependency, but doesn't need a frame
//    pointer.  It also doesn't call malloc.
//
// 3) The gdb unwinder -- also the one used by the c++ exception code.
//    It's obviously well-tested, but has a fatal flaw: it can call
//    malloc() from the unwinder.  This is a problem because we're
//    trying to use the unwinder to instrument malloc().
//
// Note: if you add a new implementation here, make sure it works
// correctly when absl::GetStackTrace() is called with max_depth == 0.
// Some code may do that.

#include "absl/debugging/stacktrace.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/debugging/internal/stacktrace_config.h"

#ifdef ABSL_INTERNAL_HAVE_ALLOCA
#error ABSL_INTERNAL_HAVE_ALLOCA cannot be directly set
#endif

#ifdef _WIN32
#include <malloc.h>
#define ABSL_INTERNAL_HAVE_ALLOCA 1
#else
#ifdef __has_include
#if __has_include(<alloca.h>)
#include <alloca.h>
#define ABSL_INTERNAL_HAVE_ALLOCA 1
#elif !defined(alloca)
static void* alloca(size_t) noexcept { return nullptr; }
#endif
#endif
#endif

#ifdef ABSL_INTERNAL_HAVE_ALLOCA
static constexpr bool kHaveAlloca = true;
#else
static constexpr bool kHaveAlloca = false;
#endif

#if defined(ABSL_STACKTRACE_INL_HEADER)
#include ABSL_STACKTRACE_INL_HEADER
#else
# error Cannot calculate stack trace: will need to write for your environment

# include "absl/debugging/internal/stacktrace_aarch64-inl.inc"
# include "absl/debugging/internal/stacktrace_arm-inl.inc"
# include "absl/debugging/internal/stacktrace_emscripten-inl.inc"
# include "absl/debugging/internal/stacktrace_generic-inl.inc"
# include "absl/debugging/internal/stacktrace_powerpc-inl.inc"
# include "absl/debugging/internal/stacktrace_riscv-inl.inc"
# include "absl/debugging/internal/stacktrace_unimplemented-inl.inc"
# include "absl/debugging/internal/stacktrace_win32-inl.inc"
# include "absl/debugging/internal/stacktrace_x86-inl.inc"
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

typedef int (*Unwinder)(void**, int*, int, int, const void*, int*);
std::atomic<Unwinder> custom;

template <bool IS_STACK_FRAMES, bool IS_WITH_CONTEXT>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline int Unwind(void** result, uintptr_t* frames,
                                               int* sizes, size_t max_depth,
                                               int skip_count, const void* uc,
                                               int* min_dropped_frames) {
  bool unwind_with_fixup = internal_stacktrace::ShouldFixUpStack();
  if (unwind_with_fixup) {
    if constexpr (kHaveAlloca) {
      // Some implementations of FixUpStack may need to be passed frame
      // information from Unwind, even if the caller doesn't need that
      // information. We allocate the necessary buffers for such implementations
      // here.
      if (frames == nullptr) {
        frames = static_cast<uintptr_t*>(alloca(max_depth * sizeof(*frames)));
      }
      if (sizes == nullptr) {
        sizes = static_cast<int*>(alloca(max_depth * sizeof(*sizes)));
      }
    }
  }

  Unwinder g = custom.load(std::memory_order_acquire);
  size_t size;
  // Add 1 to skip count for the unwinder function itself
  ++skip_count;
  if (g != nullptr) {
    size = static_cast<size_t>((*g)(result, sizes, static_cast<int>(max_depth),
                                    skip_count, uc, min_dropped_frames));
    // Frame pointers aren't returned by existing hooks, so clear them.
    if (frames != nullptr) {
      std::fill(frames, frames + size, uintptr_t());
    }
  } else {
    size = static_cast<size_t>(
        unwind_with_fixup
            ? UnwindImpl<true, IS_WITH_CONTEXT>(
                  result, frames, sizes, static_cast<int>(max_depth),
                  skip_count, uc, min_dropped_frames)
            : UnwindImpl<IS_STACK_FRAMES, IS_WITH_CONTEXT>(
                  result, frames, sizes, static_cast<int>(max_depth),
                  skip_count, uc, min_dropped_frames));
  }
  if (unwind_with_fixup) {
    internal_stacktrace::FixUpStack(result, frames, sizes, max_depth, size);
  }
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
  return static_cast<int>(size);
}

}  // anonymous namespace

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int
internal_stacktrace::GetStackFrames(void** result, uintptr_t* frames,
                                    int* sizes, int max_depth, int skip_count) {
  return Unwind<true, false>(result, frames, sizes,
                             static_cast<size_t>(max_depth), skip_count,
                             nullptr, nullptr);
}

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int
internal_stacktrace::GetStackFramesWithContext(void** result, uintptr_t* frames,
                                               int* sizes, int max_depth,
                                               int skip_count, const void* uc,
                                               int* min_dropped_frames) {
  return Unwind<true, true>(result, frames, sizes,
                            static_cast<size_t>(max_depth), skip_count, uc,
                            min_dropped_frames);
}

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int GetStackTrace(
    void** result, int max_depth, int skip_count) {
  return Unwind<false, false>(result, nullptr, nullptr,
                              static_cast<size_t>(max_depth), skip_count,
                              nullptr, nullptr);
}

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int
GetStackTraceWithContext(void** result, int max_depth, int skip_count,
                         const void* uc, int* min_dropped_frames) {
  return Unwind<false, true>(result, nullptr, nullptr,
                             static_cast<size_t>(max_depth), skip_count, uc,
                             min_dropped_frames);
}

void SetStackUnwinder(Unwinder w) {
  custom.store(w, std::memory_order_release);
}

ABSL_ATTRIBUTE_ALWAYS_INLINE static inline int DefaultStackUnwinderImpl(
    void** pcs, uintptr_t* frames, int* sizes, int depth, int skip,
    const void* uc, int* min_dropped_frames) {
  skip++;  // For this function
  decltype(&UnwindImpl<false, false>) f;
  if (sizes == nullptr) {
    if (uc == nullptr) {
      f = &UnwindImpl<false, false>;
    } else {
      f = &UnwindImpl<false, true>;
    }
  } else {
    if (uc == nullptr) {
      f = &UnwindImpl<true, false>;
    } else {
      f = &UnwindImpl<true, true>;
    }
  }
  return (*f)(pcs, frames, sizes, depth, skip, uc, min_dropped_frames);
}

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int
internal_stacktrace::DefaultStackUnwinder(void** pcs, uintptr_t* frames,
                                          int* sizes, int depth, int skip,
                                          const void* uc,
                                          int* min_dropped_frames) {
  int n = DefaultStackUnwinderImpl(pcs, frames, sizes, depth, skip, uc,
                                   min_dropped_frames);
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
  return n;
}

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL int DefaultStackUnwinder(
    void** pcs, int* sizes, int depth, int skip, const void* uc,
    int* min_dropped_frames) {
  int n = DefaultStackUnwinderImpl(pcs, nullptr, sizes, depth, skip, uc,
                                   min_dropped_frames);
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
  return n;
}

ABSL_ATTRIBUTE_WEAK bool internal_stacktrace::ShouldFixUpStack() {
  return false;
}

// Fixes up the stack trace of the current thread, in the first `depth` frames
// of each buffer. The buffers need to be larger than `depth`, to accommodate
// any newly inserted elements. `depth` is updated to reflect the new number of
// elements valid across all the buffers. (It is therefore recommended that all
// buffer sizes be equal.)
//
// The `frames` and `sizes` parameters denote the bounds of the stack frame
// corresponding to each instruction pointer in the `pcs`.
// Any elements inside these buffers may be zero or null, in which case that
// information is assumed to be absent/unavailable.
ABSL_ATTRIBUTE_WEAK void internal_stacktrace::FixUpStack(void**, uintptr_t*,
                                                         int*, size_t,
                                                         size_t&) {}

ABSL_NAMESPACE_END
}  // namespace absl
