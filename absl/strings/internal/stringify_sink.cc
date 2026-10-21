// Copyright 2022 The Abseil Authors
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

#include "absl/strings/internal/stringify_sink.h"

#include <array>
#include <cstddef>

#include "absl/base/config.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

void StringifySink::Append(size_t count, char ch) { buffer_.append(count, ch); }

void StringifySink::Append(string_view v) {
  buffer_.append(v.data(), v.size());
}

void StringifySourceLocation(StringifySink& sink, SourceLocation l) {
  sink.Append(l.file_name());
  std::array<char, 1 + numbers_internal::kFastToBufferSize> buffer;
  buffer[0] = ':';
  char* end = numbers_internal::FastIntToBuffer(l.line(), buffer.data() + 1);
  sink.Append(absl::string_view(
      buffer.data(), static_cast<size_t>(end - buffer.data())));
}

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl
