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

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/config.h"
#include "absl/flags/internal/commandlineflag.h"
#include "absl/flags/internal/registry.h"
#include "absl/flags/internal/sequence_lock.h"
#include "absl/flags/marshalling.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

class Duration;
class Time;

///////////////////////////////////////////////////////////////////////////////
// Forward declaration of absl::Flag<T> public API.
namespace flags_internal {
template <typename T>
class Flag;
}  // namespace flags_internal

template <typename T>
using Flag = flags_internal::Flag<T>;

template <typename T>
[[nodiscard]] T GetFlag(const absl::Flag<T>& flag);

template <typename T>
void SetFlag(absl::Flag<T>* flag, const T& v);

template <typename T, typename V>
void SetFlag(absl::Flag<T>* flag, const V& v);

template <typename U>
const CommandLineFlag& GetFlagReflectionHandle(const absl::Flag<U>& f);

///////////////////////////////////////////////////////////////////////////////
// Flag value type operations, eg., parsing, copying, etc. are provided
// by function specific to that type with a signature matching FlagOpFn.

namespace flags_internal {

enum class FlagOp {
  kAlloc,
  kDelete,
  kCopy,
  kCopyConstruct,
  kSizeof,
  kFastTypeId,
  kRuntimeTypeId,
  kParse,
  kUnparse,
  kValueOffset,
  kTypeName,
};
using FlagOpFn = void* (*)(FlagOp, const void*, void*, void*);

// Forward declaration for Flag value specific operations.
template <typename T>
void* FlagOps(FlagOp op, const void* v1, void* v2, void* v3);

// Allocate aligned memory for a flag value.
inline void* Alloc(FlagOpFn op) {
  return op(FlagOp::kAlloc, nullptr, nullptr, nullptr);
}
// Deletes memory interpreting obj as flag value type pointer.
inline void Delete(FlagOpFn op, void* obj) {
  op(FlagOp::kDelete, nullptr, obj, nullptr);
}
// Copies src to dst interpreting as flag value type pointers.
inline void Copy(FlagOpFn op, const void* src, void* dst) {
  op(FlagOp::kCopy, src, dst, nullptr);
}
// Construct a copy of flag value in a location pointed by dst
// based on src - pointer to the flag's value.
inline void CopyConstruct(FlagOpFn op, const void* src, void* dst) {
  op(FlagOp::kCopyConstruct, src, dst, nullptr);
}
// Makes a copy of flag value pointed by obj.
inline void* Clone(FlagOpFn op, const void* obj) {
  void* res = flags_internal::Alloc(op);
  flags_internal::CopyConstruct(op, obj, res);
  return res;
}
// Returns true if parsing of input text is successful.
inline bool Parse(FlagOpFn op, absl::string_view text, void* dst,
                  std::string* error) {
  return op(FlagOp::kParse, &text, dst, error) != nullptr;
}
// Returns string representing supplied value.
inline std::string Unparse(FlagOpFn op, const void* val) {
  std::string result;
  op(FlagOp::kUnparse, val, &result, nullptr);
  return result;
}
// Returns size of flag value type.
inline size_t Sizeof(FlagOpFn op) {
  // This sequence of casts reverses the sequence from
  // `flags_internal::FlagOps()`
  return static_cast<size_t>(reinterpret_cast<intptr_t>(
      op(FlagOp::kSizeof, nullptr, nullptr, nullptr)));
}
// Returns fast type id corresponding to the value type.
inline FlagFastTypeId FastTypeId(FlagOpFn op) {
  return absl::bit_cast<FlagFastTypeId>(
      op(FlagOp::kFastTypeId, nullptr, nullptr, nullptr));
}
// Returns fast type id corresponding to the value type.
inline const std::type_info* RuntimeTypeId(FlagOpFn op) {
  return reinterpret_cast<const std::type_info*>(
      op(FlagOp::kRuntimeTypeId, nullptr, nullptr, nullptr));
}
// Returns offset of the field value_ from the field impl_ inside of
// absl::Flag<T> data. Given FlagImpl pointer p you can get the
// location of the corresponding value as:
//      reinterpret_cast<char*>(p) + ValueOffset().
inline ptrdiff_t ValueOffset(FlagOpFn op) {
  // This sequence of casts reverses the sequence from
  // `flags_internal::FlagOps()`
  return static_cast<ptrdiff_t>(reinterpret_cast<intptr_t>(
      op(FlagOp::kValueOffset, nullptr, nullptr, nullptr)));
}
// Returns string representation of the flag's value type.
inline absl::string_view TypeName(FlagOpFn op) {
  absl::string_view result;
  op(FlagOp::kTypeName, nullptr, &result, nullptr);
  return result;
}

template <typename T>
constexpr absl::string_view GenTypeName() {
#if ABSL_FLAGS_STRIP_NAMES
  return "";
#elif defined(__clang__) || defined(__GNUC__)
  absl::string_view name = __PRETTY_FUNCTION__;
  constexpr absl::string_view kKey = "T = ";
  const size_t start = name.find(kKey);
  if (start == absl::string_view::npos) return "";
  name.remove_prefix(start + kKey.size());
  const size_t end = name.find_first_of(";]");
  return end == absl::string_view::npos ? name : name.substr(0, end);
#else
  return "";
#endif
}

// Returns an address of RTTI's typeid(T).
template <typename T>
inline const std::type_info* GenRuntimeTypeId() {
#ifdef ABSL_INTERNAL_HAS_RTTI
  return &typeid(T);
#else
  return nullptr;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Flag help auxiliary structs.

// This is help argument for absl::Flag encapsulating the string literal pointer
// or pointer to function generating it as well as enum discriminating two
// cases.
using HelpGenFunc = std::string (*)();

template <size_t N>
struct FixedCharArray {
  char value[N];

  template <size_t... I>
  static constexpr FixedCharArray<N> FromLiteralString(
      absl::string_view str, std::index_sequence<I...>) {
    return (void)str, FixedCharArray<N>({{str[I]..., '\0'}});
  }
};

template <typename Gen, size_t N = Gen::Value().size()>
constexpr FixedCharArray<N + 1> HelpStringAsArray(int) {
  return FixedCharArray<N + 1>::FromLiteralString(
      Gen::Value(), std::make_index_sequence<N>{});
}

template <typename Gen>
constexpr std::false_type HelpStringAsArray(char) {
  return std::false_type{};
}

union FlagHelpMsg {
  constexpr explicit FlagHelpMsg(const char* help_msg) : literal(help_msg) {}
  constexpr explicit FlagHelpMsg(HelpGenFunc help_gen) : gen_func(help_gen) {}

  const char* literal;
  HelpGenFunc gen_func;
};

enum class FlagHelpKind : uint8_t { kLiteral = 0, kGenFunc = 1 };

struct FlagHelpArg {
  FlagHelpMsg source;
  FlagHelpKind kind;
};

extern const char kStrippedFlagHelp[];

// These two HelpArg overloads allows us to select at compile time one of two
// way to pass Help argument to absl::Flag. We'll be passing
// AbslFlagHelpGenFor##name as Gen and integer 0 as a single argument to prefer
// first overload if possible. If help message is evaluatable on constexpr
// context We'll be able to make FixedCharArray out of it and we'll choose first
// overload. In this case the help message expression is immediately evaluated
// and is used to construct the absl::Flag. No additional code is generated by
// ABSL_FLAG Otherwise SFINAE kicks in and first overload is dropped from the
// consideration, in which case the second overload will be used. The second
// overload does not attempt to evaluate the help message expression
// immediately and instead delays the evaluation by returning the function
// pointer (&T::NonConst) generating the help message when necessary. This is
// evaluatable in constexpr context, but the cost is an extra function being
// generated in the ABSL_FLAG code.
template <typename Gen, size_t N>
constexpr FlagHelpArg HelpArg(const FixedCharArray<N>& value) {
  return {FlagHelpMsg(value.value), FlagHelpKind::kLiteral};
}

template <typename Gen>
constexpr FlagHelpArg HelpArg(std::false_type) {
  return {FlagHelpMsg(&Gen::NonConst), FlagHelpKind::kGenFunc};
}

///////////////////////////////////////////////////////////////////////////////
// Flag default value auxiliary structs.

// Signature for the function generating the initial flag value (usually
// based on default value supplied in flag's definition)
using FlagDfltGenFunc = void (*)(void*);

union FlagDefaultSrc {
  constexpr explicit FlagDefaultSrc(FlagDfltGenFunc gen_func_arg)
      : gen_func(gen_func_arg) {}

#define ABSL_FLAGS_INTERNAL_DFLT_FOR_TYPE(T, name) \
  T name##_value;                                  \
  constexpr explicit FlagDefaultSrc(T value) : name##_value(value) {}  // NOLINT
  ABSL_FLAGS_INTERNAL_BUILTIN_TYPES(ABSL_FLAGS_INTERNAL_DFLT_FOR_TYPE)
#undef ABSL_FLAGS_INTERNAL_DFLT_FOR_TYPE

  void* dynamic_value;
  FlagDfltGenFunc gen_func;
};

enum class FlagDefaultKind : uint8_t {
  kDynamicValue = 0,
  kGenFunc = 1,
  kOneWord = 2,             // inline default value in descriptor
  kDefaultConstructed = 3,  // all-zero bytes or default-constructed empty value
};

struct FlagDefaultArg {
  FlagDefaultSrc source;
  FlagDefaultKind kind;
};

// This struct and corresponding overload to InitDefaultValue are used to
// facilitate usage of {} as default value in ABSL_FLAG macro.
// TODO(rogeeff): Fix handling types with explicit constructors.
struct EmptyBraces {};

template <typename T>
constexpr T InitDefaultValue(T t) {
  return t;
}

template <typename T>
constexpr T InitDefaultValue(EmptyBraces) {
  return T{};
}

template <typename T>
struct IsStdOptional : std::false_type {};
template <typename U>
struct IsStdOptional<std::optional<U>> : std::true_type {};

struct FlagStringDefault {
  bool is_literal = false;
  absl::string_view sv{};

  constexpr FlagStringDefault() : is_literal(true), sv() {}
  constexpr FlagStringDefault(const char* s)  // NOLINT
      : is_literal(s != nullptr),
        sv(s == nullptr ? absl::string_view() : absl::string_view(s)) {}
  template <typename Arg,
            std::enable_if_t<
                !std::is_convertible_v<const Arg&, const char*> &&
                    !std::is_same_v<std::decay_t<Arg>, FlagStringDefault>,
                int> = 0>
  constexpr FlagStringDefault(const Arg& arg)  // NOLINT
      : is_literal(std::is_convertible_v<const Arg&, absl::string_view>),
        sv([&]() constexpr {
          if constexpr (std::is_convertible_v<const Arg&, absl::string_view>) {
            return absl::string_view(arg);
          } else {
            return absl::string_view();
          }
        }()) {}
};

template <typename T,
          std::enable_if_t<!std::is_same_v<T, std::string>, int> = 0>
constexpr bool IsEmptyDefaultArg(EmptyBraces) {
  return std::is_same_v<T, std::vector<std::string>> || IsStdOptional<T>::value;
}
template <typename T, std::enable_if_t<!std::is_same_v<T, std::string> &&
                                           !IsStdOptional<T>::value,
                                       int> = 0>
constexpr bool IsEmptyDefaultArg(const T&) {
  return false;
}
template <typename T, std::enable_if_t<IsStdOptional<T>::value, int> = 0>
constexpr bool IsEmptyDefaultArg(std::nullopt_t) {
  return true;
}
template <
    typename T, typename Arg,
    std::enable_if_t<IsStdOptional<T>::value &&
                         !std::is_same_v<std::decay_t<Arg>, std::nullopt_t>,
                     int> = 0>
constexpr bool IsEmptyDefaultArg(const Arg&) {
  return false;
}
template <typename T, std::enable_if_t<std::is_same_v<T, std::string>, int> = 0>
constexpr bool IsEmptyDefaultArg(FlagStringDefault d) {
  return d.is_literal && d.sv.empty();
}

template <typename T,
          std::enable_if_t<!std::is_same_v<T, std::string>, int> = 0>
constexpr absl::string_view AsConstexprStringView(EmptyBraces) {
  return absl::string_view();
}
template <typename T,
          std::enable_if_t<!std::is_same_v<T, std::string>, int> = 0>
constexpr absl::string_view AsConstexprStringView(const T&) {
  return absl::string_view();
}
template <typename T, std::enable_if_t<std::is_same_v<T, std::string>, int> = 0>
constexpr absl::string_view AsConstexprStringView(FlagStringDefault d) {
  return d.sv;
}

constexpr bool HasNoEmbeddedNul(absl::string_view s) {
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\0') return false;
  }
  return true;
}

template <size_t N>
struct FlagBytes {
  uint8_t b[N];
  constexpr bool IsAllZero() const {
    for (size_t i = 0; i < N; ++i) {
      if (b[i] != 0) return false;
    }
    return true;
  }
};

template <typename T>
struct IsBitCastableDefaultType
    : std::bool_constant<
          std::is_arithmetic_v<T> || std::is_enum_v<T> ||
          std::is_same_v<T, absl::int128> || std::is_same_v<T, absl::uint128> ||
          std::is_same_v<T, absl::Duration> || std::is_same_v<T, absl::Time>> {
};

template <typename ValueT, typename GenT,
          std::enable_if_t<std::is_arithmetic_v<ValueT>, int> = ((void)GenT{},
                                                                 0)>
constexpr FlagDefaultArg DefaultArg(int) {
  return {FlagDefaultSrc(GenT{}.value), FlagDefaultKind::kOneWord};
}

template <typename ValueT, typename GenT>
constexpr FlagDefaultArg DefaultArg(char) {
  return {FlagDefaultSrc(&GenT::Gen), FlagDefaultKind::kGenFunc};
}

///////////////////////////////////////////////////////////////////////////////
// Flag storage selector traits. Each trait indicates what kind of storage kind
// to use for the flag value.

template <typename T>
using FlagUseValueAndInitBitStorage =
    std::bool_constant<std::is_trivially_copyable_v<T> &&
                       std::is_default_constructible_v<T> && (sizeof(T) < 8)>;

template <typename T>
using FlagUseOneWordStorage =
    std::bool_constant<std::is_trivially_copyable_v<T> && (sizeof(T) <= 8)>;

template <class T>
using FlagUseSequenceLockStorage =
    std::bool_constant<std::is_trivially_copyable_v<T> && (sizeof(T) > 8)>;

enum class FlagValueStorageKind : uint8_t {
  kValueAndInitBit = 0,
  kOneWordAtomic = 1,
  kSequenceLocked = 2,
  kHeapAllocated = 3,
  kInlineBool = 4,
};

// This constexpr function returns the storage kind for the given flag value
// type.
template <typename T>
static constexpr FlagValueStorageKind StorageKind() {
  return std::is_same_v<T, bool> ? FlagValueStorageKind::kInlineBool
         : FlagUseValueAndInitBitStorage<T>::value
             ? FlagValueStorageKind::kValueAndInitBit
         : FlagUseOneWordStorage<T>::value
             ? FlagValueStorageKind::kOneWordAtomic
         : FlagUseSequenceLockStorage<T>::value
             ? FlagValueStorageKind::kSequenceLocked
             : FlagValueStorageKind::kHeapAllocated;
}

// This is a base class for the storage classes used by kOneWordAtomic and
// kValueAndInitBit storage kinds. It literally just stores the one word value
// as an atomic. By default, it is initialized to a magic value that is unlikely
// a valid value for the flag value type.
struct FlagOneWordValue {
  constexpr static int64_t Uninitialized() {
    return static_cast<int64_t>(0xababababababababll);
  }

