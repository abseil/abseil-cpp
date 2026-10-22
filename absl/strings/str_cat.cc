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

#include "absl/strings/str_cat.h"

#include <assert.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/throw_delegate.h"
#include "absl/strings/internal/append_and_overwrite.h"
#include "absl/strings/numbers.h"
#include "absl/strings/resize_and_overwrite.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// ----------------------------------------------------------------------
// StrCat()
//    This merges the given strings or integers, with no delimiter. This
//    is designed to be the fastest possible way to construct a string out
//    of a mix of raw C strings, string_views, strings, and integer values.
// ----------------------------------------------------------------------

namespace {
// Append is merely a version of memcpy that returns the address of the byte
// after the area just overwritten.
inline char* absl_nonnull Append(char* absl_nonnull out, const AlphaNum& x) {
  // memcpy is allowed to overwrite arbitrary memory, so doing this after the
  // call would force an extra fetch of x.size().
  char* after = out + x.size();
  if (x.size() != 0) {
    memcpy(out, x.data(), x.size());
  }
  return after;
}

// Safely adds size_t values, throwing std::length_error if overflow occurs.
inline size_t SafeAdd(size_t a, size_t b) {
  const uint64_t sum = static_cast<uint64_t>(a) + b;
  if (ABSL_PREDICT_FALSE(sum > (std::numeric_limits<size_t>::max)())) {
    ThrowStdLengthError("absl string append length overflow");
  }
  return static_cast<size_t>(sum);
}

inline size_t SafeAdd(std::initializer_list<size_t> sizes) {
  uint64_t sum = 0;
  for (size_t size : sizes) {
    sum += size;
  }
  if (ABSL_PREDICT_FALSE(sum > (std::numeric_limits<size_t>::max)())) {
    ThrowStdLengthError("absl string append length overflow");
  }
  return static_cast<size_t>(sum);
}

template <typename Integer>
std::string IntegerToString(Integer i) {
  // Any integer (signed/unsigned) up to 64 bits can be formatted into a buffer
  // with 22 bytes (including NULL at the end).
  constexpr size_t kMaxDigits10 = 22;
  std::string result;
  StringResizeAndOverwrite(
      result, kMaxDigits10, [i](char* start, size_t buf_size) {
        // Note: This can be optimized to not write last zero.
        char* end = numbers_internal::FastIntToBuffer(i, start);
        auto size = static_cast<size_t>(end - start);
        ABSL_ASSERT(size < buf_size);
        return size;
      });
  return result;
}

template <typename Float>
std::string FloatToString(Float f) {
  std::string result;
  StringResizeAndOverwrite(result, numbers_internal::kSixDigitsToBufferSize,
                           [f](char* start, size_t buf_size) {
                             size_t size = numbers_internal::SixDigitsToBuffer(
                                 static_cast<double>(f), start);
                             ABSL_ASSERT(size < buf_size);
                             return size;
                           });
  return result;
}

template <typename Element>
std::string JoinContiguousImpl(const Element* absl_nullable data, size_t count,
                               absl::string_view sep) {
  std::string result;
  if (count != 0 && data != nullptr) {
    const Element* absl_nonnull elems = data;
    // Use uint64_t to prevent size_t overflow. We assume it is not possible for
    // in memory strings to overflow a uint64_t.
    uint64_t result_size = elems[0].size();
    for (size_t i = 1; i < count; ++i) {
      result_size += sep.size();
      result_size += elems[i].size();
    }

    if (result_size > 0) {
      constexpr uint64_t kMaxSize =
          uint64_t{(std::numeric_limits<size_t>::max)()};
      ABSL_INTERNAL_CHECK(result_size <= kMaxSize, "size_t overflow");

      absl::StringResizeAndOverwrite(
          result, static_cast<size_t>(result_size),
          [elems, count, sep](char* result_buf, size_t result_buf_size) {
            if (!elems[0].empty()) {
              memcpy(result_buf, elems[0].data(), elems[0].size());
              result_buf += elems[0].size();
            }
            for (size_t i = 1; i < count; ++i) {
              if (!sep.empty()) {
                memcpy(result_buf, sep.data(), sep.size());
                result_buf += sep.size();
              }
              if (!elems[i].empty()) {
                memcpy(result_buf, elems[i].data(), elems[i].size());
                result_buf += elems[i].size();
              }
            }
            return result_buf_size;
          });
    }
  }
  return result;
}

}  // namespace

