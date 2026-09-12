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

#ifndef ABSL_LOG_INTERNAL_APPEND_TRUNCATED_H_
#define ABSL_LOG_INTERNAL_APPEND_TRUNCATED_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "absl/base/config.h"
#include "absl/strings/internal/utf8.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
// Copies into `dst` as many bytes of `src` as will fit, then truncates the
// copied bytes from the front of `dst` and returns the number of bytes written.
inline size_t AppendTruncated(absl::string_view src, absl::Span<char> &dst) {
  if (src.size() > dst.size()) src = src.substr(0, dst.size());
  memcpy(dst.data(), src.data(), src.size());
  dst.remove_prefix(src.size());
  return src.size();
}
inline bool IsHighSurrogate(wchar_t wc) {
  const uint32_t v = static_cast<uint32_t>(wc);
  return v >= 0xD800 && v <= 0xDBFF;
}
inline bool IsLowSurrogate(wchar_t wc) {
  const uint32_t v = static_cast<uint32_t>(wc);
  return v >= 0xDC00 && v <= 0xDFFF;
}
// Likewise, but it also takes a wide character string and transforms it into a
// UTF-8 encoded byte string regardless of the current locale.
// - On platforms where `wchar_t` is 2 bytes (e.g., Windows), the input is
//   treated as UTF-16.
// - On platforms where `wchar_t` is 4 bytes (e.g., Linux, macOS), the input
//   is treated as UTF-32.
inline size_t AppendTruncated(std::wstring_view src, absl::Span<char> &dst) {
  constexpr wchar_t kReplacementCharacter = L'\uFFFD';
  absl::strings_internal::ShiftState state;
  size_t total_bytes_written = 0;
  for (size_t i = 0; i < src.size(); ++i) {
    // A pending high surrogate already reserved the four bytes of the sequence
    // it started, so the low surrogate completing it always fits. Otherwise, if
    // the destination buffer might not be large enough to write the next
    // character, stop.
    if (!state.saw_high_surrogate &&
        dst.size() < absl::strings_internal::kMaxEncodedUTF8Size) {
      break;
    }
    wchar_t wc = src[i];
    // `WideToUtf8()` encodes a surrogate pair over two calls, emitting the
    // first two bytes of a four-byte sequence for the high surrogate and the
    // remaining two for the low one. Unless the matching low surrogate follows
    // immediately, those first two bytes would be left in `dst` as a partial
    // sequence, so encode U+FFFD for the unpaired high surrogate instead.
    if (IsHighSurrogate(wc) &&
        !(i + 1 < src.size() && IsLowSurrogate(src[i + 1]))) {
      wc = kReplacementCharacter;
    }
    size_t bytes_written =
        absl::strings_internal::WideToUtf8(wc, dst.data(), state);
    if (bytes_written == static_cast<size_t>(-1)) {
      // Invalid character. Encode REPLACEMENT CHARACTER (U+FFFD) instead.
      bytes_written = absl::strings_internal::WideToUtf8(kReplacementCharacter,
                                                         dst.data(), state);
    }
    dst.remove_prefix(bytes_written);
    total_bytes_written += bytes_written;
  }
  return total_bytes_written;
}
// Likewise, but `n` copies of `c`.
inline size_t AppendTruncated(char c, size_t n, absl::Span<char> &dst) {
  if (n > dst.size()) n = dst.size();
  memset(dst.data(), c, n);
  dst.remove_prefix(n);
  return n;
}
}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_APPEND_TRUNCATED_H_
