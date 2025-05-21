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

#include "absl/container/internal/hashtable_control_bytes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

// Convenience function to static cast to ctrl_t.
ctrl_t CtrlT(int i) { return static_cast<ctrl_t>(i); }

TEST(BitMask, Smoke) {
  EXPECT_FALSE((BitMask<uint8_t, 8>(0)));
  EXPECT_TRUE((BitMask<uint8_t, 8>(5)));

  EXPECT_THAT((BitMask<uint8_t, 8>(0)), ElementsAre());
  EXPECT_THAT((BitMask<uint8_t, 8>(0x1)), ElementsAre(0));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x2)), ElementsAre(1));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x3)), ElementsAre(0, 1));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x4)), ElementsAre(2));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x5)), ElementsAre(0, 2));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x55)), ElementsAre(0, 2, 4, 6));
  EXPECT_THAT((BitMask<uint8_t, 8>(0xAA)), ElementsAre(1, 3, 5, 7));
}

TEST(BitMask, WithShift_MatchPortable) {
  // See the non-SSE version of Group for details on what this math is for.
  uint64_t ctrl = 0x1716151413121110;
  uint64_t hash = 0x12;
  constexpr uint64_t lsbs = 0x0101010101010101ULL;
  auto x = ctrl ^ (lsbs * hash);
  uint64_t mask = (x - lsbs) & ~x & kMsbs8Bytes;
  EXPECT_EQ(0x0000000080800000, mask);

  BitMask<uint64_t, 8, 3> b(mask);
  EXPECT_EQ(*b, 2);
}

constexpr uint64_t kSome8BytesMask = /*  */ 0x8000808080008000ULL;
constexpr uint64_t kSome8BytesMaskAllOnes = 0xff00ffffff00ff00ULL;
constexpr auto kSome8BytesMaskBits = std::array<int, 5>{1, 3, 4, 5, 7};

TEST(BitMask, WithShift_FullMask) {
  EXPECT_THAT((BitMask<uint64_t, 8, 3>(kMsbs8Bytes)),
              ElementsAre(0, 1, 2, 3, 4, 5, 6, 7));
  EXPECT_THAT(
      (BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(kMsbs8Bytes)),
      ElementsAre(0, 1, 2, 3, 4, 5, 6, 7));
  EXPECT_THAT(
      (BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(~uint64_t{0})),
      ElementsAre(0, 1, 2, 3, 4, 5, 6, 7));
}

TEST(BitMask, WithShift_EmptyMask) {
  EXPECT_THAT((BitMask<uint64_t, 8, 3>(0)), ElementsAre());
  EXPECT_THAT((BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(0)),
              ElementsAre());
}

TEST(BitMask, WithShift_SomeMask) {
  EXPECT_THAT((BitMask<uint64_t, 8, 3>(kSome8BytesMask)),
              ElementsAreArray(kSome8BytesMaskBits));
  EXPECT_THAT((BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(
                  kSome8BytesMask)),
              ElementsAreArray(kSome8BytesMaskBits));
  EXPECT_THAT((BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(
                  kSome8BytesMaskAllOnes)),
              ElementsAreArray(kSome8BytesMaskBits));
}

TEST(BitMask, WithShift_SomeMaskExtraBitsForNullify) {
  // Verify that adding extra bits into non zero bytes is fine.
  uint64_t extra_bits = 77;
  for (int i = 0; i < 100; ++i) {
    // Add extra bits, but keep zero bytes untouched.
    uint64_t extra_mask = extra_bits & kSome8BytesMaskAllOnes;
    EXPECT_THAT((BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(
                    kSome8BytesMask | extra_mask)),
                ElementsAreArray(kSome8BytesMaskBits))
        << i << " " << extra_mask;
    extra_bits = (extra_bits + 1) * 3;
  }
}

TEST(BitMask, LeadingTrailing) {
  EXPECT_EQ((BitMask<uint32_t, 16>(0x00001a40).LeadingZeros()), 3);
  EXPECT_EQ((BitMask<uint32_t, 16>(0x00001a40).TrailingZeros()), 6);

  EXPECT_EQ((BitMask<uint32_t, 16>(0x00000001).LeadingZeros()), 15);
  EXPECT_EQ((BitMask<uint32_t, 16>(0x00000001).TrailingZeros()), 0);

  EXPECT_EQ((BitMask<uint32_t, 16>(0x00008000).LeadingZeros()), 0);
  EXPECT_EQ((BitMask<uint32_t, 16>(0x00008000).TrailingZeros()), 15);

  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x0000008080808000).LeadingZeros()), 3);
  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x0000008080808000).TrailingZeros()), 1);

  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x0000000000000080).LeadingZeros()), 7);
  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x0000000000000080).TrailingZeros()), 0);

  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x8000000000000000).LeadingZeros()), 0);
  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x8000000000000000).TrailingZeros()), 7);
}