namespace strings_internal {

absl::string_view FormatHex(Hex hex, char* absl_nonnull end) {
  static_assert(numbers_internal::kFastToBufferSize >= 32,
                "This function only works when output buffer >= 32 bytes long");
  const size_t real_width =
      absl::numbers_internal::FastHexToBufferZeroPad16(hex.value, end - 16);
  if (real_width >= hex.width) {
    return absl::string_view(end - real_width, real_width);
  }
  // Pad first 16 chars because FastHexToBufferZeroPad16 pads only to 16 and
  // max pad width can be up to 20.
  std::memset(end - 32, hex.fill, 16);
  // Patch up everything else up to the real_width.
  std::memset(end - real_width - 16, hex.fill, 16);
  return absl::string_view(end - hex.width, hex.width);
}

absl::string_view FormatDec(Dec dec, char* absl_nonnull end) {
  assert(dec.width <= numbers_internal::kFastToBufferSize);
  char* const minfill = end - dec.width;
  char* writer = end;
  uint64_t val = dec.value;
  while (val > 9) {
    *--writer = static_cast<char>('0' + (val % 10));
    val /= 10;
  }
  *--writer = static_cast<char>('0' + val);
  if (dec.neg) *--writer = '-';

  ptrdiff_t fillers = writer - minfill;
  if (fillers > 0) {
    // Tricky: if the fill character is ' ', then it's <fill><+/-><digits>
    // But...: if the fill character is '0', then it's <+/-><fill><digits>
    bool add_sign_again = false;
    if (dec.neg && dec.fill == '0') {  // If filling with '0',
      ++writer;                        // ignore the sign we just added
      add_sign_again = true;           // and re-add the sign later.
    }
    writer -= fillers;
    std::fill_n(writer, fillers, dec.fill);
    if (add_sign_again) *--writer = '-';
  }

  return absl::string_view(writer, static_cast<size_t>(end - writer));
}

std::string JoinStringViews(const absl::string_view* absl_nullable data,
                            size_t count, absl::string_view sep) {
  return JoinContiguousImpl(data, count, sep);
}

std::string JoinStrings(const std::string* absl_nullable data, size_t count,
                        absl::string_view sep) {
  return JoinContiguousImpl(data, count, sep);
}

std::string SingleArgStrCat(int x) { return IntegerToString(x); }
std::string SingleArgStrCat(unsigned int x) { return IntegerToString(x); }
// NOLINTNEXTLINE
std::string SingleArgStrCat(long x) { return IntegerToString(x); }
// NOLINTNEXTLINE
std::string SingleArgStrCat(unsigned long x) { return IntegerToString(x); }
// NOLINTNEXTLINE
std::string SingleArgStrCat(long long x) { return IntegerToString(x); }
// NOLINTNEXTLINE
std::string SingleArgStrCat(unsigned long long x) { return IntegerToString(x); }
std::string SingleArgStrCat(float x) { return FloatToString(x); }
std::string SingleArgStrCat(double x) { return FloatToString(x); }

}  // namespace strings_internal

strings_internal::AlphaNumBuffer<numbers_internal::kFastToBufferSize>
HighPrecision(float f) {
  strings_internal::AlphaNumBuffer<numbers_internal::kFastToBufferSize> result;
  result.size =
      strlen(numbers_internal::RoundTripFloatToBuffer(f, &result.data[0]));
  return result;
}

strings_internal::AlphaNumBuffer<numbers_internal::kFastToBufferSize>
HighPrecision(double d) {
  strings_internal::AlphaNumBuffer<numbers_internal::kFastToBufferSize> result;
  result.size =
      strlen(numbers_internal::RoundTripDoubleToBuffer(d, &result.data[0]));
  return result;
}

strings_internal::AlphaNumBuffer<numbers_internal::kSixDigitsToBufferSize>
SixDigits(double d) {
  strings_internal::AlphaNumBuffer<numbers_internal::kSixDigitsToBufferSize>
      result;
  result.size = numbers_internal::SixDigitsToBuffer(d, &result.data[0]);
  return result;
}

std::string StrCat(const AlphaNum& a, const AlphaNum& b) {
  std::string result;
  // Use uint64_t to prevent size_t overflow. We assume it is not possible for
  // in memory strings to overflow a uint64_t.
  constexpr uint64_t kMaxSize = uint64_t{std::numeric_limits<size_t>::max()};
  const uint64_t result_size =
      static_cast<uint64_t>(a.size()) + static_cast<uint64_t>(b.size());
  ABSL_INTERNAL_CHECK(result_size <= kMaxSize, "size_t overflow");
  absl::StringResizeAndOverwrite(result, static_cast<size_t>(result_size),
                                 [&a, &b](char* const begin, size_t buf_size) {
                                   char* out = begin;
                                   out = Append(out, a);
                                   out = Append(out, b);
                                   assert(out == begin + buf_size);
                                   return buf_size;
                                 });
  return result;
}

std::string StrCat(const AlphaNum& a, const AlphaNum& b, const AlphaNum& c) {
  std::string result;
  // Use uint64_t to prevent size_t overflow. We assume it is not possible for
  // in memory strings to overflow a uint64_t.
  constexpr uint64_t kMaxSize = uint64_t{std::numeric_limits<size_t>::max()};
  const uint64_t result_size = static_cast<uint64_t>(a.size()) +
                               static_cast<uint64_t>(b.size()) +
                               static_cast<uint64_t>(c.size());
  ABSL_INTERNAL_CHECK(result_size <= kMaxSize, "size_t overflow");
  absl::StringResizeAndOverwrite(
      result, static_cast<size_t>(result_size),
      [&a, &b, &c](char* const begin, size_t buf_size) {
        char* out = begin;
        out = Append(out, a);
        out = Append(out, b);
        out = Append(out, c);
        assert(out == begin + buf_size);
        return buf_size;
      });
  return result;
}

std::string StrCat(const AlphaNum& a, const AlphaNum& b, const AlphaNum& c,
                   const AlphaNum& d) {
  std::string result;
  // Use uint64_t to prevent size_t overflow. We assume it is not possible for
  // in memory strings to overflow a uint64_t.
  constexpr uint64_t kMaxSize = uint64_t{std::numeric_limits<size_t>::max()};
  const uint64_t result_size =
      static_cast<uint64_t>(a.size()) + static_cast<uint64_t>(b.size()) +
      static_cast<uint64_t>(c.size()) + static_cast<uint64_t>(d.size());
  ABSL_INTERNAL_CHECK(result_size <= kMaxSize, "size_t overflow");
  absl::StringResizeAndOverwrite(
      result, static_cast<size_t>(result_size),
      [&a, &b, &c, &d](char* const begin, size_t buf_size) {
        char* out = begin;
        out = Append(out, a);
        out = Append(out, b);
        out = Append(out, c);
        out = Append(out, d);
        assert(out == begin + buf_size);
        return buf_size;
      });
  return result;
}

