//
// Copyright 2018 The Abseil Authors.
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

// This module allows the programmer to install a signal handler that
// dumps useful debugging information (like a stacktrace) on program
// failure. To use this functionality, call
// absl::InstallFailureSignalHandler() very early in your program,
// usually in the first few lines of main():
//
// int main(int argc, char** argv) {
//   absl::InitializeSymbolizer(argv[0]);
//   absl::FailureSignalHandlerOptions options;
//   absl::InstallFailureSignalHandler(options);
//   DoSomethingInteresting();
//   return 0;
// }

#ifndef ABSL_DEBUGGING_FAILURE_SIGNAL_HANDLER_H_
#define ABSL_DEBUGGING_FAILURE_SIGNAL_HANDLER_H_

namespace absl {

// Options struct for absl::InstallFailureSignalHandler().
struct FailureSignalHandlerOptions {
  // If true, try to symbolize the stacktrace emitted on failure.
  bool symbolize_stacktrace = true;

  // If true, try to run signal handlers on an alternate stack (if
  // supported on the given platform). This is useful in the case
  // where the program crashes due to a stack overflow. By running on
  // a alternate stack, the signal handler might be able to run even
  // when the normal stack space has been exausted. The downside of
  // using an alternate stack is that extra memory for the alternate
  // stack needs to be pre-allocated.
  bool use_alternate_stack = true;

  // If positive, FailureSignalHandler() sets an alarm to be delivered
  // to the program after this many seconds, which will immediately
  // abort the program. This is useful in the potential case where
  // FailureSignalHandler() itself is hung or deadlocked.
  int alarm_on_failure_secs = 3;

  // If false, after absl::FailureSignalHandler() runs, the signal is
  // raised to the default handler for that signal (which normally
  // terminates the program).
  //
  // If true, after absl::FailureSignalHandler() runs, it will call
  // the previously registered signal handler for the signal that was
  // received (if one was registered). This can be used to chain
  // signal handlers.
  //
  // IMPORTANT: If true, the chained fatal signal handlers must not
  // try to recover from the fatal signal. Instead, they should
  // terminate the program via some mechanism, like raising the
  // default handler for the signal, or by calling _exit().
  // absl::FailureSignalHandler() may put parts of the Abseil
  // library into a state that cannot be recovered from.
  bool call_previous_handler = false;

  // If not null, this function may be called with a std::string argument
  // containing failure data. This function is used as a hook to write
  // the failure data to a secondary location, for instance, to a log
  // file. This function may also be called with a null data
  // argument. This is a hint that this is a good time to flush any
  // buffered data before the program may be terminated. Consider
  // flushing any buffered data in all calls to this function.
  //
  // Since this function runs in a signal handler, it should be
  // async-signal-safe if possible.
  // See http://man7.org/linux/man-pages/man7/signal-safety.7.html
  void (*writerfn)(const char*) = nullptr;
};

// Installs a signal handler for the common failure signals SIGSEGV,
// SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGBUG, and SIGTRAP (if they
// exist on the given platform). The signal handler dumps program
// failure data in a unspecified format to stderr. The data dumped by
// the signal handler includes information that may be useful in
// debugging the failure. This may include the program counter, a
// stacktrace, and register information on some systems.  Do not rely
// on the exact format of the output; it is subject to change.
void InstallFailureSignalHandler(const FailureSignalHandlerOptions& options);

namespace debugging_internal {
const char* FailureSignalToString(int signo);
}  // namespace debugging_internal

}  // namespace absl

#endif  // ABSL_DEBUGGING_FAILURE_SIGNAL_HANDLER_H_
