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

#include "absl/flags/internal/commandlineflag.h"

#include <cassert>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/flags/config.h"
#include "absl/flags/usage_config.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

namespace absl {
namespace flags_internal {

// The help message indicating that the commandline flag has been
// 'stripped'. It will not show up when doing "-help" and its
// variants. The flag is stripped if ABSL_FLAGS_STRIP_HELP is set to 1
// before including absl/flags/flag.h

// This is used by this file, and also in commandlineflags_reporting.cc
const char kStrippedFlagHelp[] = "\001\002\003\004 (unknown) \004\003\002\001";

namespace {

void StoreAtomic(CommandLineFlag* flag, const void* data, size_t size) {
  int64_t t = 0;
  assert(size <= sizeof(int64_t));
  memcpy(&t, data, size);
  flag->atomic.store(t, std::memory_order_release);
}

// If the flag has a mutation callback this function invokes it. While the
// callback is being invoked the primary flag's mutex is unlocked and it is
// re-locked back after call to callback is completed. Callback invocation is
// guarded by flag's secondary mutex instead which prevents concurrent callback
// invocation. Note that it is possible for other thread to grab the primary
// lock and update flag's value at any time during the callback invocation.
// This is by design. Callback can get a value of the flag if necessary, but it
// might be different from the value initiated the callback and it also can be
// different by the time the callback invocation is completed.
// Requires that *primary_lock be held in exclusive mode; it may be released
// and reacquired by the implementation.
void InvokeCallback(CommandLineFlag* flag, absl::Mutex* primary_lock)
    EXCLUSIVE_LOCKS_REQUIRED(primary_lock) {
  if (!flag->callback) return;

  // The callback lock is guaranteed initialized, because *primary_lock exists.
  absl::Mutex* callback_mu = &flag->locks->callback_mu;

  // When executing the callback we need the primary flag's mutex to be unlocked
  // so that callback can retrieve the flag's value.
  primary_lock->Unlock();

  {
    absl::MutexLock lock(callback_mu);

    flag->callback();
  }

  primary_lock->Lock();
}

// Currently we only validate flag values for user-defined flag types.
bool ShouldValidateFlagValue(const CommandLineFlag& flag) {
#define DONT_VALIDATE(T) \
  if (flag.IsOfType<T>()) return false;
  ABSL_FLAGS_INTERNAL_FOR_EACH_LOCK_FREE(DONT_VALIDATE)
  DONT_VALIDATE(std::string)
  DONT_VALIDATE(std::vector<std::string>)
#undef DONT_VALIDATE

  return true;
}

}  // namespace

// Update any copy of the flag value that is stored in an atomic word.
// In addition if flag has a mutation callback this function invokes it.
void UpdateCopy(CommandLineFlag* flag, absl::Mutex* primary_lock)
    EXCLUSIVE_LOCKS_REQUIRED(primary_lock) {
#define STORE_ATOMIC(T)                      \
  else if (flag->IsOfType<T>()) {            \
    StoreAtomic(flag, flag->cur, sizeof(T)); \
  }

  if (false) {
  }
  ABSL_FLAGS_INTERNAL_FOR_EACH_LOCK_FREE(STORE_ATOMIC)
#undef STORE_ATOMIC

  InvokeCallback(flag, primary_lock);
}

// Ensure that the lazily initialized fields of *flag have been initialized,
// and return &flag->locks->primary_mu.
absl::Mutex* InitFlagIfNecessary(CommandLineFlag* flag)
    LOCK_RETURNED(flag->locks->primary_mu) {
  absl::Mutex* mu;
  if (!flag->inited.load(std::memory_order_acquire)) {
    // Need to initialize lazily initialized fields.
    ABSL_CONST_INIT static absl::Mutex init_lock(absl::kConstInit);
    init_lock.Lock();
    if (flag->locks == nullptr) {  // Must initialize Mutexes for this flag.
      flag->locks = new flags_internal::CommandLineFlagLocks;
    }
    mu = &flag->locks->primary_mu;
    init_lock.Unlock();
    mu->Lock();
    if (!flag->retired &&
        flag->def == nullptr) {  // Need to initialize def and cur fields.
      flag->def = (*flag->make_init_value)();
      flag->cur = Clone(flag->op, flag->def);
      UpdateCopy(flag, mu);
    }
    mu->Unlock();
    flag->inited.store(true, std::memory_order_release);
  } else {  // All fields initialized; flag->locks is therefore safe to read.
    mu = &flag->locks->primary_mu;
  }
  return mu;
}

// Return true iff flag value was changed via direct-access.
bool ChangedDirectly(CommandLineFlag* flag, const void* a, const void* b) {
  if (!flag->IsAbseilFlag()) {
    // Need to compare values for direct-access flags.
#define CHANGED_FOR_TYPE(T)                                                  \
  if (flag->IsOfType<T>()) {                                                 \
    return *reinterpret_cast<const T*>(a) != *reinterpret_cast<const T*>(b); \
  }

    CHANGED_FOR_TYPE(bool);
    CHANGED_FOR_TYPE(int32_t);
    CHANGED_FOR_TYPE(int64_t);
    CHANGED_FOR_TYPE(uint64_t);
    CHANGED_FOR_TYPE(double);
    CHANGED_FOR_TYPE(std::string);
#undef CHANGED_FOR_TYPE
  }
  return false;
}

// Direct-access flags can be modified without going through the
// flag API. Detect such changes and updated the modified bit.
void UpdateModifiedBit(CommandLineFlag* flag) {
  if (!flag->IsAbseilFlag()) {
    absl::MutexLock l(InitFlagIfNecessary(flag));
    if (!flag->modified && ChangedDirectly(flag, flag->cur, flag->def)) {
      flag->modified = true;
    }
  }
}

bool Validate(CommandLineFlag*, const void*) {
  return true;
}

std::string HelpText::GetHelpText() const {
  if (help_function_) return help_function_();
  if (help_message_) return help_message_;

  return {};
}

const int64_t CommandLineFlag::kAtomicInit;

void CommandLineFlag::Read(void* dst,
                           const flags_internal::FlagOpFn dst_op) const {
  absl::ReaderMutexLock l(
      InitFlagIfNecessary(const_cast<CommandLineFlag*>(this)));

  // `dst_op` is the unmarshaling operation corresponding to the declaration
  // visibile at the call site. `op` is the Flag's defined unmarshalling
  // operation. They must match for this operation to be well-defined.
  if (ABSL_PREDICT_FALSE(dst_op != op)) {
    ABSL_INTERNAL_LOG(
        ERROR,
        absl::StrCat("Flag '", name,
                     "' is defined as one type and declared as another"));
  }
  CopyConstruct(op, cur, dst);
}

void CommandLineFlag::Write(const void* src,
                            const flags_internal::FlagOpFn src_op) {
  absl::Mutex* mu = InitFlagIfNecessary(this);
  absl::MutexLock l(mu);

  // `src_op` is the marshalling operation corresponding to the declaration
  // visible at the call site. `op` is the Flag's defined marshalling operation.
  // They must match for this operation to be well-defined.
  if (ABSL_PREDICT_FALSE(src_op != op)) {
    ABSL_INTERNAL_LOG(
        ERROR,
        absl::StrCat("Flag '", name,
                     "' is defined as one type and declared as another"));
  }

  if (ShouldValidateFlagValue(*this)) {
    void* obj = Clone(op, src);
    std::string ignored_error;
    std::string src_as_str = Unparse(marshalling_op, src);
    if (!Parse(marshalling_op, src_as_str, obj, &ignored_error) ||
        !Validate(this, obj)) {
      ABSL_INTERNAL_LOG(ERROR, absl::StrCat("Attempt to set flag '", name,
                                            "' to invalid value ", src_as_str));
    }
    Delete(op, obj);
  }

  modified = true;
  counter++;
  Copy(op, src, cur);

  UpdateCopy(this, mu);
}

absl::string_view CommandLineFlag::Typename() const {
  // We do not store/report type in Abseil Flags, so that user do not rely on in
  // at runtime
  if (IsAbseilFlag() || IsRetired()) return "";

#define HANDLE_V1_BUILTIN_TYPE(t) \
  if (IsOfType<t>()) {            \
    return #t;                    \
  }

  HANDLE_V1_BUILTIN_TYPE(bool);
  HANDLE_V1_BUILTIN_TYPE(int32_t);
  HANDLE_V1_BUILTIN_TYPE(int64_t);
  HANDLE_V1_BUILTIN_TYPE(uint64_t);
  HANDLE_V1_BUILTIN_TYPE(double);
#undef HANDLE_V1_BUILTIN_TYPE

  if (IsOfType<std::string>()) {
    return "string";
  }

  return "";
}

std::string CommandLineFlag::Filename() const {
  return flags_internal::GetUsageConfig().normalize_filename(this->filename);
}

std::string CommandLineFlag::DefaultValue() const {
  return Unparse(this->marshalling_op, this->def);
}

std::string CommandLineFlag::CurrentValue() const {
  return Unparse(this->marshalling_op, this->cur);
}

void CommandLineFlag::SetCallback(
    const flags_internal::FlagCallback mutation_callback) {
  absl::Mutex* mu = InitFlagIfNecessary(this);
  absl::MutexLock l(mu);

  callback = mutation_callback;

  InvokeCallback(this, mu);
}

// Attempts to parse supplied `value` string using parsing routine in the `flag`
// argument. If parsing is successful, it will try to validate that the parsed
// value is valid for the specified 'flag'. Finally this function stores the
// parsed value in 'dst' assuming it is a pointer to the flag's value type. In
// case if any error is encountered in either step, the error message is stored
// in 'err'
static bool TryParseLocked(CommandLineFlag* flag, void* dst,
                           absl::string_view value, std::string* err) {
  void* tentative_value = Clone(flag->op, flag->def);
  std::string parse_err;
  if (!Parse(flag->marshalling_op, value, tentative_value, &parse_err)) {
    auto type_name = flag->Typename();
    absl::string_view err_sep = parse_err.empty() ? "" : "; ";
    absl::string_view typename_sep = type_name.empty() ? "" : " ";
    *err = absl::StrCat("Illegal value '", value, "' specified for",
                        typename_sep, type_name, " flag '", flag->Name(), "'",
                        err_sep, parse_err);
    Delete(flag->op, tentative_value);
    return false;
  }

  if (!Validate(flag, tentative_value)) {
    *err = absl::StrCat("Failed validation of new value '",
                        Unparse(flag->marshalling_op, tentative_value),
                        "' for flag '", flag->Name(), "'");
    Delete(flag->op, tentative_value);
    return false;
  }

  flag->counter++;
  Copy(flag->op, tentative_value, dst);
  Delete(flag->op, tentative_value);
  return true;
}

// Sets the value of the flag based on specified string `value`. If the flag
// was successfully set to new value, it returns true. Otherwise, sets `err`
// to indicate the error, leaves the flag unchanged, and returns false. There
// are three ways to set the flag's value:
//  * Update the current flag value
//  * Update the flag's default value
//  * Update the current flag value if it was never set before
// The mode is selected based on 'set_mode' parameter.
bool CommandLineFlag::SetFromString(absl::string_view value,
                                    FlagSettingMode set_mode,
                                    ValueSource source, std::string* err) {
  if (IsRetired()) return false;

  UpdateModifiedBit(this);

  absl::Mutex* mu = InitFlagIfNecessary(this);
  absl::MutexLock l(mu);

  switch (set_mode) {
    case SET_FLAGS_VALUE: {
      // set or modify the flag's value
      if (!TryParseLocked(this, this->cur, value, err)) return false;
      this->modified = true;
      UpdateCopy(this, mu);

      if (source == kCommandLine) {
        this->on_command_line = true;
      }
      break;
    }
    case SET_FLAG_IF_DEFAULT: {
      // set the flag's value, but only if it hasn't been set by someone else
      if (!this->modified) {
        if (!TryParseLocked(this, this->cur, value, err)) return false;
        this->modified = true;
        UpdateCopy(this, mu);
      } else {
        // TODO(rogeeff): review and fix this semantic. Currently we do not fail
        // in this case if flag is modified. This is misleading since the flag's
        // value is not updated even though we return true.
        // *err = absl::StrCat(this->Name(), " is already set to ",
        //                     CurrentValue(), "\n");
        // return false;
        return true;
      }
      break;
    }
    case SET_FLAGS_DEFAULT: {
      // modify the flag's default-value
      if (!TryParseLocked(this, this->def, value, err)) return false;

      if (!this->modified) {
        // Need to set both defvalue *and* current, in this case
        Copy(this->op, this->def, this->cur);
        UpdateCopy(this, mu);
      }
      break;
    }
    default: {
      // unknown set_mode
      assert(false);
      return false;
    }
  }

  return true;
}

}  // namespace flags_internal
}  // namespace absl
