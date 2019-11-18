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

#ifndef ABSL_FLAGS_INTERNAL_COMMANDLINEFLAG_H_
#define ABSL_FLAGS_INTERNAL_COMMANDLINEFLAG_H_

#include <memory>

#include "absl/base/macros.h"
#include "absl/flags/marshalling.h"
#include "absl/types/optional.h"

namespace absl {
namespace flags_internal {

// Type-specific operations, eg., parsing, copying, etc. are provided
// by function specific to that type with a signature matching FlagOpFn.
enum FlagOp {
  kDelete,
  kClone,
  kCopy,
  kCopyConstruct,
  kSizeof,
  kParse,
  kUnparse
};
using FlagOpFn = void* (*)(FlagOp, const void*, void*);
using FlagMarshallingOpFn = void* (*)(FlagOp, const void*, void*, void*);

// Options that control SetCommandLineOptionWithMode.
enum FlagSettingMode {
  // update the flag's value unconditionally (can call this multiple times).
  SET_FLAGS_VALUE,
  // update the flag's value, but *only if* it has not yet been updated
  // with SET_FLAGS_VALUE, SET_FLAG_IF_DEFAULT, or "FLAGS_xxx = nondef".
  SET_FLAG_IF_DEFAULT,
  // set the flag's default value to this.  If the flag has not been updated
  // yet (via SET_FLAGS_VALUE, SET_FLAG_IF_DEFAULT, or "FLAGS_xxx = nondef")
  // change the flag's current value to the new default value as well.
  SET_FLAGS_DEFAULT
};

// Options that control SetFromString: Source of a value.
enum ValueSource {
  // Flag is being set by value specified on a command line.
  kCommandLine,
  // Flag is being set by value specified in the code.
  kProgrammaticChange,
};

extern const char kStrippedFlagHelp[];

// The per-type function
template <typename T>
void* FlagOps(FlagOp op, const void* v1, void* v2) {
  switch (op) {
    case kDelete:
      delete static_cast<const T*>(v1);
      return nullptr;
    case kClone:
      return new T(*static_cast<const T*>(v1));
    case kCopy:
      *static_cast<T*>(v2) = *static_cast<const T*>(v1);
      return nullptr;
    case kCopyConstruct:
      new (v2) T(*static_cast<const T*>(v1));
      return nullptr;
    case kSizeof:
      return reinterpret_cast<void*>(sizeof(T));
    default:
      return nullptr;
  }
}

template <typename T>
void* FlagMarshallingOps(FlagOp op, const void* v1, void* v2, void* v3) {
  switch (op) {
    case kParse: {
      // initialize the temporary instance of type T based on current value in
      // destination (which is going to be flag's default value).
      T temp(*static_cast<T*>(v2));
      if (!absl::ParseFlag<T>(*static_cast<const absl::string_view*>(v1), &temp,
                              static_cast<std::string*>(v3))) {
        return nullptr;
      }
      *static_cast<T*>(v2) = std::move(temp);
      return v2;
    }
    case kUnparse:
      *static_cast<std::string*>(v2) =
          absl::UnparseFlag<T>(*static_cast<const T*>(v1));
      return nullptr;
    default:
      return nullptr;
  }
}

// Functions that invoke flag-type-specific operations.
inline void Delete(FlagOpFn op, const void* obj) {
  op(flags_internal::kDelete, obj, nullptr);
}

inline void* Clone(FlagOpFn op, const void* obj) {
  return op(flags_internal::kClone, obj, nullptr);
}

inline void Copy(FlagOpFn op, const void* src, void* dst) {
  op(flags_internal::kCopy, src, dst);
}

inline void CopyConstruct(FlagOpFn op, const void* src, void* dst) {
  op(flags_internal::kCopyConstruct, src, dst);
}

inline bool Parse(FlagMarshallingOpFn op, absl::string_view text, void* dst,
                  std::string* error) {
  return op(flags_internal::kParse, &text, dst, error) != nullptr;
}

inline std::string Unparse(FlagMarshallingOpFn op, const void* val) {
  std::string result;
  op(flags_internal::kUnparse, val, &result, nullptr);
  return result;
}

inline size_t Sizeof(FlagOpFn op) {
  // This sequence of casts reverses the sequence from base::internal::FlagOps()
  return static_cast<size_t>(reinterpret_cast<intptr_t>(
      op(flags_internal::kSizeof, nullptr, nullptr)));
}

// Handle to FlagState objects. Specific flag state objects will restore state
// of a flag produced this flag state from method CommandLineFlag::SaveState().
class FlagStateInterface {
 public:
  virtual ~FlagStateInterface() {}

