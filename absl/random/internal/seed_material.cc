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

#include "absl/random/internal/seed_material.h"

#include <fcntl.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/config.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/numeric/bits.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

#if defined(_WIN32)

#include <windows.h>
#define ABSL_RANDOM_USE_BCRYPT 1
#pragma comment(lib, "bcrypt.lib")

#elif defined(__Fuchsia__)
#include <zircon/syscalls.h>

#elif defined(__APPLE__)
#if (!defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) ||   \
     __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101200) &&  \
    (!defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) ||  \
     __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ >= 100000) && \
    (!defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) ||      \
     __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ >= 100000) &&     \
    (!defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) ||   \
     __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ >= 30000)
#include <stdlib.h>
#define ABSL_RANDOM_USE_ARC4RANDOM_BUF 1
#endif

#elif defined(__OpenBSD__) || defined(__NetBSD__) || \
    (defined(__FreeBSD__) && __FreeBSD__ >= 12)
#include <stdlib.h>
#define ABSL_RANDOM_USE_ARC4RANDOM_BUF 1

#endif

#if !defined(_WIN32) && !defined(__Fuchsia__) && \
    !defined(ABSL_RANDOM_USE_ARC4RANDOM_BUF)

#if (defined(__GLIBC__) &&                                            \
     (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))) || \
    (defined(__ANDROID__) && defined(__ANDROID_API__) &&              \
     __ANDROID_API__ >= 28)
// glibc >= 2.25 and Android API >= 28 provide getrandom() in <sys/random.h>.
#include <sys/random.h>
#define ABSL_RANDOM_USE_GETRANDOM 1
#elif defined(__linux__)
#include <sys/syscall.h>
#if !defined(SYS_getrandom) && defined(__NR_getrandom)
#define SYS_getrandom __NR_getrandom
#endif
#if defined(SYS_getrandom)
#define ABSL_RANDOM_USE_SYS_GETRANDOM 1
#endif
#elif defined(__EMSCRIPTEN__) || defined(__wasi__)
#include <sys/random.h>
// Emscripten and WASI provide getentropy() in <sys/random.h>.
#define ABSL_RANDOM_USE_GET_ENTROPY 1
#endif

#endif  // !_WIN32 && !__Fuchsia__ && !ABSL_RANDOM_USE_ARC4RANDOM_BUF

#if defined(ABSL_RANDOM_USE_BCRYPT)
#include <bcrypt.h>

#ifndef BCRYPT_SUCCESS
#define BCRYPT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
// Also link bcrypt; this can be done via linker options or:
// #pragma comment(lib, "bcrypt.lib")
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

// Read OS Entropy for random number seeds.
// TODO(absl-team): Possibly place a cap on how much entropy may be read at a
// time.

#if defined(ABSL_RANDOM_USE_BCRYPT)

// On Windows potentially use the BCRYPT CNG API to read available entropy.
bool ReadSeedMaterialFromOSEntropyImpl(absl::Span<uint32_t> values) {
  BCRYPT_ALG_HANDLE hProvider;
  NTSTATUS ret;
  ret = BCryptOpenAlgorithmProvider(&hProvider, BCRYPT_RNG_ALGORITHM,
                                    MS_PRIMITIVE_PROVIDER, 0);
  if (!(BCRYPT_SUCCESS(ret))) {
    ABSL_RAW_LOG(ERROR, "Failed to open crypto provider.");
    return false;
  }
  auto* buffer = reinterpret_cast<UCHAR*>(values.data());
  size_t buffer_size = sizeof(uint32_t) * values.size();
  constexpr size_t kMaxChunk =
      static_cast<size_t>((std::numeric_limits<ULONG>::max)() & ~ULONG{3});
  while (buffer_size > 0) {
    const ULONG chunk = static_cast<ULONG>((std::min)(buffer_size, kMaxChunk));
    ret = BCryptGenRandom(hProvider, buffer, chunk, 0);
    if (!(BCRYPT_SUCCESS(ret))) {
      break;
    }
    buffer += chunk;
    buffer_size -= chunk;
  }
  BCryptCloseAlgorithmProvider(hProvider, 0);
  return BCRYPT_SUCCESS(ret);
}

#elif defined(__Fuchsia__)

bool ReadSeedMaterialFromOSEntropyImpl(absl::Span<uint32_t> values) {
  auto buffer = reinterpret_cast<uint8_t*>(values.data());
  size_t buffer_size = sizeof(uint32_t) * values.size();
  zx_cprng_draw(buffer, buffer_size);
  return true;
}

#elif defined(ABSL_RANDOM_USE_ARC4RANDOM_BUF)

bool ReadSeedMaterialFromOSEntropyImpl(absl::Span<uint32_t> values) {
  auto* buffer = reinterpret_cast<uint8_t*>(values.data());
  size_t buffer_size = sizeof(uint32_t) * values.size();
  arc4random_buf(buffer, buffer_size);
  ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(buffer, buffer_size);
  return true;
}

#else

constexpr size_t kMaxPosixRead =
    static_cast<size_t>((std::numeric_limits<int>::max)());

// Fills `values` by repeatedly calling `read_fn(buffer, to_read)` in chunks of
// at most `max_chunk` bytes. `read_fn` follows the POSIX `read()` contract:
// returns bytes read (> 0), 0 on EOF/failure, or < 0 with `errno` set
// (retrying on `EINTR`).
template <typename ReadFn>
bool ReadEntropyBytes(absl::Span<uint32_t> values, size_t max_chunk,
                      ReadFn read_fn) {
  auto* buffer = reinterpret_cast<uint8_t*>(values.data());
  size_t buffer_size = sizeof(uint32_t) * values.size();
  while (buffer_size > 0) {
    const size_t to_read = (std::min)(buffer_size, max_chunk);
    const ssize_t bytes_read = read_fn(buffer, to_read);
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (bytes_read == 0) {
      return false;
    }
    const size_t n = static_cast<size_t>(bytes_read);
    // https://github.com/google/sanitizers/issues/1173
    // MemorySanitizer cannot always see through getentropy() or raw syscalls.
    ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(buffer, n);
    buffer += n;
    buffer_size -= n;
  }
  return true;
}

#if defined(ABSL_RANDOM_USE_GETRANDOM) || defined(ABSL_RANDOM_USE_SYS_GETRANDOM)
// On Linux and glibc/Android platforms with getrandom(), read entropy directly
// without opening /dev/urandom. Note that libc or kernel headers may declare
// getrandom() / SYS_getrandom while the running kernel or sandbox does not
// support it (e.g., ENOSYS/EPERM), in which case this function returns false
// and the caller falls back to /dev/urandom.
bool ReadSeedMaterialFromGetRandom(absl::Span<uint32_t> values) {
  auto read_chunk = [](uint8_t* buffer, size_t to_read) -> ssize_t {
#if defined(ABSL_RANDOM_USE_GETRANDOM)
    return getrandom(buffer, to_read, 0u);
#else
    return static_cast<ssize_t>(syscall(SYS_getrandom, buffer, to_read, 0u));
#endif
  };
  return ReadEntropyBytes(values, kMaxPosixRead, read_chunk);
}
#endif  // defined(ABSL_RANDOM_USE_GETRANDOM) ||
        // defined(ABSL_RANDOM_USE_SYS_GETRANDOM)

#if defined(ABSL_RANDOM_USE_GET_ENTROPY)
// On *nix platforms without getrandom() (e.g., Emscripten, WASI), use
// getentropy() if supported. Note that libc may support getentropy(), but the
// runtime may not, in which case this function will return false.
bool ReadSeedMaterialFromGetEntropy(absl::Span<uint32_t> values) {
  // getentropy() has a maximum permitted length of 256.
  return ReadEntropyBytes(values, /*max_chunk=*/256,
                          [](uint8_t* buffer, size_t to_read) -> ssize_t {
                            return getentropy(buffer, to_read) == 0
                                       ? static_cast<ssize_t>(to_read)
                                       : -1;
                          });
}
#endif  // defined(ABSL_RANDOM_USE_GET_ENTROPY)

// On *nix, read entropy from /dev/urandom.
bool ReadSeedMaterialFromDevURandom(absl::Span<uint32_t> values) {
  const char kEntropyFile[] = "/dev/urandom";

#ifdef O_CLOEXEC
  constexpr int kOpenFlags = O_RDONLY | O_CLOEXEC;
#else
  constexpr int kOpenFlags = O_RDONLY;
#endif
  int dev_urandom;
  do {
    dev_urandom = open(kEntropyFile, kOpenFlags);
  } while (dev_urandom < 0 && errno == EINTR);
  if (dev_urandom < 0) {
    ABSL_RAW_LOG(ERROR, "Failed to open /dev/urandom.");
    return false;
  }

  const bool success = ReadEntropyBytes(
      values, kMaxPosixRead,
      [dev_urandom](uint8_t* buffer, size_t to_read) -> ssize_t {
        return read(dev_urandom, buffer, to_read);
      });

  close(dev_urandom);
  return success;
}

bool ReadSeedMaterialFromOSEntropyImpl(absl::Span<uint32_t> values) {
#if defined(ABSL_RANDOM_USE_GETRANDOM) || defined(ABSL_RANDOM_USE_SYS_GETRANDOM)
  if (ReadSeedMaterialFromGetRandom(values)) {
    return true;
  }
#elif defined(ABSL_RANDOM_USE_GET_ENTROPY)
  if (ReadSeedMaterialFromGetEntropy(values)) {
    return true;
  }
#endif
  // Libc or the kernel may not support getrandom() or getentropy(), so we still
  // fall back to ReadSeedMaterialFromDevURandom().
  return ReadSeedMaterialFromDevURandom(values);
}

#endif

// Standard SipHash-2-4 with 128-bit output (Aumasson & Bernstein).
// Pure 64-bit ARX, constant-time, zero lookup tables, little-endian word order.
class SipHash24_128 {
 public:
  explicit SipHash24_128(const SaltMaterial& key) noexcept {
    const uint64_t k0 =
        uint64_t{key[0]} | (static_cast<uint64_t>(key[1]) << 32);
    const uint64_t k1 =
        uint64_t{key[2]} | (static_cast<uint64_t>(key[3]) << 32);
    v_[0] = uint64_t{0x736f6d6570736575} ^ k0;
    v_[1] = uint64_t{0x646f72616e646f6d} ^ k1 ^ uint64_t{0xee};
    v_[2] = uint64_t{0x6c7967656e657261} ^ k0;
    v_[3] = uint64_t{0x7465646279746573} ^ k1;
  }

  ~SipHash24_128() noexcept { Cleanse(); }

  void UpdateU64(uint64_t word) noexcept {
    if (has_pending_u32_) {
      CompressBlock(uint64_t{pending_u32_} | (word << 32));
      pending_u32_ = static_cast<uint32_t>(word >> 32);
    } else {
      CompressBlock(word);
    }
    total_bytes_ += 8;
  }

  void UpdateWordsLe(absl::Span<const uint32_t> words) noexcept {
    size_t i = 0;
    if (has_pending_u32_ && !words.empty()) {
      CompressBlock(uint64_t{pending_u32_} |
                    (static_cast<uint64_t>(words[0]) << 32));
      has_pending_u32_ = false;
      pending_u32_ = 0;
      total_bytes_ += 4;
      i = 1;
    }
    for (; i + 1 < words.size(); i += 2) {
      CompressBlock(uint64_t{words[i]} |
                    (static_cast<uint64_t>(words[i + 1]) << 32));
      total_bytes_ += 8;
    }
    if (i < words.size()) {
      pending_u32_ = words[i];
      has_pending_u32_ = true;
      total_bytes_ += 4;
    }
  }

  SaltMaterial Final() noexcept {
    uint64_t b = (total_bytes_ & 0xffu) << 56;
    if (has_pending_u32_) {
      b |= uint64_t{pending_u32_};
    }
    CompressBlock(b);
    v_[2] ^= uint64_t{0xee};
    for (int i = 0; i < 4; ++i) SipRound();
    const uint64_t h0 = v_[0] ^ v_[1] ^ v_[2] ^ v_[3];
    v_[1] ^= uint64_t{0xdd};
    for (int i = 0; i < 4; ++i) SipRound();
    const uint64_t h1 = v_[0] ^ v_[1] ^ v_[2] ^ v_[3];
    SaltMaterial out = {
        static_cast<uint32_t>(h0),
        static_cast<uint32_t>(h0 >> 32),
        static_cast<uint32_t>(h1),
        static_cast<uint32_t>(h1 >> 32),
    };
    return out;
  }

