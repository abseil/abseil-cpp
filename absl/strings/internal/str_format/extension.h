//
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
//
#ifndef ABSL_STRINGS_INTERNAL_STR_FORMAT_EXTENSION_H_
#define ABSL_STRINGS_INTERNAL_STR_FORMAT_EXTENSION_H_

#include <limits.h>

#include <cstddef>
#include <cstring>
#include <ostream>

#include "absl/base/config.h"
#include "absl/base/port.h"
#include "absl/strings/internal/str_format/output.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {

class FormatRawSinkImpl {
 public:
  // Implicitly convert from any type that provides the hook function as
  // described above.
  template <typename T, decltype(str_format_internal::InvokeFlush(
                            std::declval<T*>(), string_view()))* = nullptr>
  FormatRawSinkImpl(T* raw)  // NOLINT
      : sink_(raw), write_(&FormatRawSinkImpl::Flush<T>) {}

  void Write(string_view s) { write_(sink_, s); }

  template <typename T>
  static FormatRawSinkImpl Extract(T s) {
    return s.sink_;
  }

 private:
  template <typename T>
  static void Flush(void* r, string_view s) {
    str_format_internal::InvokeFlush(static_cast<T*>(r), s);
  }

  void* sink_;
  void (*write_)(void*, string_view);
};

// An abstraction to which conversions write their string data.
class FormatSinkImpl {
 public:
  explicit FormatSinkImpl(FormatRawSinkImpl raw) : raw_(raw) {}

  ~FormatSinkImpl() { Flush(); }

  void Flush() {
    raw_.Write(string_view(buf_, pos_ - buf_));
    pos_ = buf_;
  }

  void Append(size_t n, char c) {
    if (n == 0) return;
    size_ += n;
    auto raw_append = [&](size_t count) {
      memset(pos_, c, count);
      pos_ += count;
    };
    while (n > Avail()) {
      n -= Avail();
      if (Avail() > 0) {
        raw_append(Avail());
      }
      Flush();
    }
    raw_append(n);
  }

  void Append(string_view v) {
    size_t n = v.size();
    if (n == 0) return;
    size_ += n;
    if (n >= Avail()) {
      Flush();
      raw_.Write(v);
      return;
    }
    memcpy(pos_, v.data(), n);
    pos_ += n;
  }

  size_t size() const { return size_; }

  // Put 'v' to 'sink' with specified width, precision, and left flag.
  bool PutPaddedString(string_view v, int w, int p, bool l);

  template <typename T>
  T Wrap() {
    return T(this);
  }

  template <typename T>
  static FormatSinkImpl* Extract(T* s) {
    return s->sink_;
  }

 private:
  size_t Avail() const { return buf_ + sizeof(buf_) - pos_; }

  FormatRawSinkImpl raw_;
  size_t size_ = 0;
  char* pos_ = buf_;
  char buf_[1024];
};

struct Flags {
  bool basic : 1;     // fastest conversion: no flags, width, or precision
  bool left : 1;      // "-"
  bool show_pos : 1;  // "+"
  bool sign_col : 1;  // " "
  bool alt : 1;       // "#"
  bool zero : 1;      // "0"
  std::string ToString() const;
  friend std::ostream& operator<<(std::ostream& os, const Flags& v) {
    return os << v.ToString();
  }
};

// clang-format off
#define ABSL_CONVERSION_CHARS_EXPAND_(X_VAL, X_SEP) \
  /* text */ \
  X_VAL(c) X_SEP X_VAL(C) X_SEP X_VAL(s) X_SEP X_VAL(S) X_SEP \
  /* ints */ \
  X_VAL(d) X_SEP X_VAL(i) X_SEP X_VAL(o) X_SEP \
  X_VAL(u) X_SEP X_VAL(x) X_SEP X_VAL(X) X_SEP \
  /* floats */ \
  X_VAL(f) X_SEP X_VAL(F) X_SEP X_VAL(e) X_SEP X_VAL(E) X_SEP \
  X_VAL(g) X_SEP X_VAL(G) X_SEP X_VAL(a) X_SEP X_VAL(A) X_SEP \
  /* misc */ \
  X_VAL(n) X_SEP X_VAL(p)

enum class FormatConversionChar : uint8_t {
    c, C, s, S,              // text
    d, i, o, u, x, X,        // int
    f, F, e, E, g, G, a, A,  // float
    n, p,                    // misc
    kNone,
    none = kNone
};
// clang-format on

inline FormatConversionChar FormatConversionCharFromChar(char c) {
  switch (c) {
#define X_VAL(id) \
  case #id[0]:    \
    return FormatConversionChar::id;
    ABSL_CONVERSION_CHARS_EXPAND_(X_VAL, )
#undef X_VAL
  }
  return FormatConversionChar::kNone;
}

inline int FormatConversionCharRadix(FormatConversionChar c) {
  switch (c) {
    case FormatConversionChar::x:
    case FormatConversionChar::X:
    case FormatConversionChar::a:
    case FormatConversionChar::A:
    case FormatConversionChar::p:
      return 16;
    case FormatConversionChar::o:
      return 8;
    default:
      return 10;
  }
}

inline bool FormatConversionCharIsUpper(FormatConversionChar c) {
  switch (c) {
    case FormatConversionChar::X:
    case FormatConversionChar::F:
    case FormatConversionChar::E:
    case FormatConversionChar::G:
    case FormatConversionChar::A:
      return true;
    default:
      return false;
  }
}

inline bool FormatConversionCharIsSigned(FormatConversionChar c) {
  switch (c) {
    case FormatConversionChar::d:
    case FormatConversionChar::i:
      return true;
    default:
      return false;
  }
}

inline bool FormatConversionCharIsIntegral(FormatConversionChar c) {
  switch (c) {
    case FormatConversionChar::d:
    case FormatConversionChar::i:
    case FormatConversionChar::u:
    case FormatConversionChar::o:
    case FormatConversionChar::x:
    case FormatConversionChar::X:
      return true;
    default:
      return false;
  }
}

inline bool FormatConversionCharIsFloat(FormatConversionChar c) {
  switch (c) {
    case FormatConversionChar::a:
    case FormatConversionChar::e:
    case FormatConversionChar::f:
    case FormatConversionChar::g:
    case FormatConversionChar::A:
    case FormatConversionChar::E:
    case FormatConversionChar::F:
    case FormatConversionChar::G:
      return true;
    default:
      return false;
  }
}

inline char FormatConversionCharToChar(FormatConversionChar c) {
  switch (c) {
#define X_VAL(e)                \
  case FormatConversionChar::e: \
    return #e[0];
#define X_SEP
    ABSL_CONVERSION_CHARS_EXPAND_(X_VAL, X_SEP)
    case FormatConversionChar::kNone:
      return '\0';
#undef X_VAL
#undef X_SEP
  }
  return '\0';
}

// The associated char.
inline std::ostream& operator<<(std::ostream& os, FormatConversionChar v) {
  char c = FormatConversionCharToChar(v);
  if (!c) c = '?';
  return os << c;
}

class ConversionSpec {
 public:
  Flags flags() const { return flags_; }
  FormatConversionChar conv() const {
    // Keep this field first in the struct . It generates better code when
    // accessing it when ConversionSpec is passed by value in registers.
    static_assert(offsetof(ConversionSpec, conv_) == 0, "");
    return conv_;
  }

  // Returns the specified width. If width is unspecfied, it returns a negative
  // value.
  int width() const { return width_; }
  // Returns the specified precision. If precision is unspecfied, it returns a
  // negative value.
  int precision() const { return precision_; }

  void set_flags(Flags f) { flags_ = f; }
  void set_conv(FormatConversionChar c) { conv_ = c; }
  void set_width(int w) { width_ = w; }
  void set_precision(int p) { precision_ = p; }
  void set_left(bool b) { flags_.left = b; }

 private:
  FormatConversionChar conv_ = FormatConversionChar::kNone;
  Flags flags_;
  int width_;
  int precision_;
};

constexpr uint64_t FormatConversionCharToConvValue(char conv) {
  return
#define CONV_SET_CASE(c)                                                     \
  conv == #c[0]                                                              \
      ? (uint64_t{1} << (1 + static_cast<uint8_t>(FormatConversionChar::c))) \
      :
      ABSL_CONVERSION_CHARS_EXPAND_(CONV_SET_CASE, )
#undef CONV_SET_CASE
                  conv == '*'
          ? 1
          : 0;
}

enum class Conv : uint64_t {
#define CONV_SET_CASE(c) c = FormatConversionCharToConvValue(#c[0]),
  ABSL_CONVERSION_CHARS_EXPAND_(CONV_SET_CASE, )
#undef CONV_SET_CASE

  // Used for width/precision '*' specification.
  star = FormatConversionCharToConvValue('*'),

  // Some predefined values:
  integral = d | i | u | o | x | X,
  floating = a | e | f | g | A | E | F | G,
  numeric = integral | floating,
  string = s,
  pointer = p
};

// Type safe OR operator.
// We need this for two reasons:
//  1. operator| on enums makes them decay to integers and the result is an
//     integer. We need the result to stay as an enum.
//  2. We use "enum class" which would not work even if we accepted the decay.
constexpr Conv operator|(Conv a, Conv b) {
  return Conv(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

// Get a conversion with a single character in it.
constexpr Conv ConversionCharToConv(char c) {
  return Conv(FormatConversionCharToConvValue(c));
}

// Checks whether `c` exists in `set`.
constexpr bool Contains(Conv set, char c) {
  return (static_cast<uint64_t>(set) & FormatConversionCharToConvValue(c)) != 0;
}

// Checks whether all the characters in `c` are contained in `set`
constexpr bool Contains(Conv set, Conv c) {
  return (static_cast<uint64_t>(set) & static_cast<uint64_t>(c)) ==
         static_cast<uint64_t>(c);
}

// Return type of the AbslFormatConvert() functions.
// The Conv template parameter is used to inform the framework of what
// conversion characters are supported by that AbslFormatConvert routine.
template <Conv C>
struct ConvertResult {
  static constexpr Conv kConv = C;
  bool value;
};
template <Conv C>
constexpr Conv ConvertResult<C>::kConv;

// Return capacity - used, clipped to a minimum of 0.
inline size_t Excess(size_t used, size_t capacity) {
  return used < capacity ? capacity - used : 0;
}

// Type alias for use during migration.
using ConversionChar = FormatConversionChar;

}  // namespace str_format_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STR_FORMAT_EXTENSION_H_
