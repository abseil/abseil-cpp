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

#ifndef ABSL_RANDOM_INTERNAL_RANDEN_ENGINE_H_
#define ABSL_RANDOM_INTERNAL_RANDEN_ENGINE_H_

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <istream>
#include <iterator>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/meta/type_traits.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/randen.h"
#include "absl/random/internal/traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// Deterministic pseudorandom byte generator with backtracking resistance
// (leaking the state does not compromise prior outputs). Based on Reverie
// (see "A Robust and Sponge-Like PRNG with Improved Efficiency") instantiated
// with an improved Simpira-like permutation.
// Returns values of type "T" (must be a built-in unsigned integer type).
//
// RANDen = RANDom generator or beetroots in Swiss High German.
// 'Strong' (well-distributed, unpredictable, backtracking-resistant) random
// generator, faster in some benchmarks than std::mt19937_64 and pcg64_c32.
template <typename T>
class alignas(8) randen_engine {
  template <class SeedSequence>
  using EnableIfSeedSeq = std::conjunction<
      std::negation<
          std::is_base_of<randen_engine, absl::remove_cvref_t<SeedSequence>>>,
      std::negation<std::is_convertible<SeedSequence, T>>,
      HasSeedSeqGenerate<SeedSequence>, HasSeedSeqSize<SeedSequence>>;

 public:
  // C++11 URBG interface:
  using result_type = T;
  static_assert(std::is_unsigned_v<result_type>,
                "randen_engine template argument must be a built-in unsigned "
                "integer type");

  static constexpr result_type(min)() {
    return (std::numeric_limits<result_type>::min)();
  }

  static constexpr result_type(max)() {
    return (std::numeric_limits<result_type>::max)();
  }

  randen_engine() : randen_engine(result_type{0}) {}
  explicit randen_engine(result_type seed_value) { seed(seed_value); }

  template <class SeedSequence,
            typename = std::enable_if_t<EnableIfSeedSeq<SeedSequence>::value>>
  explicit randen_engine(SeedSequence&& seq) {
    seed(seq);
  }

  // alignment requirements dictate custom copy and move constructors.
  randen_engine(const randen_engine& other)
      : next_(other.next_), impl_(other.impl_) {
    std::memcpy(state(), other.state(), kStateSizeT * sizeof(result_type));
  }
  randen_engine& operator=(const randen_engine& other) {
    if (this == &other) {
      return *this;
    }
    next_ = other.next_;
    impl_ = other.impl_;
    std::memcpy(state(), other.state(), kStateSizeT * sizeof(result_type));
    return *this;
  }

  // Returns random bits from the buffer in units of result_type.
  result_type operator()() {
    // Refill the buffer if needed (unlikely).
    auto* begin = state();
    if (next_ >= kStateSizeT) {
      next_ = kCapacityT;
      impl_.Generate(begin);
    }
    return little_endian::ToHost(begin[next_++]);
  }

  template <class SeedSequence,
            typename = std::enable_if_t<EnableIfSeedSeq<SeedSequence>::value>>
  void seed(SeedSequence&& seq) {
    // Zeroes the state and absorbs seed material.
    AbsorbSeed</*kMixExistingState=*/false>(seq);
  }

  void seed(result_type seed_value = result_type{0}) {
    next_ = kStateSizeT;
    // Zeroes the inner state and fills the outer state with seed_value to
    // mimic the behaviour of reseed
    auto* begin = state();
    std::fill(begin, begin + kCapacityT, result_type{0});
    std::fill(begin + kCapacityT, begin + kStateSizeT,
              little_endian::FromHost(seed_value));
  }

  // Inserts entropy into (part of) the state. Calling this periodically with
  // sufficient entropy ensures prediction resistance (attackers cannot predict
  // future outputs even if state is compromised).
  template <class SeedSequence,
            typename = std::enable_if_t<EnableIfSeedSeq<SeedSequence&>::value>>
  void reseed(SeedSequence& seq) {
    AbsorbSeed</*kMixExistingState=*/true>(seq);
  }

  void discard(uint64_t count) {
    uint64_t step =
        (std::min)(static_cast<uint64_t>(kStateSizeT - next_), count);
    count -= step;

    constexpr uint64_t kRateT = kStateSizeT - kCapacityT;
    auto* begin = state();
    while (count > 0) {
      next_ = kCapacityT;
      impl_.Generate(begin);
      step = (std::min)(kRateT, count);
      count -= step;
    }
    next_ += step;
  }

  bool operator==(const randen_engine& other) const {
    const auto* begin = state();
    return next_ == other.next_ &&
           std::equal(begin, begin + kStateSizeT, other.state());
  }

  bool operator!=(const randen_engine& other) const {
    return !(*this == other);
  }

  template <class CharT, class Traits>
  friend std::basic_ostream<CharT, Traits>& operator<<(
      std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
      const randen_engine<T>& engine) {       // NOLINT(runtime/references)
    using numeric_type =
        typename random_internal::stream_format_type<result_type>::type;
    auto saver = random_internal::make_ostream_state_saver(os);
    auto* it = engine.state();
    for (auto* end = it + kStateSizeT; it < end; ++it) {
      // In the case that `elem` is `uint8_t`, it must be cast to something
      // larger so that it prints as an integer rather than a character. For
      // simplicity, apply the cast all circumstances.
      os << static_cast<numeric_type>(little_endian::FromHost(*it))
         << os.fill();
    }
    os << engine.next_;
    return os;
  }

  template <class CharT, class Traits>
  friend std::basic_istream<CharT, Traits>& operator>>(
      std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
      randen_engine<T>& engine) {             // NOLINT(runtime/references)
    auto saver = random_internal::make_istream_state_saver(is);
    result_type state[kStateSizeT];
    for (auto& elem : state) {
      // It is not possible to read uint8_t from a stream, so it is necessary to
      // read a wider type and then cast it to uint8_t.
      result_type value = 0;
      if (!random_internal::read_integral(is, value)) {
        return is;
      }
      elem = little_endian::ToHost(value);
    }
    size_t next = 0;
    if (!random_internal::read_integral(is, next)) {
      return is;
    }
    if (next < kCapacityT || next > kStateSizeT) {
      is.setstate(std::ios_base::failbit);
      return is;
    }
    std::memcpy(engine.state(), state, sizeof(state));
    engine.next_ = next;
    return is;
  }

 private:
  static constexpr size_t kStateSizeT =
      Randen::kStateBytes / sizeof(result_type);
  static constexpr size_t kCapacityT =
      Randen::kCapacityBytes / sizeof(result_type);

  static void XorLane128(unsigned char* dst, const uint32_t* src) noexcept {
    for (size_t i = 0; i < 16; i += sizeof(uint64_t)) {
      uint64_t s = 0;
      uint64_t d = 0;
      std::memcpy(&s, reinterpret_cast<const unsigned char*>(src) + i,
                  sizeof(s));
      std::memcpy(&d, dst + i, sizeof(d));
      d ^= s;
      std::memcpy(dst + i, &d, sizeof(d));
    }
  }

  template <bool kMixExistingState>
  void PrepareStateForSeed(result_type* begin) {
    if constexpr (kMixExistingState) {
      // Permute the existing sponge state before absorbing new seed material so
      // repeated reseed(s) calls cannot XOR-cancel and mid-buffer reseeds do
      // not XOR directly onto already-emitted rate words. Also fold a nonzero
      // domain tag and the current byte offset into the inner capacity (which
      // Absorb() leaves untouched) to separate reseed() from ordinary block
      // refills and distinguish reseeds at different mid-buffer positions.
      static_assert(Randen::kCapacityBytes >= 4,
                    "Randen capacity must hold domain separator bytes");
      const size_t byte_offset = next_ * sizeof(result_type);
      impl_.Generate(begin);
      auto* capacity = reinterpret_cast<unsigned char*>(begin);
      capacity[0] ^= 0x01;
      capacity[1] ^= static_cast<unsigned char>(byte_offset & 0xff);
      capacity[2] ^= static_cast<unsigned char>((byte_offset >> 8) & 0xff);
    } else {
      seed();
    }
  }

