//
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

#include "absl/strings/internal/str_format/extension.h"

#include <errno.h>
#include <algorithm>
#include <string>

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {

std::string Flags::ToString() const {
  std::string s;
  s.append(left     ? "-" : "");
  s.append(show_pos ? "+" : "");
  s.append(sign_col ? " " : "");
  s.append(alt      ? "#" : "");
  s.append(zero     ? "0" : "");
  return s;
}

bool FormatSinkImpl::PutPaddedString(string_view value, int width,
                                     int precision, bool left) {
  size_t space_remaining = 0;
  if (width >= 0) space_remaining = width;
  size_t n = value.size();
  if (precision >= 0) n = std::min(n, static_cast<size_t>(precision));
  string_view shown(value.data(), n);
  space_remaining = Excess(shown.size(), space_remaining);
  if (!left) Append(space_remaining, ' ');
  Append(shown);
  if (left) Append(space_remaining, ' ');
  return true;
}

}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl
