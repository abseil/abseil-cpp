// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_STRINGS_INTERNAL_BITS_H_
#define ABSL_STRINGS_INTERNAL_BITS_H_

#include <cstdint>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#pragma intrinsic(_BitScanReverse64)
#endif

namespace absl {
namespace strings_internal {

// Returns the number of leading 0 bits in a 64-bit value.
inline int CountLeadingZeros64(uint64_t n) {
#if defined(__GNUC__)
  static_assert(sizeof(unsigned long long) == sizeof(n),  // NOLINT(runtime/int)
                "__builtin_clzll does not take 64bit arg");
  return n == 0 ? 64 : __builtin_clzll(n);
#elif defined(_MSC_VER) && defined(_M_X64)
  unsigned long result;  // NOLINT(runtime/int)
  if (_BitScanReverse64(&result, n)) {
    return 63 - result;
  }
  return 64;
#else
  int zeroes = 60;
  if (n >> 32) zeroes -= 32, n >>= 32;
  if (n >> 16) zeroes -= 16, n >>= 16;
  if (n >> 8) zeroes -= 8, n >>= 8;
  if (n >> 4) zeroes -= 4, n >>= 4;
  return "\4\3\2\2\1\1\1\1\0\0\0\0\0\0\0\0"[n] + zeroes;
#endif
}

}  // namespace strings_internal
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_BITS_H_
