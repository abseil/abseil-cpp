// Copyright 2026 The Abseil Authors.
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

#include "absl/strings/internal/string_search.h"

#include <algorithm>
#include <cstddef>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/strings/ascii.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace {

using absl::strings_internal::FindIgnoreCaseLinear;
using absl::strings_internal::FindLongNeedle;
using absl::strings_internal::kMaxShortNeedleSize;
using absl::strings_internal::TwoWaySearcher;

constexpr size_t kNpos = absl::string_view::npos;

size_t NaiveFindIgnoreCase(absl::string_view haystack, absl::string_view needle,
                           size_t pos) {
  if (pos > haystack.size()) return kNpos;
  for (size_t i = pos; i + needle.size() <= haystack.size(); ++i) {
    if (absl::EqualsIgnoreCase(haystack.substr(i, needle.size()), needle)) {
      return i;
    }
  }
  return kNpos;
}

// Returns a random string over `alphabet`. Small alphabets make matches,
// near-matches and periodic needles common.
std::string RandomString(std::mt19937& rng, absl::string_view alphabet,
                         size_t size) {
  std::uniform_int_distribution<size_t> dist(0, alphabet.size() - 1);
  std::string s(size, ' ');
  for (char& c : s) c = alphabet[dist(rng)];
  return s;
}

// Returns a needle that is often periodic: a random string repeated, with an
// occasional mutation.
std::string RandomNeedle(std::mt19937& rng, absl::string_view alphabet,
                         size_t size) {
  std::uniform_int_distribution<size_t> period_dist(1, size);
  const std::string unit = RandomString(rng, alphabet, period_dist(rng));
  std::string s;
  while (s.size() < size) s += unit;
  s.resize(size);
  if (rng() % 2 == 0) {
    s[rng() % size] = alphabet[rng() % alphabet.size()];
  }
  return s;
}

TEST(TwoWaySearcherTest, MatchesFind) {
  std::mt19937 rng(1);
  for (absl::string_view alphabet : {"ab", "abc", "abcdefgh"}) {
    for (int iter = 0; iter < 3000; ++iter) {
      const std::string needle = RandomNeedle(rng, alphabet, 1 + rng() % 12);
      const std::string haystack = RandomString(rng, alphabet, rng() % 64);
      const TwoWaySearcher<false> searcher(needle);
      for (size_t pos = 0; pos <= haystack.size() + 1; ++pos) {
        ASSERT_EQ(searcher.Find(haystack, needle, pos),
                  haystack.find(needle, pos))
            << "haystack=" << haystack << " needle=" << needle
            << " pos=" << pos;
      }
    }
  }
}

TEST(TwoWaySearcherTest, IgnoreCaseMatchesNaive) {
  std::mt19937 rng(2);
  for (absl::string_view alphabet : {"aA", "aAbB", "aAbBc-"}) {
    for (int iter = 0; iter < 3000; ++iter) {
      const std::string needle = RandomNeedle(rng, alphabet, 1 + rng() % 12);
      const std::string haystack = RandomString(rng, alphabet, rng() % 64);
      const TwoWaySearcher<true> searcher(needle);
      for (size_t pos = 0; pos <= haystack.size() + 1; ++pos) {
        ASSERT_EQ(searcher.Find(haystack, needle, pos),
                  NaiveFindIgnoreCase(haystack, needle, pos))
            << "haystack=" << haystack << " needle=" << needle
            << " pos=" << pos;
      }
    }
  }
}

TEST(TwoWaySearcherTest, ShortPeriod) {
  EXPECT_EQ(TwoWaySearcher<false>("abababab").short_period(), size_t{2});
  EXPECT_EQ(TwoWaySearcher<false>("aaaa").short_period(), size_t{1});
  EXPECT_EQ(TwoWaySearcher<false>("abcab").short_period(), size_t{0});
  EXPECT_EQ(TwoWaySearcher<false>("abcdef").short_period(), size_t{0});
  EXPECT_EQ(TwoWaySearcher<true>("aAaA").short_period(), size_t{1});
}

TEST(FindLongNeedleTest, MatchesFind) {
  std::mt19937 rng(3);
  for (absl::string_view alphabet : {"ab", "abc", "abcdefghijklmnop"}) {
    for (int iter = 0; iter < 300; ++iter) {
      const size_t m = kMaxShortNeedleSize + 1 + rng() % 200;
      const std::string needle = RandomNeedle(rng, alphabet, m);
      // Embed occurrences and near-occurrences of the needle.
      std::string haystack;
      while (haystack.size() < 20000) {
        switch (rng() % 3) {
          case 0:
            haystack += RandomString(rng, alphabet, rng() % 100);
            break;
          case 1:
            haystack += needle.substr(0, rng() % m);
            break;
          default:
            haystack += needle;
        }
      }
      for (size_t pos = 0; pos <= haystack.size() + 1; pos += 1 + rng() % 997) {
        ASSERT_EQ(FindLongNeedle(haystack, needle, pos),
                  haystack.find(needle, pos))
            << "needle=" << needle << " pos=" << pos;
      }
    }
  }
}

// Adversarial inputs, for which a naive search takes
// O(haystack.size() * needle.size()) time. Each test must finish quickly.
class AdversarialTest : public testing::Test {
 protected:
  static constexpr size_t kHaystackSize = 4 << 20;
  static constexpr size_t kNeedleSize = 2 << 20;

  // 'a' x n.
  const std::string haystack_ = std::string(kHaystackSize, 'a');
  // 'a' x (m - 1) + 'b': defeats memchr() + memcmp().
  const std::string needle_ab_ = std::string(kNeedleSize - 1, 'a') + "b";
  // 'a' x k + 'b' + 'a' x k: defeats Boyer-Moore-Horspool with first- and
  // last-byte prefilters.
  const std::string needle_aba_ = std::string(kNeedleSize / 2, 'a') + "b" +
                                  std::string(kNeedleSize / 2, 'a');
  // 'b' + 'a' x (m - 1).
  const std::string needle_ba_ = "b" + std::string(kNeedleSize - 1, 'a');
};

TEST_F(AdversarialTest, Find) {
  for (const std::string* needle : {&needle_ab_, &needle_aba_, &needle_ba_}) {
    EXPECT_EQ(absl::strings_internal::Find(haystack_, *needle), kNpos);
    EXPECT_FALSE(absl::StrContains(haystack_, *needle));
    const std::string with_match = haystack_ + *needle;
    EXPECT_EQ(absl::strings_internal::Find(with_match, *needle),
              haystack_.size());
    EXPECT_TRUE(absl::StrContains(with_match, *needle));
  }
}

TEST_F(AdversarialTest, StrContainsIgnoreCase) {
  for (const std::string* needle : {&needle_ab_, &needle_aba_, &needle_ba_}) {
    EXPECT_FALSE(absl::StrContainsIgnoreCase(haystack_, *needle));
    EXPECT_TRUE(absl::StrContainsIgnoreCase(
        absl::StrCat(haystack_, absl::AsciiStrToUpper(*needle)), *needle));
  }
  EXPECT_EQ(FindIgnoreCaseLinear(haystack_, needle_aba_, 0), kNpos);
}

TEST_F(AdversarialTest, StrSplit) {
  const std::string text = absl::StrCat(haystack_, needle_ab_, haystack_);
  std::vector<absl::string_view> pieces = absl::StrSplit(text, needle_ab_);
  ASSERT_EQ(pieces.size(), size_t{2});
  EXPECT_EQ(pieces[0], haystack_);
  EXPECT_EQ(pieces[1], haystack_);
}

TEST_F(AdversarialTest, StrReplaceAll) {
  EXPECT_EQ(absl::StrReplaceAll(haystack_, {{needle_ab_, "x"}}), haystack_);
  EXPECT_EQ(absl::StrReplaceAll(absl::StrCat(haystack_, needle_ab_),
                                {{needle_ab_, "x"}}),
            absl::StrCat(haystack_, "x"));
  EXPECT_EQ(StringReplace(haystack_, needle_ab_, "x", true), haystack_);
}

// With several substitutions, a long substitution can be searched for again
// within its previous occurrence, after a shorter one is applied over it.
TEST(StrReplaceAllTest, OverlappingSubstitutions) {
  constexpr size_t kPairs = 1 << 20;
  std::string text;
  for (size_t i = 0; i < kPairs; ++i) text += "ab";
  std::string long_old;
  for (size_t i = 0; i < 1000; ++i) long_old += "ba";
  // "ab" always occurs first, so "ba..." never gets replaced, but it occurs
  // at every odd position.
  EXPECT_EQ(absl::StrReplaceAll(text, {{"ab", ""}, {long_old, "x"}}), "");
  // Non-periodic long needle, overlapped by "ab" at every occurrence.
  const std::string long_old2 = "b" + std::string(100, 'a') + "c";
  std::string text2, expected2;
  for (size_t i = 0; i < 20000; ++i) {
    text2 += "a" + long_old2;
    expected2 += std::string(100, 'a') + "c";
  }
  EXPECT_EQ(absl::StrReplaceAll(text2, {{"ab", ""}, {long_old2, "x"}}),
            expected2);
}

TEST(StrReplaceAllTest, MatchesNaive) {
  std::mt19937 rng(4);
  for (int iter = 0; iter < 500; ++iter) {
    const std::string a = RandomNeedle(rng, "ab", 1 + rng() % 3);
    const std::string b =
        RandomNeedle(rng, "ab", kMaxShortNeedleSize + 1 + rng() % 40);
    const std::string text = RandomString(rng, "ab", rng() % 2000) + b +
                             RandomString(rng, "ab", rng() % 100) + b + a;
    // Reference: the specified semantics, applied naively.
    std::string expected;
    size_t pos = 0;
    while (pos < text.size()) {
      size_t best = kNpos, best_len = 0;
      for (const std::string* old : {&a, &b}) {
        const size_t p = text.find(*old, pos);
        if (p < best || (p == best && old->size() > best_len)) {
          best = p;
          best_len = old->size();
        }
      }
      if (best == kNpos) break;
      expected.append(text, pos, best - pos);
      expected += best_len == a.size() ? "1" : "2";
      pos = best + best_len;
    }
    expected.append(text, (std::min)(pos, text.size()));
    ASSERT_EQ(absl::StrReplaceAll(text, {{a, "1"}, {b, "2"}}), expected)
        << "a=" << a << " b=" << b << " text=" << text;
  }
}

// Returns a Cord with the contents of `s` split into chunks of `chunk_size`.
absl::Cord MakeFragmentedCord(absl::string_view s, size_t chunk_size) {
  absl::Cord cord;
  for (size_t i = 0; i < s.size(); i += chunk_size) {
    cord.Append(absl::MakeCordFromExternal(s.substr(i, chunk_size),
                                           [](absl::string_view) {}));
  }
  return cord;
}

TEST_F(AdversarialTest, CordFind) {
  for (const std::string* needle : {&needle_ab_, &needle_aba_, &needle_ba_}) {
    const absl::Cord needle_cord = MakeFragmentedCord(*needle, 4096);
    const std::string with_match = haystack_ + *needle;
    for (size_t chunk_size : {size_t{1000}, size_t{1} << 20}) {
      const absl::Cord cord = MakeFragmentedCord(haystack_, chunk_size);
      EXPECT_EQ(cord.Find(*needle), cord.char_end());
      EXPECT_EQ(cord.Find(needle_cord), cord.char_end());
      const absl::Cord match_cord = MakeFragmentedCord(with_match, chunk_size);
      EXPECT_EQ(static_cast<size_t>(absl::Cord::Distance(
                    match_cord.char_begin(), match_cord.Find(*needle))),
                haystack_.size());
      EXPECT_EQ(static_cast<size_t>(absl::Cord::Distance(
                    match_cord.char_begin(), match_cord.Find(needle_cord))),
                haystack_.size());
    }
  }
}

TEST(CordFindTest, MatchesFind) {
  std::mt19937 rng(5);
  for (int iter = 0; iter < 300; ++iter) {
    const size_t m = 2 + rng() % 400;
    const std::string needle = RandomNeedle(rng, "ab", m);
    std::string haystack;
    while (haystack.size() < 30000) {
      haystack += rng() % 4 == 0 ? needle : needle.substr(0, rng() % m);
      haystack += RandomString(rng, "ab", rng() % 3);
    }
    const absl::Cord cord = MakeFragmentedCord(haystack, 1 + rng() % 5000);
    const size_t expected = haystack.find(needle);
    const absl::Cord::CharIterator it = cord.Find(needle);
    if (expected == kNpos) {
      ASSERT_EQ(it, cord.char_end());
    } else {
      ASSERT_EQ(
          static_cast<size_t>(absl::Cord::Distance(cord.char_begin(), it)),
          expected)
          << "needle=" << needle;
    }
  }
}

}  // namespace
