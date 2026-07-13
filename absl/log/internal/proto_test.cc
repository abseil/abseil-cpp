//
// Copyright 2024 The Abseil Authors.
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

#include "absl/log/internal/proto.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
namespace {

TEST(ProtoTest, DecodeVarintRoundTrips) {
  std::vector<char> buf(BufferSizeFor(1, WireType::kVarint));
  absl::Span<char> encode_span(buf);
  ASSERT_TRUE(EncodeVarint(1, uint64_t{300}, &encode_span));

  absl::Span<const char> decode_span(buf.data(),
                                     buf.size() - encode_span.size());
  ProtoField field;
  ASSERT_TRUE(field.DecodeFrom(&decode_span));
  EXPECT_EQ(field.tag(), 1);
  EXPECT_EQ(field.type(), WireType::kVarint);
  EXPECT_EQ(field.uint64_value(), 300);
  EXPECT_TRUE(decode_span.empty());
}

// A varint whose bytes all set the continuation bit is malformed, but the
// decoder is meant to tolerate arbitrary input.  Before the length cap this
// drove the shift exponent past the width of the accumulator (`<< 70`), which
// is undefined behavior that UBSan flags.  The decoder must stop after the
// 10-byte maximum of a 64-bit varint instead.
TEST(ProtoTest, DecodeOverlongVarintDoesNotOverflowShift) {
  std::vector<char> buf;
  buf.push_back(static_cast<char>(MakeTagType(1, WireType::kVarint)));
  for (int i = 0; i < 11; ++i) buf.push_back(static_cast<char>(0x80));
  buf.push_back(static_cast<char>(0x01));

  absl::Span<const char> decode_span(buf);
  ProtoField field;
  ASSERT_TRUE(field.DecodeFrom(&decode_span));
  EXPECT_EQ(field.type(), WireType::kVarint);
  // At most the 10 varint bytes are consumed, leaving the trailing bytes.
  EXPECT_FALSE(decode_span.empty());
}

}  // namespace
}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