  template <bool kMixExistingState, class SeedSequence>
  void AbsorbSeed(SeedSequence& seq) {
    if constexpr (IsDirectEntropySeedSeq<
                      absl::remove_cvref_t<SeedSequence>>::value) {
      // Direct entropy sources (such as RandenPoolSeedSeq) yield full-entropy
      // words without needing full-rate [rand.req.seedseq] mixing. Draw 8 words
      // (256 bits) rather than all 60 rate words (which would drain an entire
      // RandenPool refill per BitGen construction) and place them directly into
      // two even-numbered 128-bit sponge lanes (state[2] and state[4]), leaving
      // the remaining 52 rate words zeroed (on seed) or unperturbed (on
      // reseed).
      constexpr size_t kDirectEntropyWords = 8;
      constexpr size_t kLane0Offset = Randen::kCapacityBytes + 16;
      constexpr size_t kLane1Offset = Randen::kCapacityBytes + 48;
      static_assert(kLane1Offset + 16 <= Randen::kStateBytes,
                    "Randen state must contain even 128-bit lanes 2 and 4");
      alignas(16) uint32_t entropy[kDirectEntropyWords];
      seq.generate(entropy, entropy + kDirectEntropyWords);
#ifdef ABSL_IS_BIG_ENDIAN
      for (uint32_t& e : entropy) {
        e = absl::little_endian::FromHost(e);
      }
#endif
      auto* begin = state();
      PrepareStateForSeed<kMixExistingState>(begin);
      auto* state_bytes = reinterpret_cast<unsigned char*>(begin);
      if constexpr (kMixExistingState) {
        XorLane128(state_bytes + kLane0Offset, entropy);
        XorLane128(state_bytes + kLane1Offset, entropy + 4);
      } else {
        std::memcpy(state_bytes + kLane0Offset, entropy, 16);
        std::memcpy(state_bytes + kLane1Offset, entropy + 4, 16);
      }
      next_ = kStateSizeT;
      return;
    }

    constexpr size_t kBufferSize = Randen::kSeedBytes / sizeof(uint32_t);
    alignas(16) uint32_t buffer[kBufferSize];

    // Randen::Absorb XORs the seed into state, which is then mixed by a call
    // to Randen::Generate. Seeding with only the provided entropy is preferred
    // to using an arbitrary generate() call, so use [rand.req.seed_seq]
    // size as a proxy for the number of entropy units that can be generated
    // without relying on seed sequence mixing...
    const size_t entropy_size = seq.size();
    if (entropy_size < kBufferSize) {
      // ... and only request that many values, or 256-bits, when unspecified.
      const size_t requested_entropy = (entropy_size == 0) ? 8u : entropy_size;
      std::fill(buffer + requested_entropy, buffer + kBufferSize, 0);
      seq.generate(buffer, buffer + requested_entropy);
      // The Randen paper suggests preferentially initializing even-numbered
      // 128-bit vectors of the randen state (there are 16 such vectors).
      // The seed data is merged into the state offset by 128-bits, which
      // implies preferring seed bytes [16..31, ..., 208..223]. Since the
      // buffer is 32-bit values, we swap the corresponding buffer positions in
      // 128-bit chunks.
      size_t dst = kBufferSize;
      while (dst > 7) {
        // leave the odd bucket as-is.
        dst -= 4;
        size_t src = dst >> 1;
        // swap 128-bits into the even bucket
        std::swap(buffer[--dst], buffer[--src]);
        std::swap(buffer[--dst], buffer[--src]);
        std::swap(buffer[--dst], buffer[--src]);
        std::swap(buffer[--dst], buffer[--src]);
      }
    } else {
      seq.generate(buffer, buffer + kBufferSize);
    }
#ifdef ABSL_IS_BIG_ENDIAN
    // Randen expects the seed buffer to be in Little Endian; reverse it on
    // Big Endian platforms.
    for (uint32_t& e : buffer) {
      e = absl::little_endian::FromHost(e);
    }
#endif
    auto* begin = state();
    PrepareStateForSeed<kMixExistingState>(begin);
    impl_.Absorb(buffer, begin);

    // Generate will be called when operator() is called.
    next_ = kStateSizeT;
  }

  // Returns the state array pointer, which is aligned to 16 bytes.
  // The first kCapacityT are the `inner' sponge; the remainder are available.
  result_type* state() {
    return reinterpret_cast<result_type*>(
        (reinterpret_cast<uintptr_t>(&raw_state_) & 0xf) ? (raw_state_ + 8)
                                                         : raw_state_);
  }
  const result_type* state() const {
    return const_cast<randen_engine*>(this)->state();
  }

  // raw state array, manually aligned in state(). This overallocates
  // by 8 bytes since C++ does not guarantee extended heap alignment.
  alignas(8) unsigned char raw_state_[Randen::kStateBytes + 8];
  size_t next_;  // index within state()
  Randen impl_;
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_RANDEN_ENGINE_H_
