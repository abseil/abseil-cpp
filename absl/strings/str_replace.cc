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

#include "absl/strings/str_replace.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/strings/internal/string_search.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

using FixedMapping =
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>;

namespace {

// Returns the position of the first occurrence of `sub.old` in `s` at or after
// `pos`, for long needles, given that its previous occurrence is at
// `sub.offset < pos`.
//
// The total time of the calls for one substitution is linear in `s.size()`,
// even though, when another substitution was applied over `sub.offset`, `pos`
// may be within the previous occurrence, so that successive searches overlap.
// In that case, long needles use the needle's period to find the following
// occurrences, as in Two-Way.
size_t FindNextOccurrenceSlow(
    absl::string_view s, ViableSubstitution& sub, size_t pos,
    std::vector<std::unique_ptr<const TwoWaySearcher<false>>>& searchers) {
  const size_t m = sub.old.size();
  if (sub.searcher == nullptr) {
    if (pos >= sub.offset + m) {
      // This search does not overlap the previous one.
      return FindLongNeedle(s, sub.old, pos);
    }
    searchers.push_back(std::make_unique<const TwoWaySearcher<false>>(sub.old));
    sub.searcher = searchers.back().get();
  }
  const size_t p = sub.searcher->short_period();
  if (p > 0) {
    // Any occurrence at last < j <= last + m - p would imply a period j - last
    // that is a multiple of p, and so an occurrence at last + p. An occurrence
    // at last + p only needs its last p bytes to be compared.
    size_t last = sub.offset;
    while (true) {
      if (last + m + p > s.size() ||
          s.substr(last + m, p) != sub.old.substr(m - p)) {
        pos = (std::max)(pos, last + m - p + 1);
        break;
      }
      last += p;
      if (last >= pos) return last;
    }
  }
  // Otherwise successive occurrences are more than m / 2 bytes apart.
  return sub.searcher->Find(s, sub.old, pos);
}

}  // namespace

// Applies the ViableSubstitutions in subs_ptr to the absl::string_view s, and
// stores the result in *result_ptr. Returns the number of substitutions that
// occurred.
int ApplySubstitutions(
    absl::string_view s,
    std::vector<strings_internal::ViableSubstitution>* absl_nonnull subs_ptr,
    std::string* absl_nonnull result_ptr) {
  auto& subs = *subs_ptr;
  // Owns the ViableSubstitution::searcher objects.
  std::vector<std::unique_ptr<const TwoWaySearcher<false>>> searchers;
  int substitutions = 0;
  size_t pos = 0;
  while (!subs.empty()) {
    auto& sub = subs.back();
    if (sub.offset >= pos) {
      if (pos <= s.size()) {
        StrAppend(result_ptr, s.substr(pos, sub.offset - pos), sub.replacement);
      }
      pos = sub.offset + sub.old.size();
      substitutions += 1;
    }
    sub.offset =
        ABSL_PREDICT_TRUE(NaiveSearchIsFast(sub.old.size(), s.size(), pos))
            ? s.find(sub.old, pos)
            : FindNextOccurrenceSlow(s, sub, pos, searchers);
    if (sub.offset == s.npos) {
      subs.pop_back();
    } else {
      // Insertion sort to ensure the last ViableSubstitution continues to be
      // before all the others.
      size_t index = subs.size();
      while (--index && subs[index - 1].OccursBefore(subs[index])) {
        std::swap(subs[index], subs[index - 1]);
      }
    }
  }
  result_ptr->append(s.data() + pos, s.size() - pos);
  return substitutions;
}

}  // namespace strings_internal

// We can implement this in terms of the generic StrReplaceAll, but
// we must specify the template overload because C++ cannot deduce the type
// of an initializer_list parameter to a function, and also if we don't specify
// the type, we just call ourselves.
//
// Note that we implement them here, rather than in the header, so that they
// aren't inlined.

std::string StrReplaceAll(absl::string_view s,
                          strings_internal::FixedMapping replacements) {
  return StrReplaceAll<strings_internal::FixedMapping>(s, replacements);
}

int StrReplaceAll(strings_internal::FixedMapping replacements,
                  std::string* absl_nonnull target) {
  return StrReplaceAll<strings_internal::FixedMapping>(replacements, target);
}

ABSL_NAMESPACE_END
}  // namespace absl
