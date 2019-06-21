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

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/meta/type_traits.h"
#include "absl/random/internal/seed_material.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace absl {
namespace random_internal {

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
 public:
  using inner_sequence_type = SSeq;
  using result_type = typename SSeq::result_type;

  SaltedSeedSeq() : seq_(absl::make_unique<SSeq>()) {}

  template <typename Iterator>
  SaltedSeedSeq(Iterator begin, Iterator end)
      : seq_(absl::make_unique<SSeq>(begin, end)) {}

  template <typename T>
  SaltedSeedSeq(std::initializer_list<T> il)
      : SaltedSeedSeq(il.begin(), il.end()) {}

  SaltedSeedSeq(const SaltedSeedSeq& other) = delete;
  SaltedSeedSeq& operator=(const SaltedSeedSeq& other) = delete;

  SaltedSeedSeq(SaltedSeedSeq&& other) = default;
  SaltedSeedSeq& operator=(SaltedSeedSeq&& other) = default;

  template <typename RandomAccessIterator>
  void generate(RandomAccessIterator begin, RandomAccessIterator end) {
    if (begin != end) {
      generate_impl(
          std::integral_constant<bool, sizeof(*begin) == sizeof(uint32_t)>{},
          begin, end);
    }
  }

  template <typename OutIterator>
  void param(OutIterator out) const {
    seq_->param(out);
  }

  size_t size() const { return seq_->size(); }

 private:
  // The common case for generate is that it is called with iterators over a
  // 32-bit value buffer. These can be reinterpreted to a uint32_t and we can
  // operate on them as such.
  template <typename RandomAccessIterator>
  void generate_impl(std::integral_constant<bool, true> /*is_32bit*/,
                     RandomAccessIterator begin, RandomAccessIterator end) {
    seq_->generate(begin, end);
    const uint32_t salt = absl::random_internal::GetSaltMaterial().value_or(0);
    auto buffer = absl::MakeSpan(begin, end);
    MixIntoSeedMaterial(
        absl::MakeConstSpan(&salt, 1),
        absl::MakeSpan(reinterpret_cast<uint32_t*>(buffer.data()),
                       buffer.size()));
  }

  // The uncommon case for generate is that it is called with iterators over
  // some other buffer type which is assignable from a 32-bit value. In this
  // case we allocate a temporary 32-bit buffer and then copy-assign back
  // to the initial inputs.
  template <typename RandomAccessIterator>
  void generate_impl(std::integral_constant<bool, false> /*is_32bit*/,
                     RandomAccessIterator begin, RandomAccessIterator end) {
    // Allocate a temporary buffer, seed, and then copy.
    absl::InlinedVector<uint32_t, 8> data(std::distance(begin, end), 0);
    generate_impl(std::integral_constant<bool, true>{}, data.begin(),
                  data.end());
    std::copy(data.begin(), data.end(), begin);
  }

  // Because [rand.req.seedseq] is not copy-constructible, copy-assignable nor
  // movable so we wrap it with unique pointer to be able to move SaltedSeedSeq.
  std::unique_ptr<SSeq> seq_;
};

// is_salted_seed_seq indicates whether the type is a SaltedSeedSeq.
template <typename T, typename = void>
struct is_salted_seed_seq : public std::false_type {};

template <typename T>
struct is_salted_seed_seq<
    T, typename std::enable_if<std::is_same<
           T, SaltedSeedSeq<typename T::inner_sequence_type>>::value>::type>
    : public std::true_type {};

// MakeSaltedSeedSeq returns a salted variant of the seed sequence.
// When provided with an existing SaltedSeedSeq, returns the input parameter,
// otherwise constructs a new SaltedSeedSeq which embodies the original
// non-salted seed parameters.
template <
    typename SSeq,  //
    typename EnableIf = absl::enable_if_t<is_salted_seed_seq<SSeq>::value>>
SSeq MakeSaltedSeedSeq(SSeq&& seq) {
  return SSeq(std::forward<SSeq>(seq));
}

template <
    typename SSeq,  //
    typename EnableIf = absl::enable_if_t<!is_salted_seed_seq<SSeq>::value>>
SaltedSeedSeq<typename std::decay<SSeq>::type> MakeSaltedSeedSeq(SSeq&& seq) {
  using sseq_type = typename std::decay<SSeq>::type;
  using result_type = typename sseq_type::result_type;

  absl::InlinedVector<result_type, 8> data;
  seq.param(std::back_inserter(data));
  return SaltedSeedSeq<sseq_type>(data.begin(), data.end());
}

}  // namespace random_internal
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_SALTED_SEED_SEQ_H_
