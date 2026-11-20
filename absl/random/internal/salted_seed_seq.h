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

#ifndef ABSL_RANDOM_INTERNAL_SALTED_SEED_SEQ_H_
#define ABSL_RANDOM_INTERNAL_SALTED_SEED_SEQ_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/container/inlined_vector.h"
#include "absl/meta/type_traits.h"
#include "absl/random/internal/seed_material.h"
#include "absl/random/internal/traits.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

template <typename Iterator, bool IsPointer = std::is_pointer_v<Iterator>,
          typename = void>
struct IsIntegralIterator : std::false_type {};

template <typename Iterator>
struct IsIntegralIterator<Iterator, true, void>
    : std::is_integral<std::remove_pointer_t<Iterator>> {};

template <typename Iterator>
struct IsIntegralIterator<
    Iterator, false,
    std::void_t<typename std::iterator_traits<Iterator>::value_type>>
    : std::is_integral<typename std::iterator_traits<Iterator>::value_type> {};

// This class conforms to the C++ Standard "Seed Sequence" concept
// [rand.req.seedseq].
//
// A `SaltedSeedSeq` is meant to wrap an existing seed sequence and modify
// generated sequence by mixing with extra entropy. This entropy may be
// build-dependent or process-dependent. The implementation may change to be
// have either or both kinds of entropy. If salt is not available sequence is
// not modified.
template <typename SSeq>
class SaltedSeedSeq {
  static_assert(!std::is_rvalue_reference_v<SSeq>,
                "SaltedSeedSeq cannot be instantiated with an rvalue "
                "reference; use an owning type or an lvalue reference.");
  using raw_sequence_type = std::remove_reference_t<SSeq>;

 public:
  using inner_sequence_type = SSeq;
  using result_type = typename raw_sequence_type::result_type;

  template <typename U = SSeq,
            typename = std::enable_if_t<std::conjunction_v<
                std::negation<std::is_reference<U>>,
                std::is_default_constructible<std::remove_reference_t<U>>>>>
  SaltedSeedSeq() : seq_(std::make_unique<raw_sequence_type>()) {}

  template <
      typename Iterator, typename U = SSeq,
      typename = std::enable_if_t<std::conjunction_v<
          std::negation<std::is_reference<U>>, IsIntegralIterator<Iterator>,
          std::is_constructible<std::remove_reference_t<U>, Iterator,
                                Iterator>>>>
  SaltedSeedSeq(Iterator begin, Iterator end)
      : seq_(std::make_unique<raw_sequence_type>(begin, end)) {}

  template <typename T, typename U = SSeq,
            typename = std::enable_if_t<std::conjunction_v<
                std::negation<std::is_reference<U>>, std::is_integral<T>,
                std::is_constructible<std::remove_reference_t<U>,
                                      std::initializer_list<T>>>>>
  SaltedSeedSeq(std::initializer_list<T> il)
      : seq_(std::make_unique<raw_sequence_type>(il)) {}

  template <typename U = SSeq,
            typename = std::enable_if_t<std::is_lvalue_reference_v<U>>>
  explicit SaltedSeedSeq(raw_sequence_type& seq) : seq_(&seq) {}

  SaltedSeedSeq(const SaltedSeedSeq&) = delete;
  SaltedSeedSeq& operator=(const SaltedSeedSeq&) = delete;

  SaltedSeedSeq(SaltedSeedSeq&&) = default;
  SaltedSeedSeq& operator=(SaltedSeedSeq&&) = default;

  template <typename RandomAccessIterator, typename U = raw_sequence_type,
            typename = std::void_t<decltype(std::declval<U&>().generate(
                std::declval<uint32_t*>(), std::declval<uint32_t*>()))>>
  void generate(RandomAccessIterator begin, RandomAccessIterator end) {
    using V = typename std::iterator_traits<RandomAccessIterator>::value_type;
    using Ref = typename std::iterator_traits<RandomAccessIterator>::reference;

    // The common case is that generate is called with ContiguousIterators
    // to uint32_t arrays. Such contiguous memory regions may be optimized,
    // which we detect here.
    using TagType = std::conditional_t<
        (std::is_same_v<V, uint32_t> && std::is_same_v<Ref, uint32_t&> &&
         (std::is_pointer_v<RandomAccessIterator> ||
          std::is_same_v<RandomAccessIterator,
                         typename std::vector<uint32_t>::iterator>)),
        ContiguousAndUint32Tag, DefaultTag>;
    if (begin != end) {
      generate_impl(TagType{}, begin, end,
                    static_cast<size_t>(std::distance(begin, end)));
    }
  }

  template <typename OutIterator, typename U = raw_sequence_type,
            typename = std::void_t<decltype(std::declval<U&>().param(
                std::declval<OutIterator>()))>>
  void param(OutIterator out) const {
    seq_->param(out);
  }

  template <typename U = raw_sequence_type,
            typename = std::enable_if_t<std::is_convertible_v<
                decltype(std::declval<U&>().size()), size_t>>>
  size_t size() const {
    return seq_->size();
  }

 private:
  struct ContiguousAndUint32Tag {};
  struct DefaultTag {};

  // Generate which requires the iterators are contiguous pointers to uint32_t.
  // Fills the initial seed buffer from the underlying SSeq::generate() call,
  // then mixes in the salt material if available. When `n < min(60, 2 * s)`,
  // expands `generate()` to `min(60, 2 * s)` words first (satisfying
  // `s <= m - p(m)` for all `1 <= s <= 33` so narrow requests such as `n = 2`
  // or `n = 4` do not suffer `[rand.util.seedseq]` truncation collisions) and
  // folds both the salt and the excess words into the `n`-word output.
  template <typename Contiguous>
  void generate_impl(ContiguousAndUint32Tag, Contiguous begin, Contiguous end,
                     size_t n) {
    const auto salt = absl::random_internal::GetSaltMaterial();
    if (!salt.has_value()) {
      seq_->generate(begin, end);
      return;
    }
    constexpr size_t kMaxExpanded = 60;
    auto out = absl::Span<uint32_t>(&*begin, n);
    if (n < kMaxExpanded) {
      size_t s = 0;
      if constexpr (HasSeedSeqSize<raw_sequence_type>::value) {
        s = seq_->size();
      }
      const size_t target = (s >= kMaxExpanded / 2) ? kMaxExpanded : (2 * s);
      if (n < target) {
        uint32_t expanded[kMaxExpanded];
        std::fill_n(expanded, target, uint32_t{0});
        seq_->generate(expanded, expanded + target);
        std::copy_n(expanded, n, out.data());
        expanded[n - 1] = *salt;
        MixIntoSeedMaterial(
            absl::MakeConstSpan(expanded + (n - 1), target - n + 1), out);
        return;
      }
    }
    seq_->generate(begin, end);
    MixIntoSeedMaterial(absl::MakeConstSpan(&*salt, 1), out);
  }

  // The uncommon case for generate is that it is called with iterators over
  // some other buffer type which is assignable from a 32-bit value. In this
  // case we allocate a temporary 32-bit buffer and then copy-assign back
  // to the initial inputs.
  template <typename RandomAccessIterator>
  void generate_impl(DefaultTag, RandomAccessIterator begin,
                     RandomAccessIterator, size_t n) {
    // Allocates a seed buffer of `n` elements, generates the seed, then
    // copies the result into the `out` iterator.
    absl::InlinedVector<uint32_t, 8> data(n, 0);
    generate_impl(ContiguousAndUint32Tag{}, data.begin(), data.end(), n);
    std::copy(data.begin(), data.end(), begin);
  }

  // Because [rand.req.seedseq] is not required to be copy-constructible,
  // copy-assignable nor movable, owning instances wrap it with unique_ptr to be
  // movable, while SaltedSeedSeq<SSeq&> holds a non-owning pointer.
  std::conditional_t<std::is_lvalue_reference_v<SSeq>, raw_sequence_type*,
                     std::unique_ptr<raw_sequence_type>>
      seq_;
};

// is_salted_seed_seq indicates whether the type (ignoring cvref) is a
// SaltedSeedSeq.
template <typename T, typename = void>
struct is_salted_seed_seq : public std::false_type {};

template <typename T>
struct is_salted_seed_seq<
    T,
    std::enable_if_t<std::is_same_v<
        absl::remove_cvref_t<T>,
        SaltedSeedSeq<typename absl::remove_cvref_t<T>::inner_sequence_type>>>>
    : public std::true_type {};

// UnwrappedSeedSeq unwraps `SaltedSeedSeq<Inner>` to
// `absl::remove_cvref_t<Inner>`, or decays any other seed sequence type via
// `absl::remove_cvref_t`.
template <typename SSeq, bool = is_salted_seed_seq<SSeq>::value>
struct UnwrappedSeedSeq {
  using type = absl::remove_cvref_t<SSeq>;
};

template <typename SSeq>
struct UnwrappedSeedSeq<SSeq, true> {
  using type = absl::remove_cvref_t<
      typename absl::remove_cvref_t<SSeq>::inner_sequence_type>;
};

template <typename SSeq>
struct is_movable_owning_salted_seed_seq : std::false_type {};

template <typename Inner>
struct is_movable_owning_salted_seed_seq<SaltedSeedSeq<Inner>>
    : std::negation<std::is_reference<Inner>> {};

// MakeSaltedSeedSeq returns a salted variant of the seed sequence.
// When provided with an existing SaltedSeedSeq, preserves single salting,
// otherwise constructs a new SaltedSeedSeq which embodies the original
// non-salted seed parameters.
template <typename SSeq>
auto MakeSaltedSeedSeq(SSeq&& seq) {
  if constexpr (is_movable_owning_salted_seed_seq<SSeq>::value) {
    return SSeq(std::forward<SSeq>(seq));
  } else {
    using target_type = typename UnwrappedSeedSeq<SSeq>::type;
    using result_type = typename target_type::result_type;

    absl::InlinedVector<result_type, 8> data;
    if constexpr (HasSeedSeqSize<SSeq>::value) {
      data.reserve(seq.size());
    }
    seq.param(std::back_inserter(data));
    return SaltedSeedSeq<target_type>(data.begin(), data.end());
  }
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_SALTED_SEED_SEQ_H_
