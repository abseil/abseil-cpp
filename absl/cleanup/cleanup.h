// Copyright 2021 The Abseil Authors.
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
// -----------------------------------------------------------------------------
// File: cleanup.h
// -----------------------------------------------------------------------------
//
// `absl::Cleanup` implements the scope guard idiom. On scope exit, it invokes
// the contained callback via `std::move(callback)();`.
//
// By design, the implementation does not allocate, take any locks or otherwise
// acquire resources. It is safe to use `absl::Cleanup` in a signal
// handler, assuming the underlying callback is itself signal safe.
//
// Example:
//
// ```
//   absl::Status CopyGoodData(const std::string& src_path,
//                             const std::string& sink_path) {
//     std::FILE* src_file = std::fopen(src_path.c_str(), "r");
//     if (src_file == nullptr) {
//       absl::Status result = absl::NotFoundError("No source file");
//       return result;  // Zero cleanups execute
//     }
//
//     // Ensure the source file is closed on scope exit...
//     absl::Cleanup src_closer = [src_file] { std::fclose(src_file); };
//
//     std::FILE* sink_file = std::fopen(sink_path.c_str(), "w");
//     if (sink_file == nullptr) {
//       absl::Status result = absl::NotFoundError("No sink file");
//       return result;  // First cleanup executes
//     }
//
//     // Ensure the sink file is closed on scope exit...
//     absl::Cleanup sink_closer = [sink_file] { std::fclose(sink_file); };
//
//     example::Data data;
//     while (example::ReadData(src_file, &data)) {
//       if (!data.IsGood()) {
//         absl::Status result = absl::FailedPreconditionError("Read bad data");
//         return result;  // Both cleanups execute
//       }
//       example::SaveData(sink_file, &data);
//     }
//
//     return absl::OkStatus();  // Both cleanups execute
//   }
// ```
//
// Usage:
//
// Refraining from calling any methods will...
// - Result in the callback being called when the `absl::Cleanup` goes out of
//   scope.
// - Leverage compiler constant folding to remove the runtime branch that checks
//   whether the callback is engaged. If you do not touch the cleanup object
//   after initialization, it should provide performance on par with a control
//   flow construct such as the `defer` found in Zig. Please file a bug if you
//   observe otherwise.
//
// Calls to `std::move(cleanup).Cancel();` will...
// - Destroy the callback immediately.
// - Disengage the callback, meaning that when the `absl::Cleanup` goes out of
//   scope, the callback will not be called.
//
// Calls to `std::move(cleanup).Invoke();` will...
// - Invoke the callback immediately.
// - Destroy the callback as soon as the immediate invocation completes.
// - Disengage the callback, meaning that when the `absl::Cleanup` goes out of
//   scope, the callback will not be called.
//
// Note:
//
// `absl::Cleanup` is not an interface type. It is only intended to be used
// within the body of a function. Please refrain from using it as a continuation
// passing mechanism.
//
// The use of `std::move(cleanup).MyMethod()` as ceremony to invoke the methods
// is intended to leverage existing best-effort tools that detect normal
// use-after-move. The implementation is not equipped to handle multiple method
// calls and thus has the same semantics the tools already diagnose.

#ifndef ABSL_CLEANUP_CLEANUP_H_
#define ABSL_CLEANUP_CLEANUP_H_

#include <utility>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/cleanup/internal/cleanup.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

template <typename Arg, typename Callback = void()>
class [[nodiscard]] Cleanup final {
  static_assert(cleanup_internal::WasDeduced<Arg>(),
                "Explicit template parameters are not supported.");

  static_assert(cleanup_internal::ReturnsVoid<Callback>(),
                "Callbacks that return values are not supported.");

 public:
  // NOLINTBEGIN(google-explicit-constructor)
  Cleanup(Callback callback) {  // NOLINT(runtime/explicit)
    storage_.EmplaceCallback(std::move(callback));
    storage_.EngageCallback();
  }
  // NOLINTEND(google-explicit-constructor)

  Cleanup(Cleanup&& other) {
    ABSL_HARDENING_ASSERT(other.storage_.IsCallbackEngaged());
    cleanup_internal::Defer last_step = [&] {
      other.storage_.DestroyCallback();
    };
    other.storage_.DisengageCallback();
    storage_.EmplaceCallback(std::move(other.storage_.GetCallback()));
    storage_.EngageCallback();
  }

  void Cancel() && {
    ABSL_HARDENING_ASSERT(storage_.IsCallbackEngaged());
    cleanup_internal::Defer last_step = [&] { storage_.DestroyCallback(); };
    storage_.DisengageCallback();
  }

  void Invoke() && {
    ABSL_HARDENING_ASSERT(storage_.IsCallbackEngaged());
    cleanup_internal::Defer last_step = [&] { storage_.DestroyCallback(); };
    storage_.DisengageCallback();
    storage_.InvokeCallback();
  }

  ~Cleanup() {
    if (storage_.IsCallbackEngaged()) {
      cleanup_internal::Defer last_step = [&] { storage_.DestroyCallback(); };
      storage_.DisengageCallback();
      storage_.InvokeCallback();
    }
  }

 private:
  cleanup_internal::Storage<Callback> storage_;
};

// `absl::Cleanup c = /* callback */;`
//
// Explicit deduction guides signal to tooling that this type actively and
// intentionally supports Class Template Argument Deduction.
template <typename Callback>
Cleanup(Callback callback) -> Cleanup<cleanup_internal::Tag, Callback>;

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CLEANUP_CLEANUP_H_
