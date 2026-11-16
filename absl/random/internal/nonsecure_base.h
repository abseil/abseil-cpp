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

#ifndef ABSL_RANDOM_INTERNAL_NONSECURE_BASE_H_
#define ABSL_RANDOM_INTERNAL_NONSECURE_BASE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/container/inlined_vector.h"
#include "absl/meta/type_traits.h"
#include "absl/random/internal/entropy_pool.h"
#include "absl/random/internal/salted_seed_seq.h"
#include "absl/random/internal/seed_material.h"
#include "absl/random/internal/traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// RandenPoolSeedSeq is a custom seed sequence type where generate() fills the
// provided buffer via the RandenPool entropy source.
class RandenPoolSeedSeq {
 public:
  using result_type = uint32_t;

  size_t size() const { return 0; }

  template <typename OutIterator>
  void param(OutIterator) const {}

  template <typename RandomAccessIterator>
  void generate(RandomAccessIterator begin, RandomAccessIterator end) {
    if (begin == end) {
      return;
    }
    const size_t n = static_cast<size_t>(std::distance(begin, end));
    if constexpr (CanFillDirectly<RandomAccessIterator>()) {
      auto* a = &(*begin);
      GetEntropyFromRandenPool(a, sizeof(*a) * n);
    } else {
      absl::InlinedVector<uint32_t, 8> data(n, 0);
      ScopedCleanse cleaner(data.data(), data.size() * sizeof(uint32_t));
      GetEntropyFromRandenPool(data.data(), sizeof(data[0]) * n);
      std::copy(std::begin(data), std::end(data), begin);
    }
  }

 private:
  // Fast path only for contiguous buffers of mutable, unqualified, non-bool
  // integral values (`V*` or `std::vector<V>::iterator`).
  template <typename Iter>
  static constexpr bool CanFillDirectly() {
    using V = typename std::iterator_traits<Iter>::value_type;
    using Ref = typename std::iterator_traits<Iter>::reference;
    if constexpr (std::is_same_v<V, absl::remove_cvref_t<V>> &&
                  std::is_integral_v<V> && !std::is_same_v<V, bool> &&
                  std::is_same_v<Ref, V&>) {
      return std::is_pointer_v<Iter> ||
             std::is_same_v<Iter, typename std::vector<V>::iterator>;
    } else {
      return false;
    }
  }
};

// Each instance of NonsecureURBGBase<URBG> will be seeded by variates produced
// by a thread-unique URBG-instance.
template <typename URBG, typename Seeder = RandenPoolSeedSeq>
class NonsecureURBGBase {
 private:
  static URBG ConstructURBG() {
    Seeder seeder;
    return URBG(seeder);
  }

  // Direct path: seeds `URBG` directly from an already-salted sequence, or
  // from a stack-scoped non-owning `SaltedSeedSeq<Raw&>` without copying.
  template <
      typename SSeq, typename Raw = std::remove_reference_t<SSeq>,
      typename Direct =
          std::conditional_t<IsSaltedSeedSeq<absl::remove_cvref_t<Raw>>::value,
                             Raw, SaltedSeedSeq<Raw&>>,
      typename = std::enable_if_t<std::conjunction_v<
          HasSeedSeqGenerate<SSeq>, std::is_constructible<URBG, Direct&>>>>
  static URBG ConstructURBG(SSeq&& seq, int) {  // NOLINT(runtime/references)
    if constexpr (IsSaltedSeedSeq<absl::remove_cvref_t<Raw>>::value) {
      return URBG(seq);
    } else {
      SaltedSeedSeq<Raw&> salted_seq(seq);
      return URBG(salted_seq);
    }
  }

  // Fallback path for `const` seed sequences whose `generate()` is non-const:
  // clones parameters via `MakeSaltedSeedSeq(seq)` into a mutable wrapper.
  template <typename SSeq,
            typename Salted = decltype(random_internal::MakeSaltedSeedSeq(
                std::declval<SSeq&>())),
            typename = std::enable_if_t<std::is_constructible_v<URBG, Salted&>>>
  static URBG ConstructURBG(SSeq&& seq, char) {  // NOLINT(runtime/references)
    auto salted_seq = random_internal::MakeSaltedSeedSeq(seq);
    return URBG(salted_seq);
  }

 public:
  using result_type = typename URBG::result_type;

  // Default constructor
  NonsecureURBGBase() : urbg_(ConstructURBG()) {}

  // Copy disallowed, move allowed.
  NonsecureURBGBase(const NonsecureURBGBase&) = delete;
  NonsecureURBGBase& operator=(const NonsecureURBGBase&) = delete;
  NonsecureURBGBase(NonsecureURBGBase&&) = default;
  NonsecureURBGBase& operator=(NonsecureURBGBase&&) = default;

  // Constructor using a seed
  template <
      class SSeq,
      typename = std::enable_if_t<
          !std::is_base_of_v<NonsecureURBGBase, absl::remove_cvref_t<SSeq>> &&
          !std::is_convertible_v<SSeq, result_type>>,
      typename = decltype(ConstructURBG(std::declval<SSeq>(), 0))>
  explicit NonsecureURBGBase(SSeq&& seq)
      : urbg_(ConstructURBG(std::forward<SSeq>(seq), 0)) {}

  // Note: on MSVC, min() or max() can be interpreted as MIN() or MAX(), so we
  // enclose min() or max() in parens as (min)() and (max)().
  // Additionally, clang-format requires no space before this construction.

  // NonsecureURBGBase::min()
  static constexpr result_type(min)() { return (URBG::min)(); }

  // NonsecureURBGBase::max()
  static constexpr result_type(max)() { return (URBG::max)(); }

  // NonsecureURBGBase::operator()()
  result_type operator()() { return urbg_(); }

  // NonsecureURBGBase::discard()
  void discard(unsigned long long values) {  // NOLINT(runtime/int)
    urbg_.discard(values);
  }

  bool operator==(const NonsecureURBGBase& other) const {
    return urbg_ == other.urbg_;
  }

  bool operator!=(const NonsecureURBGBase& other) const {
    return !(urbg_ == other.urbg_);
  }

 private:
  URBG urbg_;
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_NONSECURE_BASE_H_
