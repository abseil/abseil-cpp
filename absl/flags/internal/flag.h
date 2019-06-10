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

#include "absl/flags/internal/commandlineflag.h"
#include "absl/flags/internal/registry.h"

namespace absl {
namespace flags_internal {

// This is "unspecified" implementation of absl::Flag<T> type.
template <typename T>
class Flag {
 public:
  constexpr Flag(const char* name, const flags_internal::HelpGenFunc help_gen,
                 const char* filename,
                 const flags_internal::FlagMarshallingOpFn marshalling_op,
                 const flags_internal::InitialValGenFunc initial_value_gen)
      : internal(name, flags_internal::HelpText::FromFunctionPointer(help_gen),
                 filename, &flags_internal::FlagOps<T>, marshalling_op,
                 initial_value_gen,
                 /*retired_arg=*/false, /*def_arg=*/nullptr,
                 /*cur_arg=*/nullptr) {}

  // Not copyable/assignable.
  Flag(const Flag<T>&) = delete;
  Flag<T>& operator=(const Flag<T>&) = delete;

  absl::string_view Name() const { return internal.Name(); }
  std::string Help() const { return internal.Help(); }
  std::string Filename() const { return internal.Filename(); }

  absl::flags_internal::CommandLineFlag internal;

  void SetCallback(const flags_internal::FlagCallback mutation_callback) {
    internal.SetCallback(mutation_callback);
  }

 private:
  // TODO(rogeeff): add these validations once UnparseFlag invocation is fixed
  // for built-in types and when we cleanup existing code from operating on
  // forward declared types.
  //  auto IsCopyConstructible(const T& v) -> decltype(T(v));
  //  auto HasAbslParseFlag(absl::string_view in, T* dst, std::string* err)
  //      -> decltype(AbslParseFlag(in, dst, err));
  //  auto HasAbslUnparseFlag(const T& v) -> decltype(AbslUnparseFlag(v));
};

// This class facilitates Flag object registration and tail expression-based
// flag definition, for example:
// ABSL_FLAG(int, foo, 42, "Foo help").OnUpdate(NotifyFooWatcher);
template <typename T, bool do_register>
class FlagRegistrar {
 public:
  explicit FlagRegistrar(Flag<T>* flag) : flag_(flag) {
    if (do_register) flags_internal::RegisterCommandLineFlag(&flag_->internal);
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