  constexpr FlagOneWordValue() : value(Uninitialized()) {}
  constexpr explicit FlagOneWordValue(int64_t v) : value(v) {}
  std::atomic<int64_t> value;
};

// This class represents a memory layout used by kValueAndInitBit storage kind.
template <typename T>
struct alignas(8) FlagValueAndInitBit {
  T value;
  // Use an int instead of a bool to guarantee that a non-zero value has
  // a bit set.
  uint8_t init;
};

// This class implements an aligned pointer with two options stored via masks
// in unused bits of the pointer value (due to alignment requirement).
//  - IsUnprotectedReadCandidate - indicates that the value can be switched to
//    unprotected read without a lock.
//  - HasBeenRead - indicates that the value has been read at least once.
//  - AllowsUnprotectedRead - combination of the two options above and indicates
//    that the value can now be read without a lock.
// Further details of these options and their use is covered in the description
// of the FlagValue<T, FlagValueStorageKind::kHeapAllocated> specialization.
class MaskedPointer {
 public:
  using mask_t = uintptr_t;
  using ptr_t = void*;

  static constexpr int RequiredAlignment() { return 4; }

  constexpr MaskedPointer() : ptr_(nullptr) {}
  constexpr explicit MaskedPointer(ptr_t rhs) : ptr_(rhs) {}
  MaskedPointer(ptr_t rhs, bool is_candidate);

  MaskedPointer(const MaskedPointer& rhs) = default;
  MaskedPointer& operator=(const MaskedPointer& rhs) = default;

  void* Ptr() const {
    return reinterpret_cast<void*>(reinterpret_cast<mask_t>(ptr_) &
                                   kPtrValueMask);
  }
  bool AllowsUnprotectedRead() const {
    return (reinterpret_cast<mask_t>(ptr_) & kAllowsUnprotectedRead) ==
           kAllowsUnprotectedRead;
  }
  bool IsUnprotectedReadCandidate() const;
  bool HasBeenRead() const;

  void Set(FlagOpFn op, const void* src, bool is_candidate);
  void MarkAsRead();

 private:
  // Masks
  // Indicates that the flag value either default or originated from command
  // line.
  static constexpr mask_t kUnprotectedReadCandidate = 0x1u;
  // Indicates that flag has been read.
  static constexpr mask_t kHasBeenRead = 0x2u;
  static constexpr mask_t kAllowsUnprotectedRead =
      kUnprotectedReadCandidate | kHasBeenRead;
  static constexpr mask_t kPtrValueMask = ~kAllowsUnprotectedRead;

  void ApplyMask(mask_t mask);
  bool CheckMask(mask_t mask) const;

  ptr_t ptr_;
};

// This class implements a type erased storage of the heap allocated flag value.
// It is used as a base class for the storage class for kHeapAllocated storage
// kind. The initial_buffer is expected to have an alignment of at least
// MaskedPointer::RequiredAlignment(), so that the bits used by the
// MaskedPointer to store masks are set to 0. This guarantees that value starts
// in an uninitialized state.
struct FlagMaskedPointerValue {
  constexpr explicit FlagMaskedPointerValue(MaskedPointer::ptr_t initial_buffer)
      : value(MaskedPointer(initial_buffer)) {}

  std::atomic<MaskedPointer> value;
};

// This is the forward declaration for the template that represents a storage
// for the flag values. This template is expected to be explicitly specialized
// for each storage kind and it does not have a generic default
// implementation.
template <typename T,
          FlagValueStorageKind Kind = flags_internal::StorageKind<T>()>
struct FlagValue;

// This specialization represents the storage of flag values types with the
// kValueAndInitBit storage kind. It is based on the FlagOneWordValue class
// and relies on memory layout in FlagValueAndInitBit<T> to indicate that the
// value has been initialized or not.
template <typename T>
struct FlagValue<T, FlagValueStorageKind::kValueAndInitBit> : FlagOneWordValue {
  constexpr FlagValue() : FlagOneWordValue(0) {}
  bool Get(T& dst) const {
    int64_t storage = value.load(std::memory_order_acquire);
    if (ABSL_PREDICT_FALSE(storage == 0)) {
      // This assert is to ensure that the initialization inside FlagImpl::Init
      // is able to set init member correctly.
      static_assert(offsetof(FlagValueAndInitBit<T>, init) == sizeof(T),
                    "Unexpected memory layout of FlagValueAndInitBit");
      return false;
    }
    dst = absl::bit_cast<FlagValueAndInitBit<T>>(storage).value;
    return true;
  }
};

// This specialization represents the storage of flag values types with the
// kOneWordAtomic storage kind. It is based on the FlagOneWordValue class
// and relies on the magic uninitialized state of default constructed instead of
// FlagOneWordValue to indicate that the value has been initialized or not.
template <typename T>
struct FlagValue<T, FlagValueStorageKind::kOneWordAtomic> : FlagOneWordValue {
  constexpr FlagValue() : FlagOneWordValue() {}
  bool Get(T& dst) const {
    int64_t one_word_val = value.load(std::memory_order_acquire);
    if (ABSL_PREDICT_FALSE(one_word_val == FlagOneWordValue::Uninitialized())) {
      return false;
    }
    std::memcpy(&dst, static_cast<const void*>(&one_word_val), sizeof(T));
    return true;
  }
};

// This specialization represents the storage of flag values types with the
// kSequenceLocked storage kind. This storage is used by trivially copyable
// types with size greater than 8 bytes. This storage relies on uninitialized
// state of the SequenceLock to indicate that the value has been initialized or
// not. This storage also provides lock-free read access to the underlying
// value once it is initialized.
template <typename T>
struct FlagValue<T, FlagValueStorageKind::kSequenceLocked> {
  bool Get(T& dst) const {
    static_assert(
        offsetof(FlagValue, seq_lock) - offsetof(FlagValue, value_words) ==
            kNumWords * sizeof(std::atomic<uint64_t>),
        "Unexpected memory layout of sequence-locked FlagValue");
    return seq_lock.TryRead(&dst, value_words, sizeof(T));
  }

  static constexpr int kNumWords =
      flags_internal::AlignUp(sizeof(T), sizeof(uint64_t)) / sizeof(uint64_t);

  alignas(T) alignas(
      std::atomic<uint64_t>) std::atomic<uint64_t> value_words[kNumWords];
  flags_internal::SequenceLock seq_lock;
};

// This specialization represents the storage of flag values types with the
// kHeapAllocated storage kind. This is a storage of last resort and is used
// if none of other storage kinds are applicable.
//
// Generally speaking the values with this storage kind can't be accessed
// atomically and thus can't be read without holding a lock. If we would ever
// want to avoid the lock, we'd need to leak the old value every time new flag
// value is being set (since we are in danger of having a race condition
// otherwise).
//
// Instead of doing that, this implementation attempts to cater to some common
// use cases by allowing at most 2 values to be leaked - default value and
// value set from the command line.
//
// This specialization provides an initial buffer for the first flag value. This
// is where the default value is going to be stored. We attempt to reuse this
// buffer if possible, including storing the value set from the command line
// there.
//
// As long as we only read this value, we can access it without a lock (in
// practice we still use the lock for the very first read to be able set
// "has been read" option on this flag).
//
// If flag is specified on the command line we store the parsed value either
// in the internal buffer (if the default value never been read) or we leak the
// default value and allocate the new storage for the parse value. This value is
// also a candidate for an unprotected read. If flag is set programmatically
// after the command line is parsed, the storage for this value is going to be
// leaked. Note that in both scenarios we are not going to have a real leak.
// Instead we'll store the leaked value pointers in the internal freelist to
// avoid triggering the memory leak checker complains.
//
// If the flag is ever set programmatically, it stops being the candidate for an
// unprotected read, and any follow up access to the flag value requires a lock.
// Note that if the value if set programmatically before the command line is
// parsed, we can switch back to enabling unprotected reads for that value.
template <typename T>
struct FlagValue<T, FlagValueStorageKind::kHeapAllocated>
    : FlagMaskedPointerValue {
  // We const initialize the value with unmasked pointer to the internal buffer,
  // making sure it is not a candidate for unprotected read. This way we can
  // ensure Init is done before any access to the flag value.
  constexpr FlagValue() : FlagMaskedPointerValue(&buffer[0]) {}

  bool Get(T& dst) const {
    MaskedPointer ptr_value = value.load(std::memory_order_acquire);

    if (ABSL_PREDICT_TRUE(ptr_value.AllowsUnprotectedRead())) {
      ::new (static_cast<void*>(&dst)) T(*static_cast<T*>(ptr_value.Ptr()));
      return true;
    }
    return false;
  }

  alignas(MaskedPointer::RequiredAlignment()) alignas(
      T) char buffer[sizeof(T)]{};
};

// Specialization for bool flags whose value is stored inline in FlagImpl.
template <typename T>
struct FlagValue<T, FlagValueStorageKind::kInlineBool> {};

///////////////////////////////////////////////////////////////////////////////
// Flag callback auxiliary structs.

// Signature for the mutation callback used by watched Flags
// The callback is noexcept.
// TODO(rogeeff): add noexcept after C++17 support is added.
using FlagCallbackFunc = void (*)();

struct FlagCallback {
  FlagCallbackFunc func;
  absl::Mutex guard;  // Guard for concurrent callback invocations.
};

///////////////////////////////////////////////////////////////////////////////
// Flag implementation, which does not depend on flag value type.
// The class encapsulates the Flag's data and access to it.

struct DynValueDeleter {
  explicit DynValueDeleter(FlagOpFn op_arg = nullptr);
  void operator()(void* ptr) const;

  FlagOpFn op;
};

static constexpr uint8_t kNoBuiltinOp = 0xFF;
static constexpr uint8_t kBuiltinOpStdString = 11;

template <typename T>
constexpr uint8_t BuiltinFlagOpId() {
  if constexpr (std::is_same_v<T, bool>) return 0;
  if constexpr (std::is_same_v<T, short>) return 1;               // NOLINT
  if constexpr (std::is_same_v<T, unsigned short>) return 2;      // NOLINT
  if constexpr (std::is_same_v<T, int>) return 3;                 // NOLINT
  if constexpr (std::is_same_v<T, unsigned int>) return 4;        // NOLINT
  if constexpr (std::is_same_v<T, long>) return 5;                // NOLINT
  if constexpr (std::is_same_v<T, unsigned long>) return 6;       // NOLINT
  if constexpr (std::is_same_v<T, long long>) return 7;           // NOLINT
  if constexpr (std::is_same_v<T, unsigned long long>) return 8;  // NOLINT
  if constexpr (std::is_same_v<T, float>) return 9;
  if constexpr (std::is_same_v<T, double>) return 10;
  if constexpr (std::is_same_v<T, std::string>) return 11;
  if constexpr (std::is_same_v<T, std::vector<std::string>>) return 12;
  if constexpr (std::is_same_v<T, absl::int128>) return 13;
  if constexpr (std::is_same_v<T, absl::uint128>) return 14;
  if constexpr (std::is_same_v<T, absl::LogSeverity>) return 15;
  if constexpr (std::is_same_v<T, std::optional<bool>>) return 16;
  if constexpr (std::is_same_v<T, std::optional<int>>) return 17;
  if constexpr (std::is_same_v<T, std::optional<long>>) return 18;  // NOLINT
  if constexpr (std::is_same_v<T, std::optional<long long>>)        // NOLINT
    return 19;
  if constexpr (std::is_same_v<T, std::optional<unsigned long>>)  // NOLINT
    return 20;
  if constexpr (std::is_same_v<T, std::optional<double>>) return 21;
  if constexpr (std::is_same_v<T, std::optional<std::string>>) return 22;
  if constexpr (std::is_same_v<T, absl::Duration>) return 23;
  if constexpr (std::is_same_v<T, absl::Time>) return 24;
  if constexpr (std::is_same_v<T, std::optional<absl::Duration>>) return 25;
  return kNoBuiltinOp;
}

enum class FlagMetaKind : uint8_t {
  kPtrMeta = 0,
  kZeroFlatMeta = 1,
  kOneWordFlatMeta = 2,
  kTwoWordFlatMeta = 3,
};

struct FlagMetaHeader {
  constexpr static uint8_t PackKinds(FlagValueStorageKind vk, FlagHelpKind hk,
                                     FlagDefaultKind dk, FlagMetaKind mk) {
    return static_cast<uint8_t>((static_cast<uint8_t>(vk) & 0x7u) |
                                ((static_cast<uint8_t>(hk) & 0x1u) << 3) |
                                ((static_cast<uint8_t>(dk) & 0x3u) << 4) |
                                ((static_cast<uint8_t>(mk) & 0x3u) << 6));
  }

  uint8_t kinds;
  uint8_t builtin_op_id;
  uint8_t name_len;
};

constexpr size_t ConstexprStrLen(const char* s) {
  size_t len = 0;
  if (s != nullptr) {
    while (s[len] != '\0') ++len;
  }
  return len;
}

template <size_t N>
constexpr const char* ColdHelpPtr(const FixedCharArray<N>& arr) {
  return arr.value;
}
constexpr const char* ColdHelpPtr(std::false_type) { return nullptr; }

// Pointer-based descriptor for built-in types (32 B, omits FlagOpFn pointer).
struct BuiltinPtrFlagMeta {
  constexpr BuiltinPtrFlagMeta(FlagMetaHeader hdr_arg, const char* name_arg,
                               FlagHelpMsg help_arg,
                               FlagDefaultSrc default_value_arg)
      : hdr(hdr_arg),
        name(name_arg),
        help(help_arg),
        default_value(default_value_arg) {}

  FlagMetaHeader hdr;
  const char* const name;
  const FlagHelpMsg help;
  const FlagDefaultSrc default_value;
};

// Full pointer-based descriptor for custom/enum types (40 B).
struct FlagMeta {
  constexpr FlagMeta(FlagMetaHeader hdr_arg, const char* name_arg,
                     FlagHelpMsg help_arg, FlagDefaultSrc default_value_arg,
                     FlagOpFn op_arg)
      : hdr(hdr_arg),
        name(name_arg),
        help(help_arg),
        default_value(default_value_arg),
        op(op_arg) {}

  FlagMetaHeader hdr;
  // Flag's name passed to ABSL_FLAG as second arg.
  const char* const name;
  // Help message literal or function to generate it.
  const FlagHelpMsg help;
  // Initial default value source specified in ABSL_FLAG.
  const FlagDefaultSrc default_value;
  // Type-specific operations vtable.
  const FlagOpFn op;
};

template <typename T>
constexpr FlagMeta MakeFlagMeta(const char* name, FlagHelpArg help,
                                FlagDefaultArg default_arg) {
  return FlagMeta(
      FlagMetaHeader{
          FlagMetaHeader::PackKinds(flags_internal::StorageKind<T>(), help.kind,
                                    default_arg.kind, FlagMetaKind::kPtrMeta),
          BuiltinFlagOpId<T>(),
          static_cast<uint8_t>(
              ConstexprStrLen(name) <= 254 ? ConstexprStrLen(name) : 254)},
      name, help.source, default_arg.source, &FlagOps<T>);
}

template <typename T>
constexpr BuiltinPtrFlagMeta MakeBuiltinPtrFlagMeta(
    const char* name, FlagHelpArg help, FlagDefaultArg default_arg) {
  return BuiltinPtrFlagMeta(
      FlagMetaHeader{
          FlagMetaHeader::PackKinds(flags_internal::StorageKind<T>(), help.kind,
                                    default_arg.kind, FlagMetaKind::kPtrMeta),
          BuiltinFlagOpId<T>(),
          static_cast<uint8_t>(
              ConstexprStrLen(name) <= 254 ? ConstexprStrLen(name) : 254)},
      name, help.source, default_arg.source);
}

// Pointer-free .rodata descriptors for common cases (alignof == 1, 0 pointers,
// 0 relocations in PIE; help stays in flags_help_cold via FlagRegistrar).
template <size_t NameLen>
struct ZeroFlatFlagMeta {
  FlagMetaHeader hdr;
  char name[NameLen];
};

template <size_t NameLen>
struct OneWordFlatFlagMeta {
  FlagMetaHeader hdr;
  uint8_t default_bytes[8];
  char name[NameLen];
};

template <size_t NameLen>
struct TwoWordFlatFlagMeta {
  FlagMetaHeader hdr;
  uint8_t default_bytes[16];
  char name[NameLen];
};

template <typename T, size_t NameLen, size_t... NameI>
constexpr ZeroFlatFlagMeta<NameLen> MakeZeroFlatFlagMeta(
    const char* name, FlagDefaultKind def_kind, std::index_sequence<NameI...>) {
  return ZeroFlatFlagMeta<NameLen>{
      FlagMetaHeader{FlagMetaHeader::PackKinds(flags_internal::StorageKind<T>(),
                                               FlagHelpKind::kLiteral, def_kind,
                                               FlagMetaKind::kZeroFlatMeta),
                     BuiltinFlagOpId<T>(), static_cast<uint8_t>(NameLen - 1)},
      {name[NameI]..., '\0'}};
}

template <typename T, size_t NameLen, size_t... NameI>
constexpr OneWordFlatFlagMeta<NameLen> MakeOneWordFlatFlagMeta(
    const char* name, const FlagBytes<sizeof(T)>& def_bytes,
    std::index_sequence<NameI...>) {
  OneWordFlatFlagMeta<NameLen> res{
      FlagMetaHeader{FlagMetaHeader::PackKinds(flags_internal::StorageKind<T>(),
                                               FlagHelpKind::kLiteral,
                                               FlagDefaultKind::kOneWord,
                                               FlagMetaKind::kOneWordFlatMeta),
                     BuiltinFlagOpId<T>(), static_cast<uint8_t>(NameLen - 1)},
      {},
      {name[NameI]..., '\0'}};
  for (size_t i = 0; i < sizeof(T); ++i) {
    res.default_bytes[i] = def_bytes.b[i];
  }
  return res;
}

template <typename T, size_t NameLen, size_t... NameI>
constexpr TwoWordFlatFlagMeta<NameLen> MakeTwoWordFlatFlagMeta(
    const char* name, const FlagBytes<sizeof(T)>& def_bytes,
    std::index_sequence<NameI...>) {
  TwoWordFlatFlagMeta<NameLen> res{
      FlagMetaHeader{FlagMetaHeader::PackKinds(flags_internal::StorageKind<T>(),
                                               FlagHelpKind::kLiteral,
                                               FlagDefaultKind::kOneWord,
                                               FlagMetaKind::kTwoWordFlatMeta),
                     BuiltinFlagOpId<T>(), static_cast<uint8_t>(NameLen - 1)},
      {},
      {name[NameI]..., '\0'}};
  for (size_t i = 0; i < sizeof(T); ++i) {
    res.default_bytes[i] = def_bytes.b[i];
  }
  return res;
}

template <typename ValueT, typename GenT>
constexpr bool IsZeroBytesValue() {
  if constexpr (IsBitCastableDefaultType<ValueT>::value) {
    return __builtin_bit_cast(FlagBytes<sizeof(ValueT)>, GenT{}.value)
        .IsAllZero();
  } else {
    return false;
  }
}

template <typename ValueT>
constexpr bool IsZeroBytesDefaultConstructed() {
  if constexpr (IsBitCastableDefaultType<ValueT>::value) {
    return __builtin_bit_cast(FlagBytes<sizeof(ValueT)>, ValueT{}).IsAllZero();
  } else {
    return false;
  }
}

template <typename ValueT, typename GenT, typename HelpGenT, size_t NameLen,
          size_t HelpLen,
          std::enable_if_t<IsBitCastableDefaultType<ValueT>::value &&
                               (BuiltinFlagOpId<ValueT>() != kNoBuiltinOp) &&
                               (NameLen <= 255),
                           int> = ((void)GenT{}, 0),
          bool IsZero = IsZeroBytesValue<ValueT, GenT>(),
          std::enable_if_t<IsZero ? IsZeroBytesDefaultConstructed<ValueT>()
                                  : std::is_same_v<ValueT, bool>,
                           int> = 0>
constexpr ZeroFlatFlagMeta<NameLen> SelectFlagMeta(
    const char* name, const FixedCharArray<HelpLen>&, int) {
  return MakeZeroFlatFlagMeta<ValueT, NameLen>(
      name,
      IsZero ? FlagDefaultKind::kDefaultConstructed : FlagDefaultKind::kOneWord,
      std::make_index_sequence<NameLen - 1>{});
}

template <typename ValueT, typename GenT, typename HelpGenT, size_t NameLen,
          size_t HelpLen,
          std::enable_if_t<(BuiltinFlagOpId<ValueT>() != kNoBuiltinOp) &&
                               (NameLen <= 255) &&
                               !IsBitCastableDefaultType<ValueT>::value,
                           int> = 0,
          bool IsEmpty = GenT::IsEmpty(), std::enable_if_t<IsEmpty, int> = 0>
constexpr ZeroFlatFlagMeta<NameLen> SelectFlagMeta(
    const char* name, const FixedCharArray<HelpLen>&, int) {
  return MakeZeroFlatFlagMeta<ValueT, NameLen>(
      name, FlagDefaultKind::kDefaultConstructed,
      std::make_index_sequence<NameLen - 1>{});
}

template <
    typename ValueT, typename GenT, typename HelpGenT, size_t NameLen,
    size_t HelpLen,
    std::enable_if_t<IsBitCastableDefaultType<ValueT>::value &&
                         (BuiltinFlagOpId<ValueT>() != kNoBuiltinOp) &&
                         (NameLen <= 255) && !std::is_same_v<ValueT, bool> &&
                         (sizeof(ValueT) <= 8),
                     int> = ((void)GenT{}, 0),
    bool IsZero = IsZeroBytesValue<ValueT, GenT>(),
    std::enable_if_t<!IsZero, int> = 0>
constexpr OneWordFlatFlagMeta<NameLen> SelectFlagMeta(
    const char* name, const FixedCharArray<HelpLen>&, int) {
  return MakeOneWordFlatFlagMeta<ValueT, NameLen>(
      name, __builtin_bit_cast(FlagBytes<sizeof(ValueT)>, GenT{}.value),
      std::make_index_sequence<NameLen - 1>{});
}

template <typename ValueT, typename GenT, typename HelpGenT, size_t NameLen,
          size_t HelpLen,
          std::enable_if_t<IsBitCastableDefaultType<ValueT>::value &&
                               (BuiltinFlagOpId<ValueT>() != kNoBuiltinOp) &&
                               (NameLen <= 255) &&
                               (sizeof(ValueT) > 8 && sizeof(ValueT) <= 16),
                           int> = ((void)GenT{}, 0),
          bool IsZero = IsZeroBytesValue<ValueT, GenT>(),
          std::enable_if_t<!IsZero, int> = 0>
constexpr TwoWordFlatFlagMeta<NameLen> SelectFlagMeta(
    const char* name, const FixedCharArray<HelpLen>&, int) {
  return MakeTwoWordFlatFlagMeta<ValueT, NameLen>(
      name, __builtin_bit_cast(FlagBytes<sizeof(ValueT)>, GenT{}.value),
      std::make_index_sequence<NameLen - 1>{});
}

template <size_t NameLen, size_t DefLen>
struct StringFlatFlagMeta {
  FlagMetaHeader hdr;
  char name[NameLen];
  char def_str[DefLen];
};

template <typename T, size_t NameLen, size_t DefLen, size_t... NameI,
          size_t... DefI>
constexpr StringFlatFlagMeta<NameLen, DefLen> MakeStringFlatFlagMeta(
    const char* name, absl::string_view def_str, std::index_sequence<NameI...>,
    std::index_sequence<DefI...>) {
  return StringFlatFlagMeta<NameLen, DefLen>{
      FlagMetaHeader{FlagMetaHeader::PackKinds(flags_internal::StorageKind<T>(),
                                               FlagHelpKind::kLiteral,
                                               FlagDefaultKind::kOneWord,
                                               FlagMetaKind::kOneWordFlatMeta),
                     BuiltinFlagOpId<T>(), static_cast<uint8_t>(NameLen - 1)},
      {name[NameI]..., '\0'},
      {def_str[DefI]..., '\0'}};
}

template <typename ValueT, typename GenT, typename HelpGenT, size_t NameLen,
          size_t HelpLen,
          std::enable_if_t<(BuiltinFlagOpId<ValueT>() != kNoBuiltinOp) &&
                               (NameLen <= 255) &&
                               std::is_same_v<ValueT, std::string>,
                           int> = 0,
          bool IsValidStr = (!GenT::IsEmpty() &&
                             HasNoEmbeddedNul(GenT::StrView())),
          std::enable_if_t<IsValidStr, int> = 0,
          size_t DefLen = GenT::StrView().size() + 1>
constexpr StringFlatFlagMeta<NameLen, DefLen> SelectFlagMeta(
    const char* name, const FixedCharArray<HelpLen>&, int) {
  return MakeStringFlatFlagMeta<ValueT, NameLen, DefLen>(
      name, GenT::StrView(), std::make_index_sequence<NameLen - 1>{},
      std::make_index_sequence<DefLen - 1>{});
}

template <typename ValueT, typename GenT, typename HelpGenT, size_t NameLen,
          typename HelpStorageT,
          std::enable_if_t<BuiltinFlagOpId<ValueT>() != kNoBuiltinOp, int> = 0>
constexpr BuiltinPtrFlagMeta SelectFlagMeta(const char* name,
                                            const HelpStorageT& help_storage,
                                            char) {
  return MakeBuiltinPtrFlagMeta<ValueT>(name, HelpArg<HelpGenT>(help_storage),
                                        DefaultArg<ValueT, GenT>(0));
}

template <typename ValueT, typename GenT, typename HelpGenT, size_t NameLen,
          typename HelpStorageT,
          std::enable_if_t<BuiltinFlagOpId<ValueT>() == kNoBuiltinOp, int> = 0>
constexpr FlagMeta SelectFlagMeta(const char* name,
                                  const HelpStorageT& help_storage, char) {
  return MakeFlagMeta<ValueT>(name, HelpArg<HelpGenT>(help_storage),
                              DefaultArg<ValueT, GenT>(0));
}

class FlagState;

// These are only used as constexpr global objects.
// They do not use a virtual destructor to simplify their implementation.
// They are not destroyed except at program exit, so leaks do not matter.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
class FlagImpl final : public CommandLineFlag {
 public:
  constexpr explicit FlagImpl(const FlagMetaHeader* meta)
      : meta_(meta),
        init_control_{},
        file_id_(0),
        def_kind_(static_cast<uint8_t>((meta->kinds >> 4) & 0x3u)),
        modified_(false),
        on_command_line_(false),
        has_callback_(false),
        mod_count_(0),
        inline_bool_value_(0) {}

