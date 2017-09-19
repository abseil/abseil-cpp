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

#ifndef ABSL_STRINGS_ASCII_CTYPE_H_
#define ABSL_STRINGS_ASCII_CTYPE_H_

#include "absl/strings/ascii.h"

inline bool ascii_isalpha(unsigned char c) {
  return absl::ascii_isalpha(c);
}
inline bool ascii_isalnum(unsigned char c) {
  return absl::ascii_isalnum(c);
}
inline bool ascii_isspace(unsigned char c) {
  return absl::ascii_isspace(c);
}
inline bool ascii_ispunct(unsigned char c) {
  return absl::ascii_ispunct(c);
}
inline bool ascii_isblank(unsigned char c) {
  return absl::ascii_isblank(c);
}
inline bool ascii_iscntrl(unsigned char c) {
  return absl::ascii_iscntrl(c);
}
inline bool ascii_isxdigit(unsigned char c) {
  return absl::ascii_isxdigit(c);
}
inline bool ascii_isdigit(unsigned char c) {
  return absl::ascii_isdigit(c);
}
inline bool ascii_isprint(unsigned char c) {
  return absl::ascii_isprint(c);
}
inline bool ascii_isgraph(unsigned char c) {
  return absl::ascii_isgraph(c);
}
inline bool ascii_isupper(unsigned char c) {
  return absl::ascii_isupper(c);
}
inline bool ascii_islower(unsigned char c) {
  return absl::ascii_islower(c);
}
inline bool ascii_isascii(unsigned char c) {
  return absl::ascii_isascii(c);
}
inline char ascii_tolower(unsigned char c) {
  return absl::ascii_tolower(c);
}
inline char ascii_toupper(unsigned char c) {
  return absl::ascii_toupper(c);
}

#endif  // ABSL_STRINGS_ASCII_CTYPE_H_
