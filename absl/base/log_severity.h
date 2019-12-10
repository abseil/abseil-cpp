// Copyright 2017 The Abseil Authors.
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

#ifndef ABSL_BASE_INTERNAL_LOG_SEVERITY_H_
#define ABSL_BASE_INTERNAL_LOG_SEVERITY_H_

#include <array>
#include <ostream>

#include "absl/base/attributes.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Four severity levels are defined.  Logging APIs should terminate the program
// when a message is logged at severity `kFatal`; the other levels have no
// special semantics.
//
// Abseil flags may be defined with type `LogSeverity`.  Dependency layering
// constraints require that the `AbslParseFlag` overload be declared and defined
// in the flags module rather than here.  The `AbslUnparseFlag` overload is
// defined there too for consistency.
//
// The parser accepts arbitrary integers (as if the type were `int`).  It also
// accepts each named enumerator, without regard for case, with or without the
// leading 'k'.  For example: "kInfo", "INFO", and "info" all parse to the value
// `absl::LogSeverity::kInfo`.
//
// Unparsing a flag produces the same result as `absl::LogSeverityName()` for
// the standard levels and a base-ten integer otherwise.
enum class LogSeverity : int {
  kInfo = 0,
  kWarning = 1,
  kError = 2,
  kFatal = 3,
};

// Returns an iterable of all standard `absl::LogSeverity` values, ordered from
// least to most severe.
constexpr std::array<absl::LogSeverity, 4> LogSeverities() {
  return {{absl::LogSeverity::kInfo, absl::LogSeverity::kWarning,
           absl::LogSeverity::kError, absl::LogSeverity::kFatal}};
}

// Returns the all-caps string representation (e.g. "INFO") of the specified
// severity level if it is one of the standard levels and "UNKNOWN" otherwise.
constexpr const char* LogSeverityName(absl::LogSeverity s) {
  return s == absl::LogSeverity::kInfo
             ? "INFO"
             : s == absl::LogSeverity::kWarning
                   ? "WARNING"
                   : s == absl::LogSeverity::kError
                         ? "ERROR"
                         : s == absl::LogSeverity::kFatal ? "FATAL" : "UNKNOWN";
}

// Values less than `kInfo` normalize to `kInfo`; values greater than `kFatal`
// normalize to `kError` (**NOT** `kFatal`).
constexpr absl::LogSeverity NormalizeLogSeverity(absl::LogSeverity s) {
  return s < absl::LogSeverity::kInfo
             ? absl::LogSeverity::kInfo
             : s > absl::LogSeverity::kFatal ? absl::LogSeverity::kError : s;
}
constexpr absl::LogSeverity NormalizeLogSeverity(int s) {
  return absl::NormalizeLogSeverity(static_cast<absl::LogSeverity>(s));
}

// The exact representation of a streamed `absl::LogSeverity` is deliberately
// unspecified; do not rely on it.
std::ostream& operator<<(std::ostream& os, absl::LogSeverity s);

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_LOG_SEVERITY_H_
