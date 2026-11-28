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
//
// Worst-case linear-time substring search.
//
// `std::string_view::find()` (and so `absl::string_view::find()`) in common
// standard libraries looks for the first byte of the needle with memchr() and
// then compares the whole needle with memcmp(), advancing by one byte after
// each mismatch. That takes O(haystack.size() * needle.size()) time in the
// worst case, e.g. for haystack "aaa...a" and needle "aa...ab", which is
// minutes of CPU for inputs of a few MB.
//
// The functions in this file keep that fast path, but bound the work it may do
// to `kMaxShortNeedleSize` byte comparisons per haystack byte. If a search
// exceeds that budget, it continues with the Two-Way algorithm (Crochemore and
// Perrin, "Two-way string-matching", JACM 38(3), 1991), which takes worst-case
// linear time and O(1) extra memory. Typical searches never exceed the budget,
// so they use the same fast path as before.

#ifndef ABSL_STRINGS_INTERNAL_STRING_SEARCH_H_
#define ABSL_STRINGS_INTERNAL_STRING_SEARCH_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

// Needles of at most this many bytes are searched with a naive search, whose
// worst case is bounded by `kMaxShortNeedleSize * haystack.size()` byte
// comparisons. This is also the number of byte comparisons per haystack byte
// that a naive search of a longer needle may do before switching to Two-Way.
inline constexpr size_t kMaxShortNeedleSize = 64;

// Returns true if a naive search for a needle of `needle_size` bytes, starting
// at `pos` in a haystack of `haystack_size` bytes, takes worst-case linear time
// with a constant factor of at most `kMaxShortNeedleSize`: either the needle is
// short, or there are at most `kMaxShortNeedleSize` candidate positions. This
// is cheap enough to inline.
inline bool NaiveSearchIsFast(size_t needle_size, size_t haystack_size,
                              size_t pos = 0) {
  // If `pos` is huge, the sum wraps around and this returns false, which is
  // safe: FindLongNeedle() handles any `pos`.
  // Bitwise | avoids a second branch on hot paths.
  return (needle_size <= kMaxShortNeedleSize) |
         (haystack_size < pos + needle_size + kMaxShortNeedleSize);
}

// Candidates are first compared on this many bytes. A candidate that differs
// from the needle within them costs `kProbeSize` byte comparisons, so such
// candidates alone never exceed the budget.
inline constexpr size_t kProbeSize = 16;
static_assert(kProbeSize < kMaxShortNeedleSize,
              "Failed probes must stay within the budget");

// Tracks the work done by a naive search, so that it can switch to a linear
// algorithm before exceeding `kMaxShortNeedleSize` byte comparisons per
// haystack byte (plus an initial allowance of `kMaxShortNeedleSize` failed
// candidates).
class NaiveSearchBudget {
 public:
  // `start` is the position where the search starts.
  explicit NaiveSearchBudget(size_t needle_size, size_t start)
      : needle_size_(needle_size), base_(start) {}

  // Records `cost` byte comparisons (by default, a failed candidate) at
  // `position`. Returns false if the search should switch to a linear
  // algorithm.
  bool Spend(size_t position) { return Spend(position, needle_size_); }
  bool Spend(size_t position, size_t cost) {
    spent_ += cost;
    return spent_ <= kMaxShortNeedleSize * (position - base_ + needle_size_);
  }

 private:
  size_t needle_size_;
  size_t base_;
  size_t spent_ = 0;
};

// Precomputed state for searching for a fixed needle with the Two-Way
// algorithm, plus a Boyer-Moore-Horspool skip on the last byte of each window,
// following two_way_long_needle() in glibc's str-two-way.h. Construction takes
// O(needle.size()) time; each search takes worst-case
// O(haystack.size() - pos + needle.size()) time and O(1) extra memory.
//
// If `kIgnoreCase` is true, bytes are compared after `absl::ascii_tolower()`.
//
// The searcher does not store the needle: callers pass the same needle to
// `Find()` as to the constructor. This lets owners of the needle (e.g.
// `absl::ByString`) be copied and moved without fixing up pointers.
template <bool kIgnoreCase>
class TwoWaySearcher {
 public:
  // REQUIRES: `!needle.empty()`.
  explicit TwoWaySearcher(absl::string_view needle);

  // Returns the position of the first occurrence of `needle` in `haystack` at
  // or after `pos`, or `absl::string_view::npos`.
  //
  // REQUIRES: `needle` is equal to the needle passed to the constructor.
  size_t Find(absl::string_view haystack, absl::string_view needle,
              size_t pos) const;

  // Returns the smallest period p of the needle (needle[i] == needle[i + p]
  // for all valid i), if p <= needle.size() / 2. Otherwise returns 0.
  size_t short_period() const { return short_period_; }

 private:
  // The needle is split into u = needle[0, critical_pos_) and
  // v = needle[critical_pos_, m). Each window is compared on v from left to
  // right, then on u from right to left. After a mismatch in u, or a match,
  // the window moves by `shift_`.
  size_t critical_pos_;
  size_t shift_;
  // If true, `shift_` is the smallest period of the needle, and after a shift
  // the first `needle.size() - shift_` bytes of the window are known to match.
  bool periodic_;
  size_t short_period_;
  // The distance from the last occurrence of each byte value in the needle to
  // the end of the needle (or needle.size() if absent), capped at 255.
  std::array<uint8_t, 256> byte_shift_;
};

extern template class TwoWaySearcher<false>;
extern template class TwoWaySearcher<true>;

// Same as `haystack.find(needle, pos)`, for needles of more than
// `kMaxShortNeedleSize` bytes. Like libc++, finds the first byte of the needle
// with memchr(), then compares the needle (first `kProbeSize` bytes, then the
// rest). If that would exceed `NaiveSearchBudget`, switches to Two-Way.
size_t FindLongNeedle(absl::string_view haystack, absl::string_view needle,
                      size_t pos);

// Same as `haystack.find(needle, pos)`, but takes worst-case
// O((haystack.size() - pos + needle.size()) * kMaxShortNeedleSize) time.
inline size_t Find(absl::string_view haystack, absl::string_view needle,
                   size_t pos = 0) {
  if (ABSL_PREDICT_TRUE(
          NaiveSearchIsFast(needle.size(), haystack.size(), pos))) {
    return haystack.find(needle, pos);
  }
  return FindLongNeedle(haystack, needle, pos);
}

// Returns the position of the first occurrence of `needle` in `haystack` at or
// after `pos`, comparing ASCII case-insensitively, in worst-case
// O(haystack.size() - pos + needle.size()) time.
size_t FindIgnoreCaseLinear(absl::string_view haystack,
                            absl::string_view needle, size_t pos);

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STRING_SEARCH_H_
