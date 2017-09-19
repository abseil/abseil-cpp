//
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
//

// Routines to extract the current stack trace. These functions are
// thread-safe and async-signal-safe.
// Note that stack trace functionality is platform dependent and requires
// additional support from the compiler/build system in many cases. (That is,
// this generally only works on platforms/builds that have been specifically
// configured to support it.)

#ifndef ABSL_DEBUGGING_STACKTRACE_H_
#define ABSL_DEBUGGING_STACKTRACE_H_

namespace absl {

// Skips the most recent "skip_count" stack frames (also skips the
// frame generated for the "absl::GetStackFrames" routine itself), and then
// records the pc values for up to the next "max_depth" frames in
// "result", and the corresponding stack frame sizes in "sizes".
// Returns the number of values recorded in "result"/"sizes".
//
// Example:
//      main() { foo(); }
//      foo() { bar(); }
//      bar() {
//        void* result[10];
//        int sizes[10];
//        int depth = absl::GetStackFrames(result, sizes, 10, 1);
//      }
//
// The absl::GetStackFrames call will skip the frame for "bar".  It will
// return 2 and will produce pc values that map to the following
// procedures:
//      result[0]       foo
//      result[1]       main
// (Actually, there may be a few more entries after "main" to account for
// startup procedures.)
// And corresponding stack frame sizes will also be recorded:
//    sizes[0]       16
//    sizes[1]       16
// (Stack frame sizes of 16 above are just for illustration purposes.)
// Stack frame sizes of 0 or less indicate that those frame sizes couldn't
// be identified.
//
// This routine may return fewer stack frame entries than are
// available. Also note that "result" and "sizes" must both be non-null.
extern int GetStackFrames(void** result, int* sizes, int max_depth,
                          int skip_count);

// Same as above, but to be used from a signal handler. The "uc" parameter
// should be the pointer to ucontext_t which was passed as the 3rd parameter
// to sa_sigaction signal handler. It may help the unwinder to get a
// better stack trace under certain conditions. The "uc" may safely be null.
//
// If min_dropped_frames is not null, stores in *min_dropped_frames a
// lower bound on the number of dropped stack frames. The stored value is
// guaranteed to be >= 0. The number of real stack frames is guaranteed to
// be >= skip_count + max_depth + *min_dropped_frames.
extern int GetStackFramesWithContext(void** result, int* sizes, int max_depth,
                                     int skip_count, const void* uc,
                                     int* min_dropped_frames);

// This is similar to the absl::GetStackFrames routine, except that it returns
// the stack trace only, and not the stack frame sizes as well.
// Example:
//      main() { foo(); }
//      foo() { bar(); }
//      bar() {
//        void* result[10];
//        int depth = absl::GetStackTrace(result, 10, 1);
//      }
//
// This produces:
//      result[0]       foo
//      result[1]       main
//           ....       ...
//
// "result" must not be null.
extern int GetStackTrace(void** result, int max_depth, int skip_count);

// Same as above, but to be used from a signal handler. The "uc" parameter
// should be the pointer to ucontext_t which was passed as the 3rd parameter
// to sa_sigaction signal handler. It may help the unwinder to get a
// better stack trace under certain conditions. The "uc" may safely be null.
//
// If min_dropped_frames is not null, stores in *min_dropped_frames a
// lower bound on the number of dropped stack frames. The stored value is
// guaranteed to be >= 0. The number of real stack frames is guaranteed to
// be >= skip_count + max_depth + *min_dropped_frames.
extern int GetStackTraceWithContext(void** result, int max_depth,
                                    int skip_count, const void* uc,
                                    int* min_dropped_frames);

// Call this to provide a custom function for unwinding stack frames
// that will be used every time someone invokes one of the static
// GetStack{Frames,Trace}{,WithContext}() functions above.
//
// The arguments passed to the unwinder function will match the
// arguments passed to absl::GetStackFramesWithContext() except that sizes
// will be non-null iff the caller is interested in frame sizes.
//
// If unwinder is null, we revert to the default stack-tracing behavior.
//
// ****************************************************************
// WARNINGS
//
// absl::SetStackUnwinder is not suitable for general purpose use.  It is
// provided for custom runtimes.
// Some things to watch out for when calling absl::SetStackUnwinder:
//
// (a) The unwinder may be called from within signal handlers and
// therefore must be async-signal-safe.
//
// (b) Even after a custom stack unwinder has been unregistered, other
// threads may still be in the process of using that unwinder.
// Therefore do not clean up any state that may be needed by an old
// unwinder.
// ****************************************************************
extern void SetStackUnwinder(int (*unwinder)(void** pcs, int* sizes,
                                             int max_depth, int skip_count,
                                             const void* uc,
                                             int* min_dropped_frames));

// Function that exposes built-in stack-unwinding behavior, ignoring
// any calls to absl::SetStackUnwinder().
//
// pcs must NOT be null.
//
// sizes may be null.
// uc may be null.
// min_dropped_frames may be null.
//
// The semantics are the same as the corresponding GetStack*() function in the
// case where absl::SetStackUnwinder() was never called.  Equivalents are:
//
//                       null sizes         |        non-nullptr sizes
//             |==========================================================|
//     null uc | GetStackTrace()            | GetStackFrames()            |
// non-null uc | GetStackTraceWithContext() | GetStackFramesWithContext() |
//             |==========================================================|
extern int DefaultStackUnwinder(void** pcs, int* sizes, int max_depth,
                                int skip_count, const void* uc,
                                int* min_dropped_frames);

}  // namespace absl

#endif  // ABSL_DEBUGGING_STACKTRACE_H_
