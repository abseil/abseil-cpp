//
// Copyright 2019 The Abseil Authors.
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

#ifndef ABSL_FLAGS_INTERNAL_FLAG_H_
#define ABSL_FLAGS_INTERNAL_FLAG_H_

#include <cstring>

#include "absl/flags/internal/commandlineflag.h"
#include "absl/flags/internal/registry.h"

namespace absl {
namespace flags_internal {

constexpr int64_t AtomicInit() { return 0xababababababababll; }

// Signature for the mutation callback used by watched Flags
// The callback is noexcept.
// TODO(rogeeff): add noexcept after C++17 support is added.
using FlagCallback = void (*)();

void InvokeCallback(absl::Mutex* primary_mu, absl::Mutex* callback_mu,
                    FlagCallback cb) ABSL_EXCLUSIVE_LOCKS_REQUIRED(primary_mu);

// This is "unspecified" implementation of absl::Flag<T> type.
template <typename T>
class Flag final : public flags_internal::CommandLineFlag {
 public:
  constexpr Flag(const char* name, const flags_internal::HelpGenFunc help_gen,
                 const char* filename,
                 const flags_internal::FlagMarshallingOpFn marshalling_op,
                 const flags_internal::InitialValGenFunc initial_value_gen)
      : flags_internal::CommandLineFlag(
            name, flags_internal::HelpText::FromFunctionPointer(help_gen),
            filename, &flags_internal::FlagOps<T>, marshalling_op,
            initial_value_gen,
            /*def=*/nullptr,
            /*cur=*/nullptr),
        atomic_(flags_internal::AtomicInit()),
        callback_(nullptr) {}

  T Get() const {
    // Implementation notes:
    //
    // We are wrapping a union around the value of `T` to serve three purposes:
    //
    //  1. `U.value` has correct size and alignment for a value of type `T`
    //  2. The `U.value` constructor is not invoked since U's constructor does
    //  not
    //     do it explicitly.
    //  3. The `U.value` destructor is invoked since U's destructor does it
    //     explicitly. This makes `U` a kind of RAII wrapper around non default
    //     constructible value of T, which is destructed when we leave the
    //     scope. We do need to destroy U.value, which is constructed by
    //     CommandLineFlag::Read even though we left it in a moved-from state
    //     after std::move.
    //
    // All of this serves to avoid requiring `T` being default constructible.
    union U {
      T value;
      U() {}
      ~U() { value.~T(); }
    };
    U u;

    Read(&u.value, &flags_internal::FlagOps<T>);
    return std::move(u.value);
  }

  bool AtomicGet(T* v) const {
    const int64_t r = atomic_.load(std::memory_order_acquire);
    if (r != flags_internal::AtomicInit()) {
      std::memcpy(v, &r, sizeof(T));
      return true;
    }

    return false;
  }

  void Set(const T& v) { Write(&v, &flags_internal::FlagOps<T>); }

  void SetCallback(const flags_internal::FlagCallback mutation_callback) {
    absl::MutexLock l(InitFlagIfNecessary());

    callback_ = mutation_callback;

    InvokeCallback();
  }
  void InvokeCallback() override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(locks_->primary_mu) {
    flags_internal::InvokeCallback(&locks_->primary_mu, &locks_->callback_mu,
                                   callback_);
  }

 private:
  void Destroy() const override {
    // Values are heap allocated Abseil Flags.
    if (cur_) Delete(op_, cur_);
    if (def_) Delete(op_, def_);

    delete locks_;
  }

  void StoreAtomic() override {
    if (sizeof(T) <= sizeof(int64_t)) {
      int64_t t = 0;
      std::memcpy(&t, cur_, (std::min)(sizeof(T), sizeof(int64_t)));
      atomic_.store(t, std::memory_order_release);
    }
  }

  // Flag's data
  // For some types, a copy of the current value is kept in an atomically
  // accessible field.
  std::atomic<int64_t> atomic_;
  FlagCallback callback_;  // Mutation callback
};

// This class facilitates Flag object registration and tail expression-based
// flag definition, for example:
// ABSL_FLAG(int, foo, 42, "Foo help").OnUpdate(NotifyFooWatcher);
template <typename T, bool do_register>
class FlagRegistrar {
 public:
  explicit FlagRegistrar(Flag<T>* flag) : flag_(flag) {
    if (do_register) flags_internal::RegisterCommandLineFlag(flag_);
  }

  FlagRegistrar& OnUpdate(flags_internal::FlagCallback cb) && {
    flag_->SetCallback(cb);
    return *this;
  }

  // Make the registrar "die" gracefully as a bool on a line where registration
  // happens. Registrar objects are intended to live only as temporary.
  operator bool() const { return true; }  // NOLINT

 private:
  Flag<T>* flag_;  // Flag being registered (not owned).
};

// This struct and corresponding overload to MakeDefaultValue are used to
// facilitate usage of {} as default value in ABSL_FLAG macro.
struct EmptyBraces {};

template <typename T>
T* MakeFromDefaultValue(T t) {
  return new T(std::move(t));
}

template <typename T>
T* MakeFromDefaultValue(EmptyBraces) {
  return new T;
}

}  // namespace flags_internal
}  // namespace absl

#endif  // ABSL_FLAGS_INTERNAL_FLAG_H_
