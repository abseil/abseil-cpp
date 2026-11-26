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
#include <random>
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

  template <typename U = SSeq,
            typename = std::enable_if_t<std::is_lvalue_reference_v<U>>>
  explicit SaltedSeedSeq(const raw_sequence_type&&) = delete;

  SaltedSeedSeq(const SaltedSeedSeq&) = delete;
  SaltedSeedSeq& operator=(const SaltedSeedSeq&) = delete;

  SaltedSeedSeq(SaltedSeedSeq&&) = default;
  SaltedSeedSeq& operator=(SaltedSeedSeq&&) = default;

  template <typename RandomAccessIterator, typename U = raw_sequence_type,
            typename = std::void_t<decltype(std::declval<U&>().generate(
                std::declval<uint32_t*>(), std::declval<uint32_t*>()))>>
  void generate(RandomAccessIterator begin, RandomAccessIterator end) {
    if (begin == end) {
      return;
    }
    const size_t n = static_cast<size_t>(std::distance(begin, end));
    using V = typename std::iterator_traits<RandomAccessIterator>::value_type;
    using Ref = typename std::iterator_traits<RandomAccessIterator>::reference;
    if constexpr (std::is_same_v<V, uint32_t> &&
                  std::is_same_v<Ref, uint32_t&> &&
                  (std::is_pointer_v<RandomAccessIterator> ||
                   std::is_same_v<RandomAccessIterator,
                                  typename std::vector<uint32_t>::iterator>)) {
      generate_contiguous(&*begin, n);
    } else {
      absl::InlinedVector<uint32_t, 8> data(n, 0);
      ScopedCleanse cleaner(absl::MakeSpan(data));
      generate_contiguous(data.data(), n);
      std::copy(data.begin(), data.end(), begin);
    }
  }

  template <typename OutIterator, typename U = raw_sequence_type,
            typename = std::void_t<decltype(std::declval<const U&>().param(
                std::declval<OutIterator>()))>>
  void param(OutIterator out) const {
    std::as_const(*seq_).param(out);
  }

  template <typename U = raw_sequence_type,
            typename = std::enable_if_t<std::is_convertible_v<
                decltype(std::declval<const U&>().size()), size_t>>>
  size_t size() const {
    return std::as_const(*seq_).size();
  }

  template <typename U = raw_sequence_type,
            typename = std::enable_if_t<!std::is_const_v<U>>,
            typename = std::enable_if_t<std::is_convertible_v<
                decltype(std::declval<U&>().size()), size_t>>>
  size_t size() {
    return seq_->size();
  }

 private:
  // Generates `n` salted `uint32_t` values into `out_ptr`.
  // For `std::seed_seq`, bypasses `[rand.util.seedseq]`'s narrow-output
  // collisions by reading raw parameters via `param()` and applying either a
  // 128-bit salt-keyed bijection (`s <= n`, strictly injective up to `n` words
  // with 0 collisions) or a keyed SipHash-2-4-128 XOF (`s > n`). For custom
  // `SeedSequence` types, invokes `seq_->generate()` once with the exact
  // requested count `n` and applies the keyed bijection in place.
  void generate_contiguous(uint32_t* out_ptr, size_t n) {
    const auto salt = absl::random_internal::GetSaltMaterial();
    if (!salt.has_value()) {
      seq_->generate(out_ptr, out_ptr + n);
      return;
    }
    auto out = absl::Span<uint32_t>(out_ptr, n);
    if constexpr (std::is_same_v<absl::remove_cvref_t<raw_sequence_type>,
                                 std::seed_seq>) {
      absl::InlinedVector<uint32_t, 64> raw(seq_->size());
      ScopedCleanse cleaner(absl::MakeSpan(raw));
      seq_->param(raw.begin());
      if (raw.size() <= n) {
        std::copy(raw.begin(), raw.end(), out.data());
        std::fill(out.data() + raw.size(), out.data() + n, uint32_t{0});
        PermuteSeedMaterialWithSalt(*salt, /*domain_tag=*/1u,
                                    static_cast<uint64_t>(raw.size()), out);
      } else {
        DeriveSeedMaterialWithSalt(*salt, /*domain_tag=*/2u, raw, out);
      }
    } else {
      seq_->generate(out_ptr, out_ptr + n);
      PermuteSeedMaterialWithSalt(*salt, /*domain_tag=*/3u,
                                  static_cast<uint64_t>(n), out);
    }
  }

  // Because [rand.req.seedseq] is not required to be copy-constructible,
  // copy-assignable nor movable, owning instances wrap it with unique_ptr to be
  // movable, while SaltedSeedSeq<SSeq&> holds a non-owning pointer.
  std::conditional_t<std::is_lvalue_reference_v<SSeq>, raw_sequence_type*,
                     std::unique_ptr<raw_sequence_type>>
      seq_;
};

template <typename SSeq>
struct IsSaltedSeedSeq<SaltedSeedSeq<SSeq>> : std::true_type {};

// UnwrappedSeedSeq unwraps `SaltedSeedSeq<Inner>` to
// `absl::remove_cvref_t<Inner>`, or decays any other seed sequence type via
// `absl::remove_cvref_t`.
template <typename SSeq,
          bool = IsSaltedSeedSeq<absl::remove_cvref_t<SSeq>>::value>
struct UnwrappedSeedSeq {
  using type = absl::remove_cvref_t<SSeq>;
};

template <typename SSeq>
struct UnwrappedSeedSeq<SSeq, true> {
  using type = absl::remove_cvref_t<
      typename absl::remove_cvref_t<SSeq>::inner_sequence_type>;
};

// MakeSaltedSeedSeq returns a salted variant of the seed sequence.
// When provided with an existing SaltedSeedSeq, preserves single salting,
// otherwise constructs a new SaltedSeedSeq which embodies the original
// non-salted seed parameters.
template <typename Inner, typename = std::enable_if_t<std::conjunction_v<
                              std::is_same<Inner, absl::remove_cvref_t<Inner>>,
                              HasSeedSeqGenerate<Inner>>>>
SaltedSeedSeq<Inner> MakeSaltedSeedSeq(SaltedSeedSeq<Inner>&& seq) {
  return std::move(seq);
}

template <typename SSeq,
          typename Target = typename UnwrappedSeedSeq<SSeq>::type,
          typename = std::enable_if_t<HasSeedSeqGenerate<Target>::value>,
          typename Result = typename Target::result_type,
          typename = std::enable_if_t<std::is_constructible_v<
              SaltedSeedSeq<Target>,
              typename absl::InlinedVector<Result, 8>::iterator,
              typename absl::InlinedVector<Result, 8>::iterator>>,
          typename = std::void_t<
              decltype(std::declval<std::remove_reference_t<SSeq>&>().param(
                  std::back_inserter(
                      std::declval<absl::InlinedVector<Result, 8>&>())))>>
SaltedSeedSeq<Target> MakeSaltedSeedSeq(SSeq&& seq) {
  absl::InlinedVector<Result, 8> data;
  struct CleanseOnExit {
    absl::InlinedVector<Result, 8>& v;
    ~CleanseOnExit() {
      CleanseSeedMaterial(v.data(), v.size() * sizeof(Result));
    }
  } cleaner{data};
  if constexpr (HasSeedSeqSize<SSeq>::value) {
    data.reserve(seq.size());
  }
  seq.param(std::back_inserter(data));
  return SaltedSeedSeq<Target>(data.begin(), data.end());
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_SALTED_SEED_SEQ_H_