 private:
  void SipRound() noexcept {
    v_[0] += v_[1];
    v_[1] = absl::rotl(v_[1], 13);
    v_[1] ^= v_[0];
    v_[0] = absl::rotl(v_[0], 32);
    v_[2] += v_[3];
    v_[3] = absl::rotl(v_[3], 16);
    v_[3] ^= v_[2];
    v_[0] += v_[3];
    v_[3] = absl::rotl(v_[3], 21);
    v_[3] ^= v_[0];
    v_[2] += v_[1];
    v_[1] = absl::rotl(v_[1], 17);
    v_[1] ^= v_[2];
    v_[2] = absl::rotl(v_[2], 32);
  }

  void CompressBlock(uint64_t m) noexcept {
    v_[3] ^= m;
    SipRound();
    SipRound();
    v_[0] ^= m;
  }

  void Cleanse() noexcept {
    CleanseSeedMaterial(v_, sizeof(v_));
    CleanseSeedMaterial(&pending_u32_, sizeof(pending_u32_));
    has_pending_u32_ = false;
    total_bytes_ = 0;
  }

  uint64_t v_[4];
  uint64_t total_bytes_ = 0;
  uint32_t pending_u32_ = 0;
  bool has_pending_u32_ = false;
};

SaltMaterial SipHashKeyedDomain(const SaltMaterial& salt, uint32_t domain_tag,
                                uint32_t round, uint64_t param_size,
                                uint64_t out_size,
                                absl::Span<const uint32_t> msg_words) noexcept {
  SipHash24_128 hasher(salt);
  hasher.UpdateU64(uint64_t{domain_tag} | (static_cast<uint64_t>(round) << 32));
  hasher.UpdateU64(param_size);
  hasher.UpdateU64(out_size);
  hasher.UpdateWordsLe(msg_words);
  return hasher.Final();
}

void SipHashXorSqueeze(const SaltMaterial& seed,
                       absl::Span<uint32_t> out) noexcept {
  uint64_t counter = 0;
  size_t offset = 0;
  while (offset < out.size()) {
    SipHash24_128 hasher(seed);
    hasher.UpdateU64(counter++);
    SaltMaterial block = hasher.Final();
    ScopedCleanse block_cleaner(absl::MakeSpan(block));
    const size_t chunk = (std::min)(out.size() - offset, block.size());
    for (size_t i = 0; i < chunk; ++i) {
      out[offset + i] ^= block[i];
    }
    offset += chunk;
  }
}

}  // namespace

bool ReadSeedMaterialFromOSEntropy(absl::Span<uint32_t> values) {
  assert(values.data() != nullptr);
  if (values.data() == nullptr) {
    return false;
  }
  if (values.empty()) {
    return true;
  }
  return ReadSeedMaterialFromOSEntropyImpl(values);
}

void MixIntoSeedMaterial(absl::Span<const uint32_t> sequence,
                         absl::Span<uint32_t> seed_material) {
  // Algorithm is based on code available at
  // https://gist.github.com/imneme/540829265469e673d045
  constexpr uint32_t kInitVal = 0x43b0d7e5;
  constexpr uint32_t kHashMul = 0x931e8875;
  constexpr uint32_t kMixMulL = 0xca01f9dd;
  constexpr uint32_t kMixMulR = 0x4973f715;
  constexpr uint32_t kShiftSize = sizeof(uint32_t) * 8 / 2;

  uint32_t hash_const = kInitVal;
  auto hash = [&](uint32_t value) {
    value ^= hash_const;
    hash_const *= kHashMul;
    value *= hash_const;
    value ^= value >> kShiftSize;
    return value;
  };

  auto mix = [&](uint32_t x, uint32_t y) {
    uint32_t result = kMixMulL * x - kMixMulR * y;
    result ^= result >> kShiftSize;
    return result;
  };

  for (const auto& seq_val : sequence) {
    for (auto& elem : seed_material) {
      elem = mix(elem, hash(seq_val));
    }
  }
}

SaltMaterial NormalizeSaltMaterial(absl::Span<const uint32_t> words) {
  SaltMaterial salt{};
  if (words.size() == salt.size()) {
    absl::c_copy(words, salt);
    return salt;
  }
  // Fixed domain-separation key: little-endian ASCII "absl_salt_norm_1".
  constexpr SaltMaterial kNormKey = {0x6c736261u, 0x6c61735fu, 0x6f6e5f74u,
                                     0x315f6d72u};
  SipHash24_128 hasher(kNormKey);
  hasher.UpdateU64(static_cast<uint64_t>(words.size()));
  hasher.UpdateWordsLe(words);
  return hasher.Final();
}

void PermuteSeedMaterialWithSalt(const SaltMaterial& salt, uint32_t domain_tag,
                                 uint64_t param_size,
                                 absl::Span<uint32_t> seed_material) {
  const size_t n = seed_material.size();
  if (n == 0) {
    return;
  }
  if (n == 1) {
    // 6-round 16-bit Feistel PRP: exact bijection on uint32_t.
    uint32_t lo = seed_material[0] & 0xffffu;
    uint32_t hi = seed_material[0] >> 16;
    for (uint32_t r = 0; r < 6; ++r) {
      SaltMaterial digest =
          SipHashKeyedDomain(salt, domain_tag, r, param_size,
                             /*out_size=*/1, absl::MakeConstSpan(&hi, 1));
      ScopedCleanse digest_cleaner(absl::MakeSpan(digest));
      const uint32_t next_lo = hi;
      const uint32_t next_hi = lo ^ (digest[0] & 0xffffu);
      lo = next_lo;
      hi = next_hi;
    }
    seed_material[0] = lo | (hi << 16);
    return;
  }

  // 4-round balanced Feistel PRP: exact bijection on (uint32_t)^n.
  const size_t left_len = n / 2;
  auto left = seed_material.subspan(0, left_len);
  auto right = seed_material.subspan(left_len);
  for (uint32_t r = 0; r < 4; ++r) {
    if ((r & 1u) == 0u) {
      SaltMaterial round_seed =
          SipHashKeyedDomain(salt, domain_tag, r, param_size, n, right);
      ScopedCleanse seed_cleaner(absl::MakeSpan(round_seed));
      SipHashXorSqueeze(round_seed, left);
    } else {
      SaltMaterial round_seed =
          SipHashKeyedDomain(salt, domain_tag, r, param_size, n, left);
      ScopedCleanse seed_cleaner(absl::MakeSpan(round_seed));
      SipHashXorSqueeze(round_seed, right);
    }
  }
}

void DeriveSeedMaterialWithSalt(const SaltMaterial& salt, uint32_t domain_tag,
                                absl::Span<const uint32_t> seed_words,
                                absl::Span<uint32_t> out) {
  if (out.empty()) {
    return;
  }
  SaltMaterial key = SipHashKeyedDomain(
      salt, domain_tag, /*round=*/0, static_cast<uint64_t>(seed_words.size()),
      static_cast<uint64_t>(out.size()), seed_words);
  ScopedCleanse key_cleaner(absl::MakeSpan(key));
  if (out.size() <= key.size()) {
    absl::c_copy_n(key, out.size(), out.data());
    return;
  }
  // Derive a second 128-bit digest so outputs wider than 128 bits retain
  // 256 bits of intermediate entropy (matching kEntropyBitsNeeded).
  SaltMaterial extra = SipHashKeyedDomain(
      salt, domain_tag, /*round=*/1, static_cast<uint64_t>(seed_words.size()),
      static_cast<uint64_t>(out.size()), seed_words);
  ScopedCleanse extra_cleaner(absl::MakeSpan(extra));
  uint64_t counter = 0;
  size_t offset = 0;
  while (offset < out.size()) {
    SipHash24_128 hasher(key);
    hasher.UpdateU64(counter++);
    hasher.UpdateWordsLe(extra);
    SaltMaterial block = hasher.Final();
    ScopedCleanse block_cleaner(absl::MakeSpan(block));
    const size_t chunk = (std::min)(out.size() - offset, block.size());
    absl::c_copy_n(block, chunk, out.data() + offset);
    offset += chunk;
  }
}

std::optional<SaltMaterial> GetSaltMaterial() {
  // Salt must be common for all generators within the same process so read it
  // only once and store in static variable.
  static const auto salt_material = []() -> std::optional<SaltMaterial> {

    SaltMaterial salt_value{};
    if (ReadSeedMaterialFromOSEntropy(absl::MakeSpan(salt_value))) {
      return salt_value;
    }

    CleanseSeedMaterial(salt_value.data(), sizeof(salt_value));
    return std::nullopt;
  }();

  return salt_material;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
