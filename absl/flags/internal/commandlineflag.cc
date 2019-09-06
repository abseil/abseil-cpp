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

absl::Mutex* InitFlag(CommandLineFlag* flag) {
  ABSL_CONST_INIT static absl::Mutex init_lock(absl::kConstInit);
  absl::Mutex* mu;

  {
    absl::MutexLock lock(&init_lock);

    if (flag->locks_ == nullptr) {  // Must initialize Mutexes for this flag.
      flag->locks_ = new flags_internal::CommandLineFlagLocks;
    }

    mu = &flag->locks_->primary_mu;
  }

  {
    absl::MutexLock lock(mu);

    if (!flag->IsRetired() && flag->def_ == nullptr) {
      // Need to initialize def and cur fields.
      flag->def_ = (*flag->make_init_value_)();
      flag->cur_ = Clone(flag->op_, flag->def_);
      UpdateCopy(flag);
      flag->inited_.store(true, std::memory_order_release);
      flag->InvokeCallback();
    }
  }

  flag->inited_.store(true, std::memory_order_release);
  return mu;
}

// Ensure that the lazily initialized fields of *flag have been initialized,
// and return &flag->locks_->primary_mu.
absl::Mutex* CommandLineFlag::InitFlagIfNecessary() const
    ABSL_LOCK_RETURNED(locks_->primary_mu) {
  if (!inited_.load(std::memory_order_acquire)) {
    return InitFlag(const_cast<CommandLineFlag*>(this));
  }

  // All fields initialized; locks_ is therefore safe to read.
  return &locks_->primary_mu;
}

bool CommandLineFlag::IsModified() const {
  absl::MutexLock l(InitFlagIfNecessary());
  return modified_;
}

void CommandLineFlag::SetModified(bool is_modified) {
  absl::MutexLock l(InitFlagIfNecessary());
  modified_ = is_modified;
}

bool CommandLineFlag::IsSpecifiedOnCommandLine() const {
  absl::MutexLock l(InitFlagIfNecessary());
  return on_command_line_;
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
  return flags_internal::GetUsageConfig().normalize_filename(filename_);
}

std::string CommandLineFlag::DefaultValue() const {
  absl::MutexLock l(InitFlagIfNecessary());

  return Unparse(marshalling_op_, def_);
}

std::string CommandLineFlag::CurrentValue() const {
  absl::MutexLock l(InitFlagIfNecessary());

  return Unparse(marshalling_op_, cur_);
}

