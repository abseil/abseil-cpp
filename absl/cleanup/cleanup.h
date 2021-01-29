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
// `absl::Cleanup` implements the scope guard idiom, invoking `operator()() &&`
// on the callback it was constructed with, on scope exit.
//
// Example:
//
// ```
//   void CopyGoodData(const char* input_path, const char* output_path) {
//     FILE* in_file = fopen(input_path, "r");
//     FILE* out_file = fopen(output_path, "w");
//     if (in_file == nullptr || out_file == nullptr) return;
//
//     // C++17 style using class template argument deduction
//     absl::Cleanup in_closer = [&in_file] { fclose(in_file); };
//
//     // C++11 style using the factory function
//     auto out_closer = absl::MakeCleanup([&out_file] { fclose(out_file); });
//
//     // `fclose` will be called on all exit paths by the cleanup instances
//
//     Data data;
//     while (ReadData(in_file, &data)) {
//       if (data.IsBad()) {
//         LOG(ERROR) << "Found bad data.";
//         return;  // `in_closer` and `out_closer` will call their callbacks
//       }
//       SaveData(out_file, &data);
//     }
//     return;  // `in_closer` and `out_closer` will call their callbacks
//   }
// ```
//
// `std::move(cleanup).Invoke()` will execute the callback early, before
// destruction, and prevent the callback from executing in the destructor.
//
// Alternatively, `std::move(cleanup).Cancel()` will prevent the callback from
// ever executing at all.
//
// Once a cleanup object has been `std::move(...)`-ed, it may not be used again.

#ifndef ABSL_CLEANUP_CLEANUP_H_
#define ABSL_CLEANUP_CLEANUP_H_

#include <utility>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/cleanup/internal/cleanup.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

template <typename Arg, typename Callback = void()>
class ABSL_MUST_USE_RESULT Cleanup {
  static_assert(cleanup_internal::WasDeduced<Arg>(),
                "Explicit template parameters are not supported.");

  static_assert(cleanup_internal::ReturnsVoid<Callback>(),
                "Callbacks that return values are not supported.");

 public:
  Cleanup(Callback callback) : storage_(std::move(callback)) {}  // NOLINT

  Cleanup(Cleanup&& other) : storage_(std::move(other.storage_)) {}

  void Cancel() && {
    ABSL_HARDENING_ASSERT(storage_.IsCallbackEngaged());
    storage_.DisengageCallback();
  }

  void Invoke() && {
    ABSL_HARDENING_ASSERT(storage_.IsCallbackEngaged());
    storage_.DisengageCallback();
    storage_.InvokeCallback();
  }

  ~Cleanup() {
    if (storage_.IsCallbackEngaged()) {
      storage_.InvokeCallback();
    }
  }

 private:
  cleanup_internal::Storage<Callback> storage_;
};

// `auto c = absl::MakeCleanup(/* callback */);`
//
// C++11 type deduction API for creating an instance of `absl::Cleanup`.
template <typename... Args, typename Callback>
absl::Cleanup<cleanup_internal::Tag, Callback> MakeCleanup(Callback callback) {
  static_assert(cleanup_internal::WasDeduced<cleanup_internal::Tag, Args...>(),
                "Explicit template parameters are not supported.");

  static_assert(cleanup_internal::ReturnsVoid<Callback>(),
                "Callbacks that return values are not supported.");

  return {std::move(callback)};
}

// `absl::Cleanup c = /* callback */;`
//
// C++17 type deduction API for creating an instance of `absl::Cleanup`.
#if defined(ABSL_HAVE_CLASS_TEMPLATE_ARGUMENT_DEDUCTION)
template <typename Callback>
Cleanup(Callback callback) -> Cleanup<cleanup_internal::Tag, Callback>;
#endif  // defined(ABSL_HAVE_CLASS_TEMPLATE_ARGUMENT_DEDUCTION)

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CLEANUP_CLEANUP_H_
