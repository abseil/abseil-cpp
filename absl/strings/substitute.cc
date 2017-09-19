// Copyright 2017 The Abseil Authors.
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

#include "absl/strings/substitute.h"

#include <algorithm>

#include "absl/base/internal/raw_logging.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/internal/resize_uninitialized.h"
#include "absl/strings/string_view.h"

namespace absl {
namespace substitute_internal {

void SubstituteAndAppendArray(std::string* output, absl::string_view format,
                              const absl::string_view* args_array,
                              size_t num_args) {
  // Determine total size needed.
  size_t size = 0;
  for (size_t i = 0; i < format.size(); i++) {
    if (format[i] == '$') {
      if (i + 1 >= format.size()) {
#ifndef NDEBUG
        ABSL_RAW_LOG(FATAL,
                     "Invalid strings::Substitute() format std::string: \"%s\".",
                     absl::CEscape(format).c_str());
#endif
        return;
      } else if (absl::ascii_isdigit(format[i + 1])) {
        int index = format[i + 1] - '0';
        if (static_cast<size_t>(index) >= num_args) {
#ifndef NDEBUG
          ABSL_RAW_LOG(
              FATAL,
              "Invalid strings::Substitute() format std::string: asked for \"$"
              "%d\", but only %d args were given.  Full format std::string was: "
              "\"%s\".",
              index, static_cast<int>(num_args), absl::CEscape(format).c_str());
#endif
          return;
        }
        size += args_array[index].size();
        ++i;  // Skip next char.
      } else if (format[i + 1] == '$') {
        ++size;
        ++i;  // Skip next char.
      } else {
#ifndef NDEBUG
        ABSL_RAW_LOG(FATAL,
                     "Invalid strings::Substitute() format std::string: \"%s\".",
                     absl::CEscape(format).c_str());
#endif
        return;
      }
    } else {
      ++size;
    }
  }

  if (size == 0) return;

  // Build the std::string.
  size_t original_size = output->size();
  strings_internal::STLStringResizeUninitialized(output, original_size + size);
  char* target = &(*output)[original_size];
  for (size_t i = 0; i < format.size(); i++) {
    if (format[i] == '$') {
      if (absl::ascii_isdigit(format[i + 1])) {
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

  assert(target == output->data() + output->size());
}

Arg::Arg(const void* value) {
  static_assert(sizeof(scratch_) >= sizeof(value) * 2 + 2,
                "fix sizeof(scratch_)");
  if (value == nullptr) {
    piece_ = "NULL";
  } else {
    char* ptr = scratch_ + sizeof(scratch_);
    uintptr_t num = reinterpret_cast<uintptr_t>(value);
    static const char kHexDigits[] = "0123456789abcdef";
    do {
      *--ptr = kHexDigits[num & 0xf];
      num >>= 4;
    } while (num != 0);
    *--ptr = 'x';
    *--ptr = '0';
    piece_ = absl::string_view(ptr, scratch_ + sizeof(scratch_) - ptr);
  }
}

}  // namespace substitute_internal
}  // namespace absl
