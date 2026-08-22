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

#include <cstddef>

#include "absl/base/config.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
bool FNMatch(absl::string_view pattern, absl::string_view str) {
  // Two-pointer glob matcher: '?' matches exactly one character and '*' matches
  // any run of characters (including the empty run). We remember the position
  // just after the most recent '*' so that, on a later mismatch, that '*' can
  // consume one more character of `str` and the match be retried. The previous
  // implementation bound each literal run to its first occurrence in `str` and
  // never backtracked, so a run that recurs later (e.g. "*test" against
  // "testtest") was reported as a non-match even though it matches.
  size_t p = 0;  // Current position in `pattern`.
  size_t s = 0;  // Current position in `str`.
  // `pattern` position just after the most recent '*', and the `str` position
  // when it was seen; `npos` until a '*' has been encountered.
  size_t star_p = absl::string_view::npos;
  size_t star_s = 0;
  while (s < str.size()) {
    if (p < pattern.size() && pattern[p] == '*') {
      star_p = ++p;
      star_s = s;
    } else if (p < pattern.size() &&
               (pattern[p] == '?' || pattern[p] == str[s])) {
      ++p;
      ++s;
    } else if (star_p != absl::string_view::npos) {
      // Backtrack: let the most recent '*' match one more character.
      p = star_p;
      s = ++star_s;
    } else {
      return false;
    }
  }
  // `str` is exhausted; the remainder of `pattern` must be all '*'s.
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}
}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
