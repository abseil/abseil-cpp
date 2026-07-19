// Copyright 2025 The Abseil Authors
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

#include "absl/log/internal/append_truncated.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/types/span.h"

namespace {

using ::absl::log_internal::AppendTruncated;

// Runs `AppendTruncated(src, ...)` against a `capacity`-byte buffer and returns
// the bytes it wrote.
std::string Append(std::wstring_view src, size_t capacity) {
  std::array<char, 64> buffer{};
  absl::Span<char> dst(buffer.data(), capacity);
  const size_t bytes_written = AppendTruncated(src, dst);
  EXPECT_LE(bytes_written, capacity);
  return std::string(buffer.data(), bytes_written);
}

const std::string kReplacement = "\xEF\xBF\xBD";  // U+FFFD

TEST(AppendTruncatedTest, EncodesSurrogatePair) {
  // U+1F600, as a UTF-16 pair on platforms with a 2-byte wchar_t.
  EXPECT_EQ(Append(L"\xD83D\xDE00", 32), "\xF0\x9F\x98\x80");
}

TEST(AppendTruncatedTest, TrailingUnpairedHighSurrogate) {
  // The high surrogate encodes only the first two bytes of a four-byte
  // sequence, so emitting it alone would leave a partial sequence behind.
  EXPECT_EQ(Append(L"\xD800", 32), kReplacement);
}

TEST(AppendTruncatedTest, HighSurrogateNotFollowedByLowSurrogate) {
  const std::wstring high_then_ascii = std::wstring(1, wchar_t{0xD800}) + L"A";
  EXPECT_EQ(Append(high_then_ascii, 32), kReplacement + "A");
  EXPECT_EQ(Append(L"\xD800\xD801", 32), kReplacement + kReplacement);
}

TEST(AppendTruncatedTest, IsolatedLowSurrogate) {
  EXPECT_EQ(Append(L"\xDC00", 32), kReplacement);
}

TEST(AppendTruncatedTest, SurrogatePairAtTruncationBoundary) {
  const std::wstring src =
      std::wstring(L"a") + wchar_t{0xD83D} + wchar_t{0xDE00};
  // Five bytes is exactly enough for "a" plus the four-byte sequence.
  EXPECT_EQ(Append(src, 5), "a\xF0\x9F\x98\x80");
  EXPECT_EQ(Append(src, 6), "a\xF0\x9F\x98\x80");
  // Four bytes is not, so the pair is dropped rather than half-written.
  EXPECT_EQ(Append(src, 4), "a");
}

TEST(AppendTruncatedTest, PlainCharactersAreUnaffected) {
  EXPECT_EQ(Append(L"hello", 32), "hello");
  EXPECT_EQ(Append(L"é中", 32), "\xC3\xA9\xE4\xB8\xAD");
}

}  // namespace
