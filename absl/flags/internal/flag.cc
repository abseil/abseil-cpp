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

#include "absl/flags/internal/flag.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/const_init.h"
#include "absl/base/optimization.h"
#include "absl/flags/internal/commandlineflag.h"
#include "absl/flags/usage_config.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

// The help message indicating that the commandline flag has been
// 'stripped'. It will not show up when doing "-help" and its
// variants. The flag is stripped if ABSL_FLAGS_STRIP_HELP is set to 1
// before including absl/flags/flag.h
const char kStrippedFlagHelp[] = "\001\002\003\004 (unknown) \004\003\002\001";

namespace {

// Currently we only validate flag values for user-defined flag types.
bool ShouldValidateFlagValue(FlagStaticTypeId flag_type_id) {
#define DONT_VALIDATE(T) \
  if (flag_type_id == &FlagStaticTypeIdGen<T>) return false;
  ABSL_FLAGS_INTERNAL_BUILTIN_TYPES(DONT_VALIDATE)
#undef DONT_VALIDATE

  return true;
}

// RAII helper used to temporarily unlock and relock `absl::Mutex`.
// This is used when we need to ensure that locks are released while
// invoking user supplied callbacks and then reacquired, since callbacks may
// need to acquire these locks themselves.
class MutexRelock {
 public:
  explicit MutexRelock(absl::Mutex* mu) : mu_(mu) { mu_->Unlock(); }
  ~MutexRelock() { mu_->Lock(); }

  MutexRelock(const MutexRelock&) = delete;
  MutexRelock& operator=(const MutexRelock&) = delete;

 private:
  absl::Mutex* mu_;
};

}  // namespace

void FlagImpl::Init() {
  new (&data_guard_) absl::Mutex;

  // At this point the default_value_ always points to gen_func.
  std::unique_ptr<void, DynValueDeleter> init_value(
      (*default_value_.gen_func)(), DynValueDeleter{op_});
  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kHeapAllocated:
      value_.dynamic = init_value.release();
      break;
    case FlagValueStorageKind::kOneWordAtomic: {
      int64_t atomic_value;
      std::memcpy(&atomic_value, init_value.get(), Sizeof(op_));
      value_.one_word_atomic.store(atomic_value, std::memory_order_release);
      break;
    }
    case FlagValueStorageKind::kTwoWordsAtomic: {
      AlignedTwoWords atomic_value{0, 0};
      std::memcpy(&atomic_value, init_value.get(), Sizeof(op_));
      value_.two_words_atomic.store(atomic_value, std::memory_order_release);
      break;
    }
  }
}

absl::Mutex* FlagImpl::DataGuard() const {
  absl::call_once(const_cast<FlagImpl*>(this)->init_control_, &FlagImpl::Init,
                  const_cast<FlagImpl*>(this));

  // data_guard_ is initialized inside Init.
  return reinterpret_cast<absl::Mutex*>(&data_guard_);
}

void FlagImpl::AssertValidType(FlagStaticTypeId type_id) const {
  FlagStaticTypeId this_type_id = flags_internal::StaticTypeId(op_);

  // `type_id` is the type id corresponding to the declaration visibile at the
  // call site. `this_type_id` is the type id corresponding to the type stored
  // during flag definition. They must match for this operation to be
  // well-defined.
  if (ABSL_PREDICT_TRUE(type_id == this_type_id)) return;

  void* lhs_runtime_type_id = type_id();
  void* rhs_runtime_type_id = this_type_id();

  if (lhs_runtime_type_id == rhs_runtime_type_id) return;

#if defined(ABSL_FLAGS_INTERNAL_HAS_RTTI)
  if (*reinterpret_cast<std::type_info*>(lhs_runtime_type_id) ==
      *reinterpret_cast<std::type_info*>(rhs_runtime_type_id))
    return;
#endif

  ABSL_INTERNAL_LOG(
      FATAL, absl::StrCat("Flag '", Name(),
                          "' is defined as one type and declared as another"));
}

std::unique_ptr<void, DynValueDeleter> FlagImpl::MakeInitValue() const {
  void* res = nullptr;
  if (DefaultKind() == FlagDefaultKind::kDynamicValue) {
    res = flags_internal::Clone(op_, default_value_.dynamic_value);
  } else {
    res = (*default_value_.gen_func)();
  }
  return {res, DynValueDeleter{op_}};
}

void FlagImpl::StoreValue(const void* src) {
  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kHeapAllocated:
      Copy(op_, src, value_.dynamic);
      break;
    case FlagValueStorageKind::kOneWordAtomic: {
      int64_t one_word_val;
      std::memcpy(&one_word_val, src, Sizeof(op_));
      value_.one_word_atomic.store(one_word_val, std::memory_order_release);
      break;
    }
    case FlagValueStorageKind::kTwoWordsAtomic: {
      AlignedTwoWords two_words_val{0, 0};
      std::memcpy(&two_words_val, src, Sizeof(op_));
      value_.two_words_atomic.store(two_words_val, std::memory_order_release);
      break;
    }
  }

  modified_ = true;
  ++counter_;
  InvokeCallback();
}

absl::string_view FlagImpl::Name() const { return name_; }

std::string FlagImpl::Filename() const {
  return flags_internal::GetUsageConfig().normalize_filename(filename_);
}

std::string FlagImpl::Help() const {
  return HelpSourceKind() == FlagHelpKind::kLiteral ? help_.literal
                                                    : help_.gen_func();
}

bool FlagImpl::IsModified() const {
  absl::MutexLock l(DataGuard());
  return modified_;
}

bool FlagImpl::IsSpecifiedOnCommandLine() const {
  absl::MutexLock l(DataGuard());
  return on_command_line_;
}

std::string FlagImpl::DefaultValue() const {
  absl::MutexLock l(DataGuard());

  auto obj = MakeInitValue();
  return flags_internal::Unparse(op_, obj.get());
}

std::string FlagImpl::CurrentValue() const {
  DataGuard();  // Make sure flag initialized
  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kHeapAllocated: {
      absl::MutexLock l(DataGuard());
      return flags_internal::Unparse(op_, value_.dynamic);
    }
    case FlagValueStorageKind::kOneWordAtomic: {
      const auto one_word_val =
          value_.one_word_atomic.load(std::memory_order_acquire);
      return flags_internal::Unparse(op_, &one_word_val);
    }
    case FlagValueStorageKind::kTwoWordsAtomic: {
      const auto two_words_val =
          value_.two_words_atomic.load(std::memory_order_acquire);
      return flags_internal::Unparse(op_, &two_words_val);
    }
  }

  return "";
}

void FlagImpl::SetCallback(const FlagCallbackFunc mutation_callback) {
  absl::MutexLock l(DataGuard());

  if (callback_ == nullptr) {
    callback_ = new FlagCallback;
  }
  callback_->func = mutation_callback;

  InvokeCallback();
}

void FlagImpl::InvokeCallback() const {
  if (!callback_) return;

  // Make a copy of the C-style function pointer that we are about to invoke
  // before we release the lock guarding it.
  FlagCallbackFunc cb = callback_->func;

  // If the flag has a mutation callback this function invokes it. While the
  // callback is being invoked the primary flag's mutex is unlocked and it is
  // re-locked back after call to callback is completed. Callback invocation is
  // guarded by flag's secondary mutex instead which prevents concurrent
  // callback invocation. Note that it is possible for other thread to grab the
  // primary lock and update flag's value at any time during the callback
  // invocation. This is by design. Callback can get a value of the flag if
  // necessary, but it might be different from the value initiated the callback
  // and it also can be different by the time the callback invocation is
  // completed. Requires that *primary_lock be held in exclusive mode; it may be
  // released and reacquired by the implementation.
  MutexRelock relock(DataGuard());
  absl::MutexLock lock(&callback_->guard);
  cb();
}

bool FlagImpl::RestoreState(const void* value, bool modified,
                            bool on_command_line, int64_t counter) {
  {
    absl::MutexLock l(DataGuard());

    if (counter_ == counter) return false;
  }

  Write(value);

  {
    absl::MutexLock l(DataGuard());

    modified_ = modified;
    on_command_line_ = on_command_line;
  }

  return true;
}

// Attempts to parse supplied `value` string using parsing routine in the `flag`
// argument. If parsing successful, this function replaces the dst with newly
// parsed value. In case if any error is encountered in either step, the error
// message is stored in 'err'
std::unique_ptr<void, DynValueDeleter> FlagImpl::TryParse(
    absl::string_view value, std::string* err) const {
  std::unique_ptr<void, DynValueDeleter> tentative_value = MakeInitValue();

  std::string parse_err;
  if (!flags_internal::Parse(op_, value, tentative_value.get(), &parse_err)) {
    absl::string_view err_sep = parse_err.empty() ? "" : "; ";
    *err = absl::StrCat("Illegal value '", value, "' specified for flag '",
                        Name(), "'", err_sep, parse_err);
    return nullptr;
  }

  return tentative_value;
}

void FlagImpl::Read(void* dst) const {
  DataGuard();  // Make sure flag initialized
  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kHeapAllocated: {
      absl::MutexLock l(DataGuard());

      flags_internal::CopyConstruct(op_, value_.dynamic, dst);
      break;
    }
    case FlagValueStorageKind::kOneWordAtomic: {
      const auto one_word_val =
          value_.one_word_atomic.load(std::memory_order_acquire);
      std::memcpy(dst, &one_word_val, Sizeof(op_));
      break;
    }
    case FlagValueStorageKind::kTwoWordsAtomic: {
      const auto two_words_val =
          value_.two_words_atomic.load(std::memory_order_acquire);
      std::memcpy(dst, &two_words_val, Sizeof(op_));
      break;
    }
  }
}

void FlagImpl::Write(const void* src) {
  absl::MutexLock l(DataGuard());

  if (ShouldValidateFlagValue(flags_internal::StaticTypeId(op_))) {
    std::unique_ptr<void, DynValueDeleter> obj{flags_internal::Clone(op_, src),
                                               DynValueDeleter{op_}};
    std::string ignored_error;
    std::string src_as_str = flags_internal::Unparse(op_, src);
    if (!flags_internal::Parse(op_, src_as_str, obj.get(), &ignored_error)) {
      ABSL_INTERNAL_LOG(ERROR, absl::StrCat("Attempt to set flag '", Name(),
                                            "' to invalid value ", src_as_str));
    }
  }

  StoreValue(src);
}

// Sets the value of the flag based on specified string `value`. If the flag
// was successfully set to new value, it returns true. Otherwise, sets `err`
// to indicate the error, leaves the flag unchanged, and returns false. There
// are three ways to set the flag's value:
//  * Update the current flag value
//  * Update the flag's default value
//  * Update the current flag value if it was never set before
// The mode is selected based on 'set_mode' parameter.
bool FlagImpl::SetFromString(absl::string_view value, FlagSettingMode set_mode,
                             ValueSource source, std::string* err) {
  absl::MutexLock l(DataGuard());

  switch (set_mode) {
    case SET_FLAGS_VALUE: {
      // set or modify the flag's value
      auto tentative_value = TryParse(value, err);
      if (!tentative_value) return false;

      StoreValue(tentative_value.get());

      if (source == kCommandLine) {
        on_command_line_ = true;
      }
      break;
    }
    case SET_FLAG_IF_DEFAULT: {
      // set the flag's value, but only if it hasn't been set by someone else
      if (modified_) {
        // TODO(rogeeff): review and fix this semantic. Currently we do not fail
        // in this case if flag is modified. This is misleading since the flag's
        // value is not updated even though we return true.
        // *err = absl::StrCat(Name(), " is already set to ",
        //                     CurrentValue(), "\n");
        // return false;
        return true;
      }
      auto tentative_value = TryParse(value, err);
      if (!tentative_value) return false;

      StoreValue(tentative_value.get());
      break;
    }
    case SET_FLAGS_DEFAULT: {
      auto tentative_value = TryParse(value, err);
      if (!tentative_value) return false;

      if (DefaultKind() == FlagDefaultKind::kDynamicValue) {
        void* old_value = default_value_.dynamic_value;
        default_value_.dynamic_value = tentative_value.release();
        tentative_value.reset(old_value);
      } else {
        default_value_.dynamic_value = tentative_value.release();
        def_kind_ = static_cast<uint8_t>(FlagDefaultKind::kDynamicValue);
      }

      if (!modified_) {
        // Need to set both default value *and* current, in this case.
        StoreValue(default_value_.dynamic_value);
        modified_ = false;
      }
      break;
    }
  }

  return true;
}

void FlagImpl::CheckDefaultValueParsingRoundtrip() const {
  std::string v = DefaultValue();

  absl::MutexLock lock(DataGuard());

  auto dst = MakeInitValue();
  std::string error;
  if (!flags_internal::Parse(op_, v, dst.get(), &error)) {
    ABSL_INTERNAL_LOG(
        FATAL,
        absl::StrCat("Flag ", Name(), " (from ", Filename(),
                     "): std::string form of default value '", v,
                     "' could not be parsed; error=", error));
  }

  // We do not compare dst to def since parsing/unparsing may make
  // small changes, e.g., precision loss for floating point types.
}

bool FlagImpl::ValidateInputValue(absl::string_view value) const {
  absl::MutexLock l(DataGuard());

  auto obj = MakeInitValue();
  std::string ignored_error;
  return flags_internal::Parse(op_, value, obj.get(), &ignored_error);
}

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl
