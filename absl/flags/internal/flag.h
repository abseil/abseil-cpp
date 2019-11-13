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

#include <atomic>
#include <cstring>

#include "absl/base/thread_annotations.h"
#include "absl/flags/internal/commandlineflag.h"
#include "absl/flags/internal/registry.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

namespace absl {
namespace flags_internal {

constexpr int64_t AtomicInit() { return 0xababababababababll; }

template <typename T>
class Flag;

template <typename T>
class FlagState : public flags_internal::FlagStateInterface {
 public:
  FlagState(Flag<T>* flag, T&& cur, bool modified, bool on_command_line,
            int64_t counter)
      : flag_(flag),
        cur_value_(std::move(cur)),
        modified_(modified),
        on_command_line_(on_command_line),
        counter_(counter) {}

  ~FlagState() override = default;

 private:
  friend class Flag<T>;

  // Restores the flag to the saved state.
  void Restore() const override;

  // Flag and saved flag data.
  Flag<T>* flag_;
  T cur_value_;
  bool modified_;
  bool on_command_line_;
  int64_t counter_;
};

// Signature for the mutation callback used by watched Flags
// The callback is noexcept.
// TODO(rogeeff): add noexcept after C++17 support is added.
using FlagCallback = void (*)();

void InvokeCallback(absl::Mutex* primary_mu, absl::Mutex* callback_mu,
                    FlagCallback cb) ABSL_EXCLUSIVE_LOCKS_REQUIRED(primary_mu);

// The class encapsulates the Flag's data and safe access to it.
class FlagImpl {
 public:
  constexpr FlagImpl(const flags_internal::FlagOpFn op,
                     const flags_internal::FlagMarshallingOpFn marshalling_op,
                     const flags_internal::InitialValGenFunc initial_value_gen)
      : op_(op),
        marshalling_op_(marshalling_op),
        initial_value_gen_(initial_value_gen) {}

  // Forces destruction of the Flag's data.
  void Destroy() const;

  // Constant access methods
  bool IsModified() const ABSL_LOCKS_EXCLUDED(locks_->primary_mu);
  bool IsSpecifiedOnCommandLine() const ABSL_LOCKS_EXCLUDED(locks_->primary_mu);
  std::string DefaultValue() const ABSL_LOCKS_EXCLUDED(locks_->primary_mu);
  std::string CurrentValue() const ABSL_LOCKS_EXCLUDED(locks_->primary_mu);
  void Read(const CommandLineFlag& flag, void* dst,
            const flags_internal::FlagOpFn dst_op) const
      ABSL_LOCKS_EXCLUDED(locks_->primary_mu);
  // Attempts to parse supplied `value` std::string.
  bool TryParse(const CommandLineFlag& flag, void* dst, absl::string_view value,
                std::string* err) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(locks_->primary_mu);
  template <typename T>
  bool AtomicGet(T* v) const {
    const int64_t r = atomic_.load(std::memory_order_acquire);
    if (r != flags_internal::AtomicInit()) {
      std::memcpy(v, &r, sizeof(T));
      return true;
    }

    return false;
  }

  // Mutating access methods
  void Write(const CommandLineFlag& flag, const void* src,
             const flags_internal::FlagOpFn src_op)
      ABSL_LOCKS_EXCLUDED(locks_->primary_mu);
  bool SetFromString(const CommandLineFlag& flag, absl::string_view value,
                     FlagSettingMode set_mode, ValueSource source,
                     std::string* err) ABSL_LOCKS_EXCLUDED(locks_->primary_mu);
  // If possible, updates copy of the Flag's value that is stored in an
  // atomic word.
  void StoreAtomic() ABSL_EXCLUSIVE_LOCKS_REQUIRED(locks_->primary_mu);

  // Interfaces to operate on callbacks.
  void SetCallback(const flags_internal::FlagCallback mutation_callback)
      ABSL_LOCKS_EXCLUDED(locks_->primary_mu);
  void InvokeCallback() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(locks_->primary_mu);

  // Interfaces to save/restore mutable flag data
  template <typename T>
  std::unique_ptr<flags_internal::FlagStateInterface> SaveState(
      Flag<T>* flag) const ABSL_LOCKS_EXCLUDED(locks_->primary_mu) {
    T&& cur_value = flag->Get();
    absl::MutexLock l(DataGuard());

    return absl::make_unique<flags_internal::FlagState<T>>(
        flag, std::move(cur_value), modified_, on_command_line_, counter_);
  }
  bool RestoreState(const CommandLineFlag& flag, const void* value,
                    bool modified, bool on_command_line, int64_t counter)
      ABSL_LOCKS_EXCLUDED(locks_->primary_mu);

  // Value validation interfaces.
  void CheckDefaultValueParsingRoundtrip(const CommandLineFlag& flag) const
      ABSL_LOCKS_EXCLUDED(locks_->primary_mu);
  bool ValidateInputValue(absl::string_view value) const
      ABSL_LOCKS_EXCLUDED(locks_->primary_mu);

 private:
  // Lazy initialization of the Flag's data.
  void Init();
  // Ensures that the lazily initialized data is initialized,
  // and returns pointer to the mutex guarding flags data.
  absl::Mutex* DataGuard() const ABSL_LOCK_RETURNED(locks_->primary_mu);

  // Immutable Flag's data.
  const FlagOpFn op_;                          // Type-specific handler
  const FlagMarshallingOpFn marshalling_op_;   // Marshalling ops handler
  const InitialValGenFunc initial_value_gen_;  // Makes flag's initial value

  // Mutable Flag's data. (guarded by locks_->primary_mu).
  // Indicates that locks_, cur_ and def_ fields have been lazily initialized.
  std::atomic<bool> inited_{false};
  // Has flag value been modified?
  bool modified_ ABSL_GUARDED_BY(locks_->primary_mu) = false;
  // Specified on command line.
  bool on_command_line_ ABSL_GUARDED_BY(locks_->primary_mu) = false;
  // Lazily initialized pointer to default value
  void* def_ ABSL_GUARDED_BY(locks_->primary_mu) = nullptr;
  // Lazily initialized pointer to current value
  void* cur_ ABSL_GUARDED_BY(locks_->primary_mu) = nullptr;
  // Mutation counter
  int64_t counter_ ABSL_GUARDED_BY(locks_->primary_mu) = 0;
  // For some types, a copy of the current value is kept in an atomically
  // accessible field.
  std::atomic<int64_t> atomic_{flags_internal::AtomicInit()};
  // Mutation callback
  FlagCallback callback_ = nullptr;

  // Lazily initialized mutexes for this flag value.  We cannot inline a
  // SpinLock or Mutex here because those have non-constexpr constructors and
  // so would prevent constant initialization of this type.
  // TODO(rogeeff): fix it once Mutex has constexpr constructor
  // The following struct contains the locks in a CommandLineFlag struct.
  // They are in a separate struct that is lazily allocated to avoid problems
  // with static initialization and to avoid multiple allocations.
  struct CommandLineFlagLocks {
    absl::Mutex primary_mu;   // protects several fields in CommandLineFlag
    absl::Mutex callback_mu;  // used to serialize callbacks
  };

  CommandLineFlagLocks* locks_ = nullptr;  // locks, laziliy allocated.
};

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
            filename),
        impl_(&flags_internal::FlagOps<T>, marshalling_op, initial_value_gen) {}

  T Get() const {
    // See implementation notes in CommandLineFlag::Get().
    union U {
      T value;
      U() {}
      ~U() { value.~T(); }
    };
    U u;

    impl_.Read(*this, &u.value, &flags_internal::FlagOps<T>);
    return std::move(u.value);
  }

  bool AtomicGet(T* v) const { return impl_.AtomicGet(v); }

  void Set(const T& v) { impl_.Write(*this, &v, &flags_internal::FlagOps<T>); }

  void SetCallback(const flags_internal::FlagCallback mutation_callback) {
    impl_.SetCallback(mutation_callback);
  }

  // CommandLineFlag interface
  bool IsModified() const override { return impl_.IsModified(); }
  bool IsSpecifiedOnCommandLine() const override {
    return impl_.IsSpecifiedOnCommandLine();
  }
  std::string DefaultValue() const override { return impl_.DefaultValue(); }
  std::string CurrentValue() const override { return impl_.CurrentValue(); }

  bool ValidateInputValue(absl::string_view value) const override {
    return impl_.ValidateInputValue(value);
  }

  // Interfaces to save and restore flags to/from persistent state.
  // Returns current flag state or nullptr if flag does not support
  // saving and restoring a state.
  std::unique_ptr<flags_internal::FlagStateInterface> SaveState() override {
    return impl_.SaveState(this);
  }

  // Restores the flag state to the supplied state object. If there is
  // nothing to restore returns false. Otherwise returns true.
  bool RestoreState(const flags_internal::FlagState<T>& flag_state) {
    return impl_.RestoreState(*this, &flag_state.cur_value_,
                              flag_state.modified_, flag_state.on_command_line_,
                              flag_state.counter_);
  }

  bool SetFromString(absl::string_view value,
                     flags_internal::FlagSettingMode set_mode,
                     flags_internal::ValueSource source,
                     std::string* error) override {
    return impl_.SetFromString(*this, value, set_mode, source, error);
  }

  void CheckDefaultValueParsingRoundtrip() const override {
    impl_.CheckDefaultValueParsingRoundtrip(*this);
  }

 private:
  friend class FlagState<T>;

  void Destroy() const override { impl_.Destroy(); }

  void Read(void* dst) const override {
    impl_.Read(*this, dst, &flags_internal::FlagOps<T>);
  }
  flags_internal::FlagOpFn TypeId() const override {
    return &flags_internal::FlagOps<T>;
  }

  // Flag's data
  FlagImpl impl_;
};

template <typename T>
inline void FlagState<T>::Restore() const {
  if (flag_->RestoreState(*this)) {
    ABSL_INTERNAL_LOG(INFO,
                      absl::StrCat("Restore saved value of ", flag_->Name(),
                                   " to: ", flag_->CurrentValue()));
  }
}

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
