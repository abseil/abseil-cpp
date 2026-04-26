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

#ifndef ABSL_CLEANUP_INTERNAL_CLEANUP_H_
#define ABSL_CLEANUP_INTERNAL_CLEANUP_H_

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/base/macros.h"
#include "absl/base/thread_annotations.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace cleanup_internal {

template <typename Callback>
class Defer {
 public:
  // NOLINTBEGIN(google-explicit-constructor)
  Defer(Callback callback)  // NOLINT(runtime/explicit)
    : callback_(std::move(callback)) {}
  // NOLINTEND(google-explicit-constructor)

  ~Defer() {
    std::move(callback_)();
  }

 private:
  Callback callback_;
};

template <typename Callback>
Defer(Callback callback) -> Defer<Callback>;

struct Tag {};

template <typename Arg, typename... Args>
constexpr bool WasDeduced() {
  return (std::is_same<cleanup_internal::Tag, Arg>::value) &&
         (sizeof...(Args) == 0);
}

template <typename Callback>
constexpr bool ReturnsVoid() {
  return (std::is_same<std::invoke_result_t<Callback>, void>::value);
}

template <typename Callback>
class Storage {
 public:
  Storage() = default;  // Trivial default initialization is expected

  // NOTE: Is invoked on uninitialized instances of `Storage`
  void EmplaceCallback(Callback callback) {
    // The callback is stored in a character buffer to decouple its storage
    // duration from its object lifetime. We need to be able to eagerly destroy
    // the callback when a method on `absl::Cleanup` is invoked.
    ::new (GetCallbackBuffer()) Callback(std::move(callback));
  }

  // NOTE: Is invoked on uninitialized instances of `Storage`
  void EngageCallback() {
    fields_.is_callback_engaged_ = true;
  }

  Storage(const Storage& other) = delete;

  Storage& operator=(Storage&& other) = delete;

  Storage& operator=(const Storage& other) = delete;

  void* GetCallbackBuffer() {
    return static_cast<void*>(fields_.callback_buffer_);
  }

  Callback& GetCallback() {
    return *reinterpret_cast<Callback*>(GetCallbackBuffer());
  }

  bool IsCallbackEngaged() const { return fields_.is_callback_engaged_; }

  void DisengageCallback() { fields_.is_callback_engaged_ = false; }

  void DestroyCallback() {
    GetCallback().~Callback();
  }

  void InvokeCallback() ABSL_NO_THREAD_SAFETY_ANALYSIS {
    std::move(GetCallback())();
  }

 private:
  struct Fields {
    alignas(Callback) std::byte callback_buffer_[sizeof(Callback)];
    bool is_callback_engaged_;
  };
  static_assert(std::is_trivially_default_constructible<Fields>::value);

  Fields fields_;
};

}  // namespace cleanup_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CLEANUP_INTERNAL_CLEANUP_H_