  constexpr FlagImpl(const FlagMeta* meta, FlagHelpKind /*help_kind*/,
                     FlagValueStorageKind /*value_kind*/,
                     FlagDefaultKind default_kind)
      : meta_(&meta->hdr),
        init_control_{},
        file_id_(0),
        def_kind_(static_cast<uint8_t>(default_kind)),
        modified_(false),
        on_command_line_(false),
        has_callback_(false),
        mod_count_(0),
        inline_bool_value_(0) {}

  bool TryGetInlineBool(bool& dst) const {
    uint8_t s = inline_bool_value_.load(std::memory_order_acquire);
    if (ABSL_PREDICT_FALSE(s == 0)) return false;
    dst = static_cast<bool>(s & 1u);
    return true;
  }

  void SetFilename(const char* filename);
  void SetColdHelp(const char* cold_help);

  // Constant access methods
  int64_t ReadOneWord() const ABSL_LOCKS_EXCLUDED(*DataGuard());
  bool ReadOneBool() const ABSL_LOCKS_EXCLUDED(*DataGuard());
  void Read(void* dst) const override ABSL_LOCKS_EXCLUDED(*DataGuard());
  void Read(bool* value) const ABSL_LOCKS_EXCLUDED(*DataGuard()) {
    *value = ReadOneBool();
  }
  template <typename T,
            std::enable_if_t<flags_internal::StorageKind<T>() ==
                                 FlagValueStorageKind::kOneWordAtomic,
                             int> = 0>
  void Read(T* value) const ABSL_LOCKS_EXCLUDED(*DataGuard()) {
    int64_t v = ReadOneWord();
    std::memcpy(value, static_cast<const void*>(&v), sizeof(T));
  }
  template <typename T,
            std::enable_if_t<flags_internal::StorageKind<T>() ==
                                 FlagValueStorageKind::kValueAndInitBit,
                             int> = 0>
  void Read(T* value) const ABSL_LOCKS_EXCLUDED(*DataGuard()) {
    *value = absl::bit_cast<FlagValueAndInitBit<T>>(ReadOneWord()).value;
  }