  // Restores the flag originated this object to the saved state.
  virtual void Restore() const = 0;
};

// Holds all information for a flag.
class CommandLineFlag {
 public:
  constexpr CommandLineFlag(const char* name, const char* filename)
      : name_(name), filename_(filename) {}

  // Virtual destructor
  virtual void Destroy() const = 0;

  // Not copyable/assignable.
  CommandLineFlag(const CommandLineFlag&) = delete;
  CommandLineFlag& operator=(const CommandLineFlag&) = delete;

  // Non-polymorphic access methods.
  absl::string_view Name() const { return name_; }
  absl::string_view Typename() const;
  std::string Filename() const;

  // Return true iff flag has type T.
  template <typename T>
  inline bool IsOfType() const {
    return TypeId() == &flags_internal::FlagOps<T>;
  }

  // Attempts to retrieve the flag value. Returns value on success,
  // absl::nullopt otherwise.
  template <typename T>
  absl::optional<T> Get() const {
    if (IsRetired() || !IsOfType<T>()) {
      return absl::nullopt;
    }

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

    Read(&u.value);
    return std::move(u.value);
  }

  // Polymorphic access methods

  // Returns help message associated with this flag
  virtual std::string Help() const = 0;
  // Returns true iff this object corresponds to retired flag
  virtual bool IsRetired() const { return false; }
  // Returns true iff this is a handle to an Abseil Flag.
  virtual bool IsAbseilFlag() const { return true; }
  // Returns id of the flag's value type.
  virtual flags_internal::FlagOpFn TypeId() const = 0;
  virtual bool IsModified() const = 0;
  virtual bool IsSpecifiedOnCommandLine() const = 0;
  virtual std::string DefaultValue() const = 0;
  virtual std::string CurrentValue() const = 0;

  // Interfaces to operate on validators.
  virtual bool ValidateInputValue(absl::string_view value) const = 0;

  // Interface to save flag to some persistent state. Returns current flag state
  // or nullptr if flag does not support saving and restoring a state.
  virtual std::unique_ptr<FlagStateInterface> SaveState() = 0;

  // Sets the value of the flag based on specified std::string `value`. If the flag
  // was successfully set to new value, it returns true. Otherwise, sets `error`
  // to indicate the error, leaves the flag unchanged, and returns false. There
  // are three ways to set the flag's value:
  //  * Update the current flag value
  //  * Update the flag's default value
  //  * Update the current flag value if it was never set before
  // The mode is selected based on `set_mode` parameter.
  virtual bool SetFromString(absl::string_view value,
                             flags_internal::FlagSettingMode set_mode,
                             flags_internal::ValueSource source,
                             std::string* error) = 0;

  // Checks that flags default value can be converted to std::string and back to the
  // flag's value type.
  virtual void CheckDefaultValueParsingRoundtrip() const = 0;

 protected:
  ~CommandLineFlag() = default;

  // Constant configuration for a particular flag.
  const char* const name_;      // Flags name passed to ABSL_FLAG as second arg.
  const char* const filename_;  // The file name where ABSL_FLAG resides.

 private:
  // Copy-construct a new value of the flag's type in a memory referenced by
  // the dst based on the current flag's value.
  virtual void Read(void* dst) const = 0;
};

// This macro is the "source of truth" for the list of supported flag types we
// expect to perform lock free operations on. Specifically it generates code,
// a one argument macro operating on a type, supplied as a macro argument, for
// each type in the list.
#define ABSL_FLAGS_INTERNAL_FOR_EACH_LOCK_FREE(A) \
  A(bool)                                         \
  A(short)                                        \
  A(unsigned short)                               \
  A(int)                                          \
  A(unsigned int)                                 \
  A(long)                                         \
  A(unsigned long)                                \
  A(long long)                                    \
  A(unsigned long long)                           \
  A(double)                                       \
  A(float)

}  // namespace flags_internal
}  // namespace absl

#endif  // ABSL_FLAGS_INTERNAL_COMMANDLINEFLAG_H_
