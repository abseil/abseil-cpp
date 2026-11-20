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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/raw_logging.h"
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

std::optional<uint32_t> GetSaltMaterial() {
  // Salt must be common for all generators within the same process so read it
  // only once and store in static variable.
  static const auto salt_material = []() -> std::optional<uint32_t> {
    uint32_t salt_value = 0;

    if (ReadSeedMaterialFromOSEntropy(absl::MakeSpan(&salt_value, 1))) {
      return salt_value;
    }

    return std::nullopt;
  }();

  return salt_material;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