TEST(Group, Match) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {ctrl_t::kEmpty, CtrlT(1), ctrl_t::kDeleted,  CtrlT(3),
                      ctrl_t::kEmpty, CtrlT(5), ctrl_t::kSentinel, CtrlT(7),
                      CtrlT(7),       CtrlT(5), CtrlT(3),          CtrlT(1),
                      CtrlT(1),       CtrlT(1), CtrlT(1),          CtrlT(1)};
    EXPECT_THAT(Group{group}.Match(0), ElementsAre());
    EXPECT_THAT(Group{group}.Match(1), ElementsAre(1, 11, 12, 13, 14, 15));
    EXPECT_THAT(Group{group}.Match(3), ElementsAre(3, 10));
    EXPECT_THAT(Group{group}.Match(5), ElementsAre(5, 9));
    EXPECT_THAT(Group{group}.Match(7), ElementsAre(7, 8));
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), CtrlT(2),
                      ctrl_t::kDeleted,  CtrlT(2), CtrlT(1),
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.Match(0), ElementsAre());
    EXPECT_THAT(Group{group}.Match(1), ElementsAre(1, 5, 7));
    EXPECT_THAT(Group{group}.Match(2), ElementsAre(2, 4));
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Group, MaskEmpty) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {ctrl_t::kEmpty, CtrlT(1), ctrl_t::kDeleted,  CtrlT(3),
                      ctrl_t::kEmpty, CtrlT(5), ctrl_t::kSentinel, CtrlT(7),
                      CtrlT(7),       CtrlT(5), CtrlT(3),          CtrlT(1),
                      CtrlT(1),       CtrlT(1), CtrlT(1),          CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskEmpty().LowestBitSet(), 0);
    EXPECT_THAT(Group{group}.MaskEmpty().HighestBitSet(), 4);
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), CtrlT(2),
                      ctrl_t::kDeleted,  CtrlT(2), CtrlT(1),
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskEmpty().LowestBitSet(), 0);
    EXPECT_THAT(Group{group}.MaskEmpty().HighestBitSet(), 0);
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Group, MaskFull) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {
        ctrl_t::kEmpty, CtrlT(1),          ctrl_t::kDeleted,  CtrlT(3),
        ctrl_t::kEmpty, CtrlT(5),          ctrl_t::kSentinel, CtrlT(7),
        CtrlT(7),       CtrlT(5),          ctrl_t::kDeleted,  CtrlT(1),
        CtrlT(1),       ctrl_t::kSentinel, ctrl_t::kEmpty,    CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskFull(),
                ElementsAre(1, 3, 5, 7, 8, 9, 11, 12, 15));
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), ctrl_t::kEmpty,
                      ctrl_t::kDeleted,  CtrlT(2), ctrl_t::kSentinel,
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskFull(), ElementsAre(1, 4, 7));
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Group, MaskNonFull) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {
        ctrl_t::kEmpty, CtrlT(1),          ctrl_t::kDeleted,  CtrlT(3),
        ctrl_t::kEmpty, CtrlT(5),          ctrl_t::kSentinel, CtrlT(7),
        CtrlT(7),       CtrlT(5),          ctrl_t::kDeleted,  CtrlT(1),
        CtrlT(1),       ctrl_t::kSentinel, ctrl_t::kEmpty,    CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskNonFull(),
                ElementsAre(0, 2, 4, 6, 10, 13, 14));
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), ctrl_t::kEmpty,
                      ctrl_t::kDeleted,  CtrlT(2), ctrl_t::kSentinel,
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskNonFull(), ElementsAre(0, 2, 3, 5, 6));
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Group, MaskEmptyOrDeleted) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {ctrl_t::kEmpty,   CtrlT(1), ctrl_t::kEmpty,    CtrlT(3),
                      ctrl_t::kDeleted, CtrlT(5), ctrl_t::kSentinel, CtrlT(7),
                      CtrlT(7),         CtrlT(5), CtrlT(3),          CtrlT(1),
                      CtrlT(1),         CtrlT(1), CtrlT(1),          CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskEmptyOrDeleted().LowestBitSet(), 0);
    EXPECT_THAT(Group{group}.MaskEmptyOrDeleted().HighestBitSet(), 4);
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), CtrlT(2),
                      ctrl_t::kDeleted,  CtrlT(2), CtrlT(1),
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskEmptyOrDeleted().LowestBitSet(), 0);
    EXPECT_THAT(Group{group}.MaskEmptyOrDeleted().HighestBitSet(), 3);
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Group, CountLeadingEmptyOrDeleted) {
  const std::vector<ctrl_t> empty_examples = {ctrl_t::kEmpty, ctrl_t::kDeleted};
  const std::vector<ctrl_t> full_examples = {
      CtrlT(0), CtrlT(1), CtrlT(2),   CtrlT(3),
      CtrlT(5), CtrlT(9), CtrlT(127), ctrl_t::kSentinel};

  for (ctrl_t empty : empty_examples) {
    std::vector<ctrl_t> e(Group::kWidth, empty);
    EXPECT_EQ(Group::kWidth, Group{e.data()}.CountLeadingEmptyOrDeleted());
    for (ctrl_t full : full_examples) {
      for (size_t i = 0; i != Group::kWidth; ++i) {
        std::vector<ctrl_t> f(Group::kWidth, empty);
        f[i] = full;
        EXPECT_EQ(i, Group{f.data()}.CountLeadingEmptyOrDeleted());
      }
      std::vector<ctrl_t> f(Group::kWidth, empty);
      f[Group::kWidth * 2 / 3] = full;
      f[Group::kWidth / 2] = full;
      EXPECT_EQ(Group::kWidth / 2,
                Group{f.data()}.CountLeadingEmptyOrDeleted());
    }
  }
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
