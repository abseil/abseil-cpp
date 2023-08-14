// Copyright 2023 The Abseil Authors
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

#include "absl/log/internal/fnmatch.h"

#include "absl/base/config.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
bool FNMatch(absl::string_view pattern, absl::string_view str) {
  while (true) {
    if (pattern.empty()) {
      // `pattern` is exhausted; succeed if all of `str` was consumed matching
      // it.
      return str.empty();
    }
    if (str.empty()) {
      // `str` is exhausted; succeed if `pattern` is empty or all '*'s.
      return pattern.find_first_not_of('*') == pattern.npos;
    }
    if (pattern.front() == '*') {
      pattern.remove_prefix(1);
      if (pattern.empty()) return true;
      do {
        if (FNMatch(pattern, str)) return true;
        str.remove_prefix(1);
      } while (!str.empty());
      return false;
    }
    if (pattern.front() == '?' || pattern.front() == str.front()) {
      pattern.remove_prefix(1);
      str.remove_prefix(1);
      continue;
    }
    return false;
  }
}
}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