namespace strings_internal {

// Do not call directly - these are not part of the public API.
std::string CatPieces(std::initializer_list<absl::string_view> pieces) {
  std::string result;
  // Use uint64_t to prevent size_t overflow. We assume it is not possible for
  // in memory strings to overflow a uint64_t.
  constexpr uint64_t kMaxSize = uint64_t{std::numeric_limits<size_t>::max()};
  uint64_t total_size = 0;
  for (absl::string_view piece : pieces) {
    total_size += piece.size();
  }
  ABSL_INTERNAL_CHECK(total_size <= kMaxSize, "size_t overflow");
  absl::StringResizeAndOverwrite(result, static_cast<size_t>(total_size),
                                 [&pieces](char* const begin, size_t buf_size) {
                                   char* out = begin;
                                   for (absl::string_view piece : pieces) {
                                     const size_t this_size = piece.size();
                                     if (this_size != 0) {
                                       memcpy(out, piece.data(), this_size);
                                       out += this_size;
                                     }
                                   }
                                   assert(out == begin + buf_size);
                                   return buf_size;
                                 });
  return result;
}

// It's possible to call StrAppend with an absl::string_view that is itself a
// fragment of the string we're appending to.  However the results of this are
// random. Therefore, check for this in debug mode.  Use unsigned math so we
// only have to do one comparison. Note, there's an exception case: appending an
// empty string is always allowed.
#define ASSERT_NO_OVERLAP(dest, src) \
  assert(((src).size() == 0) ||      \
         (uintptr_t((src).data() - (dest).data()) > uintptr_t((dest).size())))

void AppendPieces(std::string* absl_nonnull dest,
                  std::initializer_list<absl::string_view> pieces) {
  size_t to_append = 0;
  for (absl::string_view piece : pieces) {
    ASSERT_NO_OVERLAP(*dest, piece);
    to_append = SafeAdd(to_append, piece.size());
  }
  StringAppendAndOverwrite(*dest, to_append,
                           [&pieces](char* const buf, size_t buf_size) {
                             char* out = buf;
                             for (absl::string_view piece : pieces) {
                               const size_t this_size = piece.size();
                               if (this_size != 0) {
                                 memcpy(out, piece.data(), this_size);
                                 out += this_size;
                               }
                             }
                             assert(out == buf + buf_size);
                             return buf_size;
                           });
}

}  // namespace strings_internal

void StrAppend(std::string* absl_nonnull dest, const AlphaNum& a) {
  ASSERT_NO_OVERLAP(*dest, a);
  strings_internal::StringAppendAndOverwrite(
      *dest, a.size(), [&a](char* const buf, size_t buf_size) {
        char* out = buf;
        out = Append(out, a);
        assert(out == buf + buf_size);
        return buf_size;
      });
}

void StrAppend(std::string* absl_nonnull dest, const AlphaNum& a,
               const AlphaNum& b) {
  ASSERT_NO_OVERLAP(*dest, a);
  ASSERT_NO_OVERLAP(*dest, b);
  strings_internal::StringAppendAndOverwrite(
      *dest, SafeAdd(a.size(), b.size()),
      [&a, &b](char* const buf, size_t buf_size) {
        char* out = buf;
        out = Append(out, a);
        out = Append(out, b);
        assert(out == buf + buf_size);
        return buf_size;
      });
}

void StrAppend(std::string* absl_nonnull dest, const AlphaNum& a,
               const AlphaNum& b, const AlphaNum& c) {
  ASSERT_NO_OVERLAP(*dest, a);
  ASSERT_NO_OVERLAP(*dest, b);
  ASSERT_NO_OVERLAP(*dest, c);
  strings_internal::StringAppendAndOverwrite(
      *dest, SafeAdd({a.size(), b.size(), c.size()}),
      [&a, &b, &c](char* const buf, size_t buf_size) {
        char* out = buf;
        out = Append(out, a);
        out = Append(out, b);
        out = Append(out, c);
        assert(out == buf + buf_size);
        return buf_size;
      });
}

void StrAppend(std::string* absl_nonnull dest, const AlphaNum& a,
               const AlphaNum& b, const AlphaNum& c, const AlphaNum& d) {
  ASSERT_NO_OVERLAP(*dest, a);
  ASSERT_NO_OVERLAP(*dest, b);
  ASSERT_NO_OVERLAP(*dest, c);
  ASSERT_NO_OVERLAP(*dest, d);
  strings_internal::StringAppendAndOverwrite(
      *dest, SafeAdd({a.size(), b.size(), c.size(), d.size()}),
      [&a, &b, &c, &d](char* const buf, size_t buf_size) {
        char* out = buf;
        out = Append(out, a);
        out = Append(out, b);
        out = Append(out, c);
        out = Append(out, d);
        assert(out == buf + buf_size);
        return buf_size;
      });
}

ABSL_NAMESPACE_END
}  // namespace absl