  // Mutating access methods
  void Write(const void* src) ABSL_LOCKS_EXCLUDED(*DataGuard());

  // Interfaces to operate on callbacks.
  void SetCallback(const FlagCallbackFunc mutation_callback)
      ABSL_LOCKS_EXCLUDED(*DataGuard());
  void InvokeCallback() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());

  // Used in read/write operations to validate source/target has correct type.
  // For example if flag is declared as absl::Flag<int> FLAGS_foo, a call to
  // absl::GetFlag(FLAGS_foo) validates that the type of FLAGS_foo is indeed
  // int. To do that we pass the assumed type id (which is deduced from type
  // int) as an argument `type_id`, which is in turn is validated against the
  // type id stored in flag object by flag definition statement.
  void AssertValidType(FlagFastTypeId type_id,
                       const std::type_info* (*gen_rtti)()) const;

 private:
  template <typename T>
  friend class Flag;
  friend class FlagState;

  FlagOpFn op() const;
  const BuiltinPtrFlagMeta* PtrMeta() const {
    return reinterpret_cast<const BuiltinPtrFlagMeta*>(meta_);
  }
  const void* DefaultValueStoragePtr() const;
  const char* DefaultCStringPtr() const;

  // Ensures that `data_guard_` is initialized and returns it.
  absl::Mutex* DataGuard() const
      ABSL_LOCK_RETURNED(reinterpret_cast<absl::Mutex*>(data_guard_));
  // Returns heap allocated value of type T initialized with default value.
  std::unique_ptr<void, DynValueDeleter> MakeInitValue() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());
  // Flag initialization called via absl::call_once.
  void Init();

  // Offset value access methods. One per storage kind. These methods to not
  // respect const correctness, so be very careful using them.

  // This is a shared helper routine which encapsulates most of the magic. Since
  // it is only used inside the routines below, which are defined in
  // flag.cc, we can define it in that file as well.
  template <typename StorageT>
  StorageT* OffsetValue() const;

  // The same as above, but used for sequencelock-protected storage.
  std::atomic<uint64_t>* AtomicBufferValue() const;

  // Accessor for the SequenceLock inside FlagValue<T, kSequenceLocked>.
  flags_internal::SequenceLock& SeqLock() const;

  // This is an accessor for a value stored as one word atomic. Returns a
  // mutable reference to an atomic value.
  std::atomic<int64_t>& OneWordValue() const;

  std::atomic<MaskedPointer>& PtrStorage() const;

  // Attempts to parse supplied `value` string. If parsing is successful,
  // returns new value. Otherwise returns nullptr.
  std::unique_ptr<void, DynValueDeleter> TryParse(absl::string_view value,
                                                  std::string& err) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());
  // Stores the flag value based on the pointer to the source.
  void StoreValue(const void* src, ValueSource source)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());

  // Copy the flag data, protected by `SeqLock()` into `dst`.
  //
  // REQUIRES: ValueStorageKind() == kSequenceLocked.
  void ReadSequenceLockedData(void* dst) const
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  FlagHelpKind HelpSourceKind() const {
    return static_cast<FlagHelpKind>((meta_->kinds >> 3) & 0x1u);
  }
  FlagValueStorageKind ValueStorageKind() const {
    return static_cast<FlagValueStorageKind>(meta_->kinds & 0x7u);
  }
  FlagMetaKind MetaKind() const {
    return static_cast<FlagMetaKind>((meta_->kinds >> 6) & 0x3u);
  }
  FlagDefaultKind DefaultKind() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard()) {
    return static_cast<FlagDefaultKind>(def_kind_);
  }

  // CommandLineFlag interface implementation
  absl::string_view Name() const override;
  absl::string_view TypeName() const override;
  std::string Filename() const override;
  std::string Help() const override;
  FlagFastTypeId TypeId() const override;
  bool IsSpecifiedOnCommandLine() const override
      ABSL_LOCKS_EXCLUDED(*DataGuard());
  std::string DefaultValue() const override ABSL_LOCKS_EXCLUDED(*DataGuard());
  std::string CurrentValue() const override ABSL_LOCKS_EXCLUDED(*DataGuard());
  bool ValidateInputValue(absl::string_view value) const override
      ABSL_LOCKS_EXCLUDED(*DataGuard());
  void CheckDefaultValueParsingRoundtrip() const override
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  int64_t ModificationCount() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());
  bool IsUnmodifiedFast() const ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return mod_count_ == 0;
  }
  bool CanSkipInitOnSave() const;
  void IncrementModCount() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());

  // Interfaces to save and restore flags to/from persistent state.
  // Returns current flag state or nullptr if flag does not support
  // saving and restoring a state.
  std::unique_ptr<FlagStateInterface> SaveState() override
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  // Restores the flag state to the supplied state object. If there is
  // nothing to restore returns false. Otherwise returns true.
  bool RestoreState(const FlagState& flag_state)
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  bool ParseFrom(absl::string_view value, FlagSettingMode set_mode,
                 ValueSource source, std::string& error) override
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  // Pointer to immutable flag descriptor in .rodata / .data.rel.ro.
  const FlagMetaHeader* const meta_;

  // Per-flag mutex guard, placement-initialized in Init().
  alignas(sizeof(void*)) mutable char data_guard_[sizeof(absl::Mutex)]{};

  // Unique tag for absl::call_once call to initialize this flag.
  absl::once_flag init_control_;

  // Deduplicated filename index assigned on registration (0 = unset).
  uint16_t file_id_;

  // Mutable flag's state (guarded by `DataGuard()`).

  // def_kind_ is not guarded by DataGuard() since it is accessed in Init
  // without locks.
  uint8_t def_kind_ : 2;
  // Has this flag's value been modified?
  bool modified_ : 1 ABSL_GUARDED_BY(*DataGuard());
  // Has this flag been specified on command line.
  bool on_command_line_ : 1 ABSL_GUARDED_BY(*DataGuard());
  // Has a mutation callback been registered on this flag?
  bool has_callback_ : 1 ABSL_GUARDED_BY(*DataGuard());
  // Saturating 3-bit mutation counter (7 = overflow in side table).
  uint8_t mod_count_ : 3 ABSL_GUARDED_BY(*DataGuard());

  // Inline atomic storage for Flag<bool> (0 = uninit, 2 = false, 3 = true).
  mutable std::atomic<uint8_t> inline_bool_value_;
};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

///////////////////////////////////////////////////////////////////////////////
// The Flag object parameterized by the flag's value type. This class implements
// flag reflection handle interface.

template <typename T,
          FlagValueStorageKind Kind = flags_internal::StorageKind<T>()>
struct FlagStorage {
  constexpr explicit FlagStorage(const FlagMetaHeader* meta)
      : impl_(meta), value_() {}
  constexpr FlagStorage(const FlagMeta* meta, FlagHelpKind help_kind,
                        FlagDefaultKind default_kind)
      : impl_(meta, help_kind, Kind, default_kind), value_() {}