// Attempts to parse supplied `value` string using parsing routine in the `flag`
// argument. If parsing is successful, it will try to validate that the parsed
// value is valid for the specified 'flag'. Finally this function stores the
// parsed value in 'dst' assuming it is a pointer to the flag's value type. In
// case if any error is encountered in either step, the error message is stored
// in 'err'
bool TryParseLocked(CommandLineFlag* flag, void* dst, absl::string_view value,
                    std::string* err)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(flag->locks_->primary_mu) {
  void* tentative_value = Clone(flag->op_, flag->def_);
  std::string parse_err;
  if (!Parse(flag->marshalling_op_, value, tentative_value, &parse_err)) {
    auto type_name = flag->Typename();
    absl::string_view err_sep = parse_err.empty() ? "" : "; ";
    absl::string_view typename_sep = type_name.empty() ? "" : " ";
    *err = absl::StrCat("Illegal value '", value, "' specified for",
                        typename_sep, type_name, " flag '", flag->Name(), "'",
                        err_sep, parse_err);
    Delete(flag->op_, tentative_value);
    return false;
  }

  if (!flag->InvokeValidator(tentative_value)) {
    *err = absl::StrCat("Failed validation of new value '",
                        Unparse(flag->marshalling_op_, tentative_value),
                        "' for flag '", flag->Name(), "'");
    Delete(flag->op_, tentative_value);
    return false;
  }

  flag->counter_++;
  Copy(flag->op_, tentative_value, dst);
  Delete(flag->op_, tentative_value);
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

  absl::MutexLock l(InitFlagIfNecessary());

  // Direct-access flags can be modified without going through the
  // flag API. Detect such changes and update the flag->modified_ bit.
  if (!IsAbseilFlag()) {
    if (!modified_ && ChangedDirectly(this, cur_, def_)) {
      modified_ = true;
    }
  }

  switch (set_mode) {
    case SET_FLAGS_VALUE: {
      // set or modify the flag's value
      if (!TryParseLocked(this, cur_, value, err)) return false;
      modified_ = true;
      UpdateCopy(this);
      InvokeCallback();

      if (source == kCommandLine) {
        on_command_line_ = true;
      }
      break;
    }
    case SET_FLAG_IF_DEFAULT: {
      // set the flag's value, but only if it hasn't been set by someone else
      if (!modified_) {
        if (!TryParseLocked(this, cur_, value, err)) return false;
        modified_ = true;
        UpdateCopy(this);
        InvokeCallback();
      } else {
        // TODO(rogeeff): review and fix this semantic. Currently we do not fail
        // in this case if flag is modified. This is misleading since the flag's
        // value is not updated even though we return true.
        // *err = absl::StrCat(Name(), " is already set to ",
        //                     CurrentValue(), "\n");
        // return false;
        return true;
      }
      break;
    }
    case SET_FLAGS_DEFAULT: {
      // modify the flag's default-value
      if (!TryParseLocked(this, def_, value, err)) return false;

      if (!modified_) {
        // Need to set both defvalue *and* current, in this case
        Copy(op_, def_, cur_);
        UpdateCopy(this);
        InvokeCallback();
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

void CommandLineFlag::CheckDefaultValueParsingRoundtrip() const {
  std::string v = DefaultValue();

  absl::MutexLock lock(InitFlagIfNecessary());

  void* dst = Clone(op_, def_);
  std::string error;
  if (!flags_internal::Parse(marshalling_op_, v, dst, &error)) {
    ABSL_INTERNAL_LOG(
        FATAL,
        absl::StrCat("Flag ", Name(), " (from ", Filename(),
                     "): std::string form of default value '", v,
                     "' could not be parsed; error=", error));
  }

  // We do not compare dst to def since parsing/unparsing may make
  // small changes, e.g., precision loss for floating point types.
  Delete(op_, dst);
}

bool CommandLineFlag::ValidateDefaultValue() const {
  absl::MutexLock lock(InitFlagIfNecessary());
  return InvokeValidator(def_);
}

bool CommandLineFlag::ValidateInputValue(absl::string_view value) const {
  absl::MutexLock l(InitFlagIfNecessary());  // protect default value access

  void* obj = Clone(op_, def_);
  std::string ignored_error;
  const bool result =
      flags_internal::Parse(marshalling_op_, value, obj, &ignored_error) &&
      InvokeValidator(obj);
  Delete(op_, obj);
  return result;
}

void CommandLineFlag::Read(void* dst,
                           const flags_internal::FlagOpFn dst_op) const {
  absl::ReaderMutexLock l(InitFlagIfNecessary());

  // `dst_op` is the unmarshaling operation corresponding to the declaration
  // visibile at the call site. `op` is the Flag's defined unmarshalling
  // operation. They must match for this operation to be well-defined.
  if (ABSL_PREDICT_FALSE(dst_op != op_)) {
    ABSL_INTERNAL_LOG(
        ERROR,
        absl::StrCat("Flag '", Name(),
                     "' is defined as one type and declared as another"));
  }
  CopyConstruct(op_, cur_, dst);
}

void CommandLineFlag::Write(const void* src,
                            const flags_internal::FlagOpFn src_op) {
  absl::MutexLock l(InitFlagIfNecessary());

  // `src_op` is the marshalling operation corresponding to the declaration
  // visible at the call site. `op` is the Flag's defined marshalling operation.
  // They must match for this operation to be well-defined.
  if (ABSL_PREDICT_FALSE(src_op != op_)) {
    ABSL_INTERNAL_LOG(
        ERROR,
        absl::StrCat("Flag '", Name(),
                     "' is defined as one type and declared as another"));
  }

  if (ShouldValidateFlagValue(*this)) {
    void* obj = Clone(op_, src);
    std::string ignored_error;
    std::string src_as_str = Unparse(marshalling_op_, src);
    if (!Parse(marshalling_op_, src_as_str, obj, &ignored_error) ||
        !InvokeValidator(obj)) {
      ABSL_INTERNAL_LOG(ERROR, absl::StrCat("Attempt to set flag '", Name(),
                                            "' to invalid value ", src_as_str));
    }
    Delete(op_, obj);
  }

  modified_ = true;
  counter_++;
  Copy(op_, src, cur_);

  UpdateCopy(this);
  InvokeCallback();
}

std::string HelpText::GetHelpText() const {
  if (help_function_) return help_function_();
  if (help_message_) return help_message_;

  return {};
}

// Update any copy of the flag value that is stored in an atomic word.
// In addition if flag has a mutation callback this function invokes it.
void UpdateCopy(CommandLineFlag* flag) {
#define STORE_ATOMIC(T)           \
  else if (flag->IsOfType<T>()) { \
    flag->StoreAtomic();          \
  }

  if (false) {
  }
  ABSL_FLAGS_INTERNAL_FOR_EACH_LOCK_FREE(STORE_ATOMIC)
#undef STORE_ATOMIC
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

}  // namespace flags_internal
}  // namespace absl
