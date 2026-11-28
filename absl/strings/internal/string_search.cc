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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "absl/base/config.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {
namespace {

constexpr size_t kNpos = absl::string_view::npos;

template <bool kIgnoreCase>
inline unsigned char Fold(unsigned char c) {
  if constexpr (kIgnoreCase) {
    return static_cast<unsigned char>(absl::ascii_tolower(c));
  } else {
    return c;
  }
}

// Computes the maximal suffix of `needle` under the byte order given by
// `less` (applied to folded bytes). Returns the index of the byte before the
// start of that suffix (in [-1, size - 1)), and sets `*period` to the period of
// the suffix.
template <bool kIgnoreCase, typename Less>
ptrdiff_t MaximalSuffix(absl::string_view needle, Less less,
                        ptrdiff_t* period) {
  const ptrdiff_t m = static_cast<ptrdiff_t>(needle.size());
  ptrdiff_t ms = -1;  // Index before the start of the current maximal suffix.
  ptrdiff_t j = 0;    // Start of the candidate suffix being compared, minus 1.
  ptrdiff_t k = 1;    // Offset within the current comparison.
  ptrdiff_t p = 1;    // Period of the current maximal suffix.
  while (j + k < m) {
    const unsigned char a =
        Fold<kIgnoreCase>(static_cast<unsigned char>(needle[j + k]));
    const unsigned char b =
        Fold<kIgnoreCase>(static_cast<unsigned char>(needle[ms + k]));
    if (less(a, b)) {
      // The candidate suffix is smaller; skip past it.
      j += k;
      k = 1;
      p = j - ms;
    } else if (a == b) {
      if (k != p) {
        ++k;
      } else {
        j += p;
        k = 1;
      }
    } else {
      // The candidate suffix is larger; it becomes the maximal suffix.
      ms = j;
      j = ms + 1;
      k = 1;
      p = 1;
    }
  }
  *period = p;
  return ms;
}

template <bool kIgnoreCase>
bool FoldedEqual(const char* a, const char* b, size_t n) {
  if constexpr (kIgnoreCase) {
    for (size_t i = 0; i < n; ++i) {
      if (Fold<true>(static_cast<unsigned char>(a[i])) !=
          Fold<true>(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
  } else {
    return std::memcmp(a, b, n) == 0;
  }
}

}  // namespace

template <bool kIgnoreCase>
TwoWaySearcher<kIgnoreCase>::TwoWaySearcher(absl::string_view needle) {
  assert(!needle.empty());
  const size_t m = needle.size();
  // Critical factorization: the larger of the two maximal suffixes (under
  // opposite orders) starts at a critical position.
  ptrdiff_t p, q;
  const ptrdiff_t i1 = MaximalSuffix<kIgnoreCase>(
      needle, [](unsigned char a, unsigned char b) { return a < b; }, &p);
  const ptrdiff_t i2 = MaximalSuffix<kIgnoreCase>(
      needle, [](unsigned char a, unsigned char b) { return a > b; }, &q);
  critical_pos_ = static_cast<size_t>((std::max)(i1, i2) + 1);
  const size_t period = static_cast<size_t>(i1 > i2 ? p : q);
  // `period` is a period of needle[critical_pos_, m), so
  // period + critical_pos_ <= m.
  if (FoldedEqual<kIgnoreCase>(needle.data(), needle.data() + period,
                               critical_pos_)) {
    // The whole needle has period `period`. This is its smallest period,
    // since any period of the needle is a period of the suffix.
    periodic_ = true;
    shift_ = period;
    short_period_ = period <= m / 2 ? period : 0;
  } else {
    // The smallest period of the needle is larger than
    // max(critical_pos_, m - critical_pos_). Use a shift that is safe for both
    // parts.
    periodic_ = false;
    shift_ = (std::max)(critical_pos_, m - critical_pos_) + 1;
    short_period_ = 0;
  }
  // A smaller shift is always safe, so shifts are capped to fit in uint8_t.
  constexpr size_t kMaxByteShift = (std::numeric_limits<uint8_t>::max)();
  byte_shift_.fill(static_cast<uint8_t>((std::min)(m, kMaxByteShift)));
  for (size_t i = 0; i < m; ++i) {
    const uint8_t s =
        static_cast<uint8_t>((std::min)(m - i - 1, kMaxByteShift));
    const unsigned char c = static_cast<unsigned char>(needle[i]);
    if constexpr (kIgnoreCase) {
      byte_shift_[static_cast<unsigned char>(absl::ascii_tolower(c))] = s;
      byte_shift_[static_cast<unsigned char>(absl::ascii_toupper(c))] = s;
    } else {
      byte_shift_[c] = s;
    }
  }
}

// Before each Two-Way step, the last byte of the window is checked against
// `byte_shift_` (as in Boyer-Moore-Horspool), which skips most windows in
// typical inputs without affecting the linear worst case.
template <bool kIgnoreCase>
size_t TwoWaySearcher<kIgnoreCase>::Find(absl::string_view haystack,
                                         absl::string_view needle,
                                         size_t pos) const {
  const size_t m = needle.size();
  if (pos > haystack.size() || m > haystack.size() - pos) {
    return kNpos;
  }
  const unsigned char* const x =
      reinterpret_cast<const unsigned char*>(needle.data());
  // Search in haystack[pos, end), and add `pos` to the result.
  const unsigned char* const y =
      reinterpret_cast<const unsigned char*>(haystack.data()) + pos;
  const size_t last_window = haystack.size() - pos - m;
  const size_t c = critical_pos_;
  auto eq = [](unsigned char a, unsigned char b) {
    return Fold<kIgnoreCase>(a) == Fold<kIgnoreCase>(b);
  };
  if (periodic_) {
    // After a match of the right part, the first `memory` bytes of the next
    // window are known to match.
    size_t memory = 0;
    for (size_t j = 0; j <= last_window;) {
      size_t skip = byte_shift_[y[j + m - 1]];
      if (skip > 0) {
        if (memory > 0 && skip < shift_) {
          // The needle is periodic, but the last period of the window has a
          // byte out of place, so there can be no match until after it.
          skip = m - shift_;
        }
        memory = 0;
        j += skip;
        continue;
      }
      // The last byte is known to match.
      size_t i = (std::max)(c, memory);
      while (i < m - 1 && eq(x[i], y[i + j])) ++i;
      if (i < m - 1) {
        j += i - c + 1;
        memory = 0;
        continue;
      }
      i = c;
      while (i > memory && eq(x[i - 1], y[i - 1 + j])) --i;
      if (i <= memory) return pos + j;
      j += shift_;
      memory = m - shift_;
    }
  } else {
    for (size_t j = 0; j <= last_window;) {
      const size_t skip = byte_shift_[y[j + m - 1]];
      if (skip > 0) {
        j += skip;
        continue;
      }
      // The last byte is known to match.
      size_t i = c;
      while (i < m - 1 && eq(x[i], y[i + j])) ++i;
      if (i < m - 1) {
        j += i - c + 1;
        continue;
      }
      i = c;
      while (i > 0 && eq(x[i - 1], y[i - 1 + j])) --i;
      if (i == 0) return pos + j;
      j += shift_;
    }
  }
  return kNpos;
}

template class TwoWaySearcher<false>;
template class TwoWaySearcher<true>;

size_t FindLongNeedle(absl::string_view haystack, absl::string_view needle,
                      size_t pos) {
  const size_t n = haystack.size();
  const size_t m = needle.size();
  if (pos > n || m > n - pos) return kNpos;
  // Same as libc++'s std::__search_substring(): find the first byte of the
  // needle with memchr(), then compare the whole needle with memcmp().
  const char* const begin = haystack.data();
  const char* const last = begin + (n - m);  // The last candidate.
  const char first = needle[0];
  NaiveSearchBudget budget(m, pos);
  const char* p = begin + pos;
  while (true) {
    p = static_cast<const char*>(
        std::memchr(p, first, static_cast<size_t>(last - p) + 1));
    if (p == nullptr) return kNpos;
    // Most candidates differ within the first `kProbeSize` bytes, which is an
    // inlined comparison, and costs little budget. (m > kProbeSize.)
    size_t cost = kProbeSize;
    if (std::memcmp(p, needle.data(), kProbeSize) == 0) {
      if (std::memcmp(p + kProbeSize, needle.data() + kProbeSize,
                      m - kProbeSize) == 0) {
        return static_cast<size_t>(p - begin);
      }
      cost = m;
    }
    const size_t at = static_cast<size_t>(p - begin);
    if (ABSL_PREDICT_FALSE(!budget.Spend(at, cost))) {
      return TwoWaySearcher<false>(needle).Find(haystack, needle, at + 1);
    }
    if (p == last) return kNpos;
    ++p;
  }
}

size_t FindIgnoreCaseLinear(absl::string_view haystack,
                            absl::string_view needle, size_t pos) {
  if (needle.empty()) return pos <= haystack.size() ? pos : kNpos;
  return TwoWaySearcher<true>(needle).Find(haystack, needle, pos);
}

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl
