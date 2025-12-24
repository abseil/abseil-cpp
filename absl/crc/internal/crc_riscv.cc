// Copyright 2025 The Abseil Authors.
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

#include <cstddef>
#include <cstdint>

#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/crc/internal/cpu_detect.h"
#include "absl/crc/internal/crc.h"
#include "absl/crc/internal/crc_internal.h"

#if defined(__riscv) && (__riscv_xlen == 64) && \
    (defined(__riscv_zbc) || defined(__riscv_zbkc))

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

namespace {

struct V128 {
  uint64_t lo;
  uint64_t hi;
};

static inline uint64_t ClMul(uint64_t a, uint64_t b) {
  uint64_t out;
  __asm__("clmul %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
  return out;
}

static inline uint64_t ClMulH(uint64_t a, uint64_t b) {
  uint64_t out;
  __asm__("clmulh %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
  return out;
}

static inline V128 ClMul128(uint64_t a, uint64_t b) {
  return V128{ClMul(a, b), ClMulH(a, b)};
}

static inline V128 Xor(V128 a, V128 b) {
  return V128{a.lo ^ b.lo, a.hi ^ b.hi};
}

static inline V128 AndMask32(V128 a) {
  constexpr uint64_t kMask = 0x00000000FFFFFFFFull;
  return V128{a.lo & kMask, a.hi & kMask};
}

static inline V128 ShiftRight64(V128 a) { return V128{a.hi, 0}; }

static inline V128 ShiftRight32(V128 a) {
  return V128{(a.lo >> 32) | (a.hi << 32), (a.hi >> 32)};
}

static inline V128 Load128(const unsigned char* p) {
  uint64_t lo = absl::little_endian::Load64(p);
  uint64_t hi = absl::little_endian::Load64(p + 8);
  return V128{lo, hi};
}

uint32_t AbslCrc32cClmulRiscv(uint32_t crc, const unsigned char* buf,
                              uint64_t len) {
  // This implements CRC32C (Castagnoli) using carry-less multiplication.
  // The constants match those used by Abseil's x86/arm combined implementation.
  // Precondition: len >= 32 and len % 16 == 0.
  constexpr uint64_t kK5 = 0x0f20c0dfeull;
  constexpr uint64_t kK6 = 0x14cd00bd6ull;
  constexpr uint64_t kK7 = 0x0dd45aab8ull;
  constexpr uint64_t kP1 = 0x105ec76f0ull;
  constexpr uint64_t kP2 = 0x0dea713f1ull;

  // Fold 16-byte blocks into a single 128-bit value.
  V128 x = Load128(buf);
  x.lo ^= static_cast<uint64_t>(crc);
  buf += 16;
  len -= 16;

  // Each iteration folds one 16-byte block into x.
  // x = (clmul(x.lo, kK5) ^ clmul(x.hi, kK6) ^ next_block)
  while (len >= 16) {
    const V128 block = Load128(buf);
    const V128 lo = ClMul128(x.lo, kK5);
    const V128 hi = ClMul128(x.hi, kK6);
    x = Xor(Xor(lo, hi), block);
    buf += 16;
    len -= 16;
  }

  // Reduce the 128-bit folded value to a 32-bit CRC.
  // Step A: fold 128 -> 64.
  {
    // tmp = PMul01(k5k6, x) == clmul(k6 /*hi*/, x.lo /*lo*/)
    const V128 tmp = ClMul128(kK6, x.lo);
    x = Xor(ShiftRight64(x), tmp);
  }

  // Step B: fold 64 -> 32.
  {
    const V128 tmp = ShiftRight32(x);
    x = AndMask32(x);
    // PMulLow(k7k0, x) => clmul(kK7, x.lo)
    x = ClMul128(kK7, x.lo);
    x = Xor(x, tmp);
  }

  // Step C: Barrett reduction to 32-bit.
  {
    V128 tmp = AndMask32(x);
    // PMul01(kPoly, tmp) == clmul(kP2 /*hi*/, tmp.lo /*lo*/)
    tmp = ClMul128(kP2, tmp.lo);
    tmp = AndMask32(tmp);
    // PMulLow(kPoly, tmp) == clmul(kP1 /*lo*/, tmp.lo /*lo*/)
    tmp = ClMul128(kP1, tmp.lo);
    x = Xor(x, tmp);
  }

  // Extract the second 32-bit lane (matches V128_Extract32<1>).
  return static_cast<uint32_t>((x.lo >> 32) & 0xFFFFFFFFu);
}

}  // namespace

class CRC32AcceleratedRISCV : public CRC32 {
 public:
  CRC32AcceleratedRISCV() {}
  ~CRC32AcceleratedRISCV() override {}
  void Extend(uint32_t* crc, const void* bytes, size_t length) const override;
};

void CRC32AcceleratedRISCV::Extend(uint32_t* crc, const void* bytes,
                                   size_t length) const {
  const unsigned char* buf = static_cast<const unsigned char*>(bytes);
  uint32_t c = *crc;

  constexpr size_t kMinLen = 32;
  constexpr size_t kChunkLen = 16;

  if (length < kMinLen) {
    CRC32::Extend(crc, bytes, length);
    return;
  }

  size_t unaligned_length = length % kChunkLen;
  if (unaligned_length) {
    CRC32::Extend(crc, buf, unaligned_length);
    buf += unaligned_length;
    length -= unaligned_length;
    c = *crc;
  }

  c = AbslCrc32cClmulRiscv(c, buf, length);
  *crc = c;
}

CRCImpl* TryNewCRC32AcceleratedRISCV() {
  if (SupportsRiscvCrc32()) {
    return new CRC32AcceleratedRISCV();
  }
  return nullptr;
}

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl

#else

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

CRCImpl* TryNewCRC32AcceleratedRISCV() { return nullptr; }

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif
