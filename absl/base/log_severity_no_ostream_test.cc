// Copyright 2026 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/log_severity.h"

#ifdef ABSL_INTERNAL_HAVE_STD_OSTREAM
#error "ABSL_INTERNAL_HAVE_STD_OSTREAM must not be defined in this test."
#endif

#include <cstring>
#include <iosfwd>
#include <type_traits>
#include <utility>

namespace {

template <typename T, typename = void>
struct HasOstreamInsertion : std::false_type {};

template <typename T>
struct HasOstreamInsertion<T,
                           std::void_t<decltype(std::declval<std::ostream&>()
                                                << std::declval<const T&>())>>
    : std::true_type {};

static_assert(!HasOstreamInsertion<absl::LogSeverity>::value,
              "LogSeverity operator<< must be omitted");
static_assert(!HasOstreamInsertion<absl::LogSeverityAtLeast>::value,
              "LogSeverityAtLeast operator<< must be omitted");
static_assert(!HasOstreamInsertion<absl::LogSeverityAtMost>::value,
              "LogSeverityAtMost operator<< must be omitted");

static_assert(absl::LogSeverities().size() == 4, "");
static_assert(absl::NormalizeLogSeverity(-1) == absl::LogSeverity::kInfo, "");
static_assert(absl::NormalizeLogSeverity(99) == absl::LogSeverity::kError, "");
static_assert(absl::LogSeverity::kError < absl::LogSeverityAtLeast::kFatal, "");
static_assert(absl::LogSeverity::kError > absl::LogSeverityAtMost::kWarning,
              "");
static_assert(absl::kLogDebugFatal == absl::LogSeverity::kError ||
                  absl::kLogDebugFatal == absl::LogSeverity::kFatal,
              "");

}  // namespace

int main() {
  if (std::strcmp(absl::LogSeverityName(absl::LogSeverity::kInfo), "INFO") !=
      0) {
    return 1;
  }
  if (std::strcmp(absl::LogSeverityName(absl::LogSeverity::kWarning),
                  "WARNING") != 0) {
    return 2;
  }
  if (std::strcmp(absl::LogSeverityName(absl::LogSeverity::kError), "ERROR") !=
      0) {
    return 3;
  }
  if (std::strcmp(absl::LogSeverityName(absl::LogSeverity::kFatal), "FATAL") !=
      0) {
    return 4;
  }
  if (std::strcmp(absl::LogSeverityName(static_cast<absl::LogSeverity>(42)),
                  "UNKNOWN") != 0) {
    return 5;
  }
  return 0;
}
