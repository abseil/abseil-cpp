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

#include "absl/strings/substitute.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/nullability.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/internal/append_and_overwrite.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace substitute_internal {

// Checked addition to avoid overflow on 32-bit. (In 64-bit the callers wouldn't
// get anywhere close to the limit given that 2^64 bytes of memory is beyond
// anything physically possible.)
// Note that we don't declare the parameters as size_t in order to avoid
// accidental implicit conversions.
template <int&..., typename T>
[[nodiscard]] std::enable_if_t<std::is_same_v<T, size_t>, T> CheckedAdd(T a,
                                                                        T b) {
  ABSL_INTERNAL_CHECK(b <= (std::numeric_limits<T>::max)() - a,
                      "unsigned integer overflow");
  return a + b;
}

void SubstituteAndAppendArray(std::string* absl_nonnull output,
                              absl::string_view format,
                              const absl::string_view* absl_nullable args_array,
                              size_t num_args) {
  // Determine total size needed.
  size_t size = 0;
  for (size_t i = 0; i < format.size(); i++) {
    if (format[i] == '$') {
      if (i + 1 >= format.size()) {
#ifndef NDEBUG
        ABSL_RAW_LOG(FATAL,
                     "Invalid absl::Substitute() format string: \"%s\".",
                     absl::CEscape(format).c_str());
#endif
        return;
      } else if (absl::ascii_isdigit(
                     static_cast<unsigned char>(format[i + 1]))) {
        int index = format[i + 1] - '0';
        if (static_cast<size_t>(index) >= num_args) {
#ifndef NDEBUG
          ABSL_RAW_LOG(
              FATAL,
              "Invalid absl::Substitute() format string: asked for \"$"
              "%d\", but only %d args were given.  Full format string was: "
              "\"%s\".",
              index, static_cast<int>(num_args), absl::CEscape(format).c_str());
#endif
          return;
        }
        size = CheckedAdd(size, args_array[index].size());
        ++i;  // Skip next char.
      } else if (format[i + 1] == '$') {
        size = CheckedAdd(size, size_t{1});
        ++i;  // Skip next char.
      } else {
#ifndef NDEBUG
        ABSL_RAW_LOG(FATAL,
                     "Invalid absl::Substitute() format string: \"%s\".",
                     absl::CEscape(format).c_str());
#endif
        return;
      }
    } else {
      size = CheckedAdd(size, size_t{1});
    }
  }

  if (size == 0) return;

  // Build the string.
  ABSL_INTERNAL_CHECK(size <= output->max_size() - output->size(),
                      "Exceeds std::string::max_size()");
  strings_internal::StringAppendAndOverwrite(
      *output, size, [format, args_array](char* const buf, size_t buf_size) {
        char* target = buf;
        for (size_t i = 0; i < format.size(); i++) {
          if (format[i] == '$') {
            if (absl::ascii_isdigit(
                    static_cast<unsigned char>(format[i + 1]))) {
              const absl::string_view src = args_array[format[i + 1] - '0'];
              target = std::copy(src.begin(), src.end(), target);
              ++i;  // Skip next char.
            } else if (format[i + 1] == '$') {
              *target++ = '$';
              ++i;  // Skip next char.
            }
          } else {
            *target++ = format[i];
          }
        }
        assert(target == buf + buf_size);
        return buf_size;
      });
}

ABSL_ATTRIBUTE_NOINLINE std::string SubstituteArray(
    absl::string_view format, const absl::string_view* absl_nullable args_array,
    size_t num_args) {
  std::string result;
  SubstituteAndAppendArray(&result, format, args_array, num_args);
  return result;
}

Arg::Arg(const void* absl_nullable value) {
  static_assert(sizeof(scratch_) >= sizeof(value) * 2 + 2,
                "fix sizeof(scratch_)");
  if (value == nullptr) {
    piece_ = "NULL";
  } else {
    char* ptr = scratch_ + sizeof(scratch_);
    uintptr_t num = reinterpret_cast<uintptr_t>(value);
    do {
      *--ptr = absl::numbers_internal::kHexChar[num & 0xf];
      num >>= 4;
    } while (num != 0);
    *--ptr = 'x';
    *--ptr = '0';
    piece_ = absl::string_view(
        ptr, static_cast<size_t>(scratch_ + sizeof(scratch_) - ptr));
  }
}

Arg::Arg(Hex hex)
    : piece_(strings_internal::FormatHex(
          hex, &scratch_[numbers_internal::kFastToBufferSize])) {}

Arg::Arg(Dec dec)
    : piece_(strings_internal::FormatDec(
          dec, &scratch_[numbers_internal::kFastToBufferSize])) {}

}  // namespace substitute_internal
ABSL_NAMESPACE_END
}  // namespace absl