  // The implementation depends on value_ field to be placed exactly after the
  // impl_ field, so that impl_ can figure out the offset to the value and
  // access it.
  FlagImpl impl_;
  FlagValue<T, Kind> value_;
};

template <typename T>
struct FlagStorage<T, FlagValueStorageKind::kInlineBool> {
  constexpr explicit FlagStorage(const FlagMetaHeader* meta) : impl_(meta) {}
  constexpr FlagStorage(const FlagMeta* meta, FlagHelpKind help_kind,
                        FlagDefaultKind default_kind)
      : impl_(meta, help_kind, FlagValueStorageKind::kInlineBool,
              default_kind) {}

  FlagImpl impl_;
};

template <typename T>
class Flag : private FlagStorage<T> {
 public:
  constexpr explicit Flag(const FlagMetaHeader* meta) : FlagStorage<T>(meta) {}

  constexpr Flag(const FlagMeta* meta, FlagHelpKind help_kind,
                 FlagDefaultKind default_kind)
      : FlagStorage<T>(meta, help_kind, default_kind) {}

  // CommandLineFlag interface
  absl::string_view Name() const { return this->impl_.Name(); }
  std::string Filename() const { return this->impl_.Filename(); }
  std::string Help() const { return this->impl_.Help(); }
  // Do not use. To be removed.
  bool IsSpecifiedOnCommandLine() const {
    return this->impl_.IsSpecifiedOnCommandLine();
  }
  std::string DefaultValue() const { return this->impl_.DefaultValue(); }
  std::string CurrentValue() const { return this->impl_.CurrentValue(); }

 private:
  template <typename, bool>
  friend class FlagRegistrar;
  friend class FlagImplPeer;

  T Get() const {
    // See implementation notes in CommandLineFlag::Get().
    union U {
      T value;
      U() {}
      ~U() { value.~T(); }
    };
    U u;

#if !defined(NDEBUG)
    this->impl_.AssertValidType(absl::FastTypeId<T>(), &GenRuntimeTypeId<T>);
#endif

    if constexpr (flags_internal::StorageKind<T>() ==
                  FlagValueStorageKind::kInlineBool) {
      if (ABSL_PREDICT_FALSE(!this->impl_.TryGetInlineBool(u.value))) {
        this->impl_.Read(&u.value);
      }
    } else {
      if (ABSL_PREDICT_FALSE(!this->value_.Get(u.value))) {
        this->impl_.Read(&u.value);
      }
    }
    return std::move(u.value);
  }
  void Set(const T& v) {
    this->impl_.AssertValidType(absl::FastTypeId<T>(), &GenRuntimeTypeId<T>);
    this->impl_.Write(&v);
  }

  // Access to the reflection.
  const CommandLineFlag& Reflect() const { return this->impl_; }
};

///////////////////////////////////////////////////////////////////////////////
// Trampoline for friend access

class FlagImplPeer {
 public:
  template <typename T, typename FlagType>
  static T InvokeGet(const FlagType& flag) {
    return flag.Get();
  }
  template <typename FlagType, typename T>
  static void InvokeSet(FlagType& flag, const T& v) {
    flag.Set(v);
  }
  template <typename FlagType>
  static const CommandLineFlag& InvokeReflect(const FlagType& f) {
    return f.Reflect();
  }
};

///////////////////////////////////////////////////////////////////////////////
// Implementation of Flag value specific operations routine.
template <typename T>
void* FlagOps(FlagOp op, const void* v1, void* v2, void* v3) {
  struct AlignedSpace {
    alignas(MaskedPointer::RequiredAlignment()) alignas(
        T) unsigned char buf[sizeof(T)];
  };
  using Allocator = std::allocator<AlignedSpace>;
  switch (op) {
    case FlagOp::kAlloc: {
      Allocator alloc;
      return std::allocator_traits<Allocator>::allocate(alloc, 1);
    }
    case FlagOp::kDelete: {
      T* p = static_cast<T*>(v2);
      p->~T();
      Allocator alloc;
      std::allocator_traits<Allocator>::deallocate(
          alloc, reinterpret_cast<AlignedSpace*>(p), 1);
      return nullptr;
    }
    case FlagOp::kCopy:
      *static_cast<T*>(v2) = *static_cast<const T*>(v1);
      return nullptr;
    case FlagOp::kCopyConstruct:
      if constexpr (std::is_default_constructible_v<T>) {
        if (v1 == nullptr) {
          new (v2) T();
          return nullptr;
        }
      }
      new (v2) T(*static_cast<const T*>(v1));
      return nullptr;
    case FlagOp::kSizeof:
      return reinterpret_cast<void*>(static_cast<uintptr_t>(sizeof(T)));
    case FlagOp::kFastTypeId:
      return absl::bit_cast<void*>(absl::FastTypeId<T>());
    case FlagOp::kRuntimeTypeId:
      return const_cast<std::type_info*>(GenRuntimeTypeId<T>());
    case FlagOp::kParse: {
      // Initialize the temporary instance of type T based on current value in
      // destination (which is going to be flag's default value).
      T temp(*static_cast<T*>(v2));
      if (!absl::ParseFlag<T>(*static_cast<const absl::string_view*>(v1), &temp,
                              static_cast<std::string*>(v3))) {
        return nullptr;
      }
      *static_cast<T*>(v2) = std::move(temp);
      return v2;
    }
    case FlagOp::kUnparse:
      *static_cast<std::string*>(v2) =
          absl::UnparseFlag<T>(*static_cast<const T*>(v1));
      return nullptr;
    case FlagOp::kValueOffset: {
      // Round sizeof(FlagImp) to a multiple of alignof(FlagValue<T>) to get the
      // offset of the data.
      size_t round_to = alignof(FlagValue<T>);
      size_t offset = (sizeof(FlagImpl) + round_to - 1) / round_to * round_to;
      return reinterpret_cast<void*>(offset);
    }
    case FlagOp::kTypeName:
      *static_cast<absl::string_view*>(v2) = GenTypeName<T>();
      return nullptr;
  }
  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// This class facilitates Flag object registration and tail expression-based
// flag definition, for example:
// ABSL_FLAG(int, foo, 42, "Foo help").OnUpdate(NotifyFooWatcher);
struct FlagRegistrarEmpty {};
template <typename T, bool do_register>
class FlagRegistrar {
 public:
  constexpr explicit FlagRegistrar(Flag<T>& flag, const char* filename,
                                   const char* cold_help = nullptr)
      : flag_(flag) {
    if (filename != nullptr) {
      flag_.impl_.SetFilename(filename);
    }
    if (cold_help != nullptr) {
      flag_.impl_.SetColdHelp(cold_help);
    }
    if (do_register) {
      flags_internal::RegisterCommandLineFlag(flag_.impl_, filename);
    }
  }

  FlagRegistrar OnUpdate(FlagCallbackFunc cb) && {
    flag_.impl_.SetCallback(cb);
    return *this;
  }

  // Makes the registrar die gracefully as an empty struct on a line where
  // registration happens. Registrar objects are intended to live only as
  // temporary.
  constexpr operator FlagRegistrarEmpty() const { return {}; }  // NOLINT

 private:
  Flag<T>& flag_;  // Flag being registered (not owned).
};

///////////////////////////////////////////////////////////////////////////////
// Test only API
uint64_t NumLeakedFlagValues();

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_INTERNAL_FLAG_H_
