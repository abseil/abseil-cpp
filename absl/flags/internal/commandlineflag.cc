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

#include "absl/flags/usage_config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

// The help message indicating that the commandline flag has been
// 'stripped'. It will not show up when doing "-help" and its
// variants. The flag is stripped if ABSL_FLAGS_STRIP_HELP is set to 1
// before including absl/flags/flag.h

// This is used by this file, and also in commandlineflags_reporting.cc
const char kStrippedFlagHelp[] = "\001\002\003\004 (unknown) \004\003\002\001";

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

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl
