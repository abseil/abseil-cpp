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

// This file contains functions that remove a defined part from the std::string,
// i.e., strip the std::string.

#include "absl/strings/strip.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

// ----------------------------------------------------------------------
// ReplaceCharacters
//    Replaces any occurrence of the character 'remove' (or the characters
//    in 'remove') with the character 'replace_with'.
// ----------------------------------------------------------------------
void ReplaceCharacters(char* str, size_t len, absl::string_view remove,
                       char replace_with) {
  for (char* end = str + len; str != end; ++str) {
    if (remove.find(*str) != absl::string_view::npos) {
      *str = replace_with;
    }
  }
}

void ReplaceCharacters(std::string* s, absl::string_view remove, char replace_with) {
  for (char& ch : *s) {
    if (remove.find(ch) != absl::string_view::npos) {
      ch = replace_with;
    }
  }
}

bool StripTrailingNewline(std::string* s) {
  if (!s->empty() && (*s)[s->size() - 1] == '\n') {
    if (s->size() > 1 && (*s)[s->size() - 2] == '\r')
      s->resize(s->size() - 2);
    else
      s->resize(s->size() - 1);
    return true;
  }
  return false;
}

// ----------------------------------------------------------------------
// Misc. stripping routines
// ----------------------------------------------------------------------
void StripCurlyBraces(std::string* s) {
  return StripBrackets('{', '}', s);
}

void StripBrackets(char left, char right, std::string* s) {
  std::string::iterator opencurly = std::find(s->begin(), s->end(), left);
  while (opencurly != s->end()) {
    std::string::iterator closecurly = std::find(opencurly, s->end(), right);
    if (closecurly == s->end()) return;
    opencurly = s->erase(opencurly, closecurly + 1);
    opencurly = std::find(opencurly, s->end(), left);
  }
}

void StripMarkupTags(std::string* s) {
  std::string::iterator output = std::find(s->begin(), s->end(), '<');
  std::string::iterator input = output;
  while (input != s->end()) {
    if (*input == '<') {
      input = std::find(input, s->end(), '>');
      if (input == s->end()) break;
      ++input;
    } else {
      *output++ = *input++;
    }
  }
  s->resize(output - s->begin());
}

std::string OutputWithMarkupTagsStripped(const std::string& s) {
  std::string result(s);
  StripMarkupTags(&result);
  return result;
}

ptrdiff_t TrimStringLeft(std::string* s, absl::string_view remove) {
  size_t i = 0;
  while (i < s->size() && memchr(remove.data(), (*s)[i], remove.size())) {
    ++i;
  }
  if (i > 0) s->erase(0, i);
  return i;
}

ptrdiff_t TrimStringRight(std::string* s, absl::string_view remove) {
  size_t i = s->size(), trimmed = 0;
  while (i > 0 && memchr(remove.data(), (*s)[i - 1], remove.size())) {
    --i;
  }
  if (i < s->size()) {
    trimmed = s->size() - i;
    s->erase(i);
  }
  return trimmed;
}

// Unfortunately, absl::string_view does not have erase, so we've to replicate
// the implementation with remove_prefix()/remove_suffix()
ptrdiff_t TrimStringLeft(absl::string_view* s, absl::string_view remove) {
  size_t i = 0;
  while (i < s->size() && memchr(remove.data(), (*s)[i], remove.size())) {
    ++i;
  }
  if (i > 0) s->remove_prefix(i);
  return i;
}

ptrdiff_t TrimStringRight(absl::string_view* s, absl::string_view remove) {
  size_t i = s->size(), trimmed = 0;
  while (i > 0 && memchr(remove.data(), (*s)[i - 1], remove.size())) {
    --i;
  }
  if (i < s->size()) {
    trimmed = s->size() - i;
    s->remove_suffix(trimmed);
  }
  return trimmed;
}

// ----------------------------------------------------------------------
// Various removal routines
// ----------------------------------------------------------------------
ptrdiff_t strrm(char* str, char c) {
  char* src;
  char* dest;
  for (src = dest = str; *src != '\0'; ++src)
    if (*src != c) *(dest++) = *src;
  *dest = '\0';
  return dest - str;
}

ptrdiff_t memrm(char* str, ptrdiff_t strlen, char c) {
  char* src;
  char* dest;
  for (src = dest = str; strlen-- > 0; ++src)
    if (*src != c) *(dest++) = *src;
  return dest - str;
}

ptrdiff_t strrmm(char* str, const char* chars) {
  char* src;
  char* dest;
  for (src = dest = str; *src != '\0'; ++src) {
    bool skip = false;
    for (const char* c = chars; *c != '\0'; c++) {
      if (*src == *c) {
        skip = true;
        break;
      }
    }
    if (!skip) *(dest++) = *src;
  }
  *dest = '\0';
  return dest - str;
}

ptrdiff_t strrmm(std::string* str, const std::string& chars) {
  size_t str_len = str->length();
  size_t in_index = str->find_first_of(chars);
  if (in_index == std::string::npos) return str_len;

  size_t out_index = in_index++;

  while (in_index < str_len) {
    char c = (*str)[in_index++];
    if (chars.find(c) == std::string::npos) (*str)[out_index++] = c;
  }

  str->resize(out_index);
  return out_index;
}

// ----------------------------------------------------------------------
// StripDupCharacters
//    Replaces any repeated occurrence of the character 'dup_char'
//    with single occurrence.  e.g.,
//       StripDupCharacters("a//b/c//d", '/', 0) => "a/b/c/d"
//    Return the number of characters removed
// ----------------------------------------------------------------------
ptrdiff_t StripDupCharacters(std::string* s, char dup_char, ptrdiff_t start_pos) {
  if (start_pos < 0) start_pos = 0;

  // remove dups by compaction in-place
  ptrdiff_t input_pos = start_pos;   // current reader position
  ptrdiff_t output_pos = start_pos;  // current writer position
  const ptrdiff_t input_end = s->size();
  while (input_pos < input_end) {
    // keep current character
    const char curr_char = (*s)[input_pos];
    if (output_pos != input_pos)  // must copy
      (*s)[output_pos] = curr_char;
    ++input_pos;
    ++output_pos;

    if (curr_char == dup_char) {  // skip subsequent dups
      while ((input_pos < input_end) && ((*s)[input_pos] == dup_char))
        ++input_pos;
    }
  }
  const ptrdiff_t num_deleted = input_pos - output_pos;
  s->resize(s->size() - num_deleted);
  return num_deleted;
}

// ----------------------------------------------------------------------
// TrimRunsInString
//    Removes leading and trailing runs, and collapses middle
//    runs of a set of characters into a single character (the
//    first one specified in 'remove').  Useful for collapsing
//    runs of repeated delimiters, whitespace, etc.  E.g.,
//    TrimRunsInString(&s, " :,()") removes leading and trailing
//    delimiter chars and collapses and converts internal runs
//    of delimiters to single ' ' characters, so, for example,
//    "  a:(b):c  " -> "a b c"
//    "first,last::(area)phone, ::zip" -> "first last area phone zip"
// ----------------------------------------------------------------------
void TrimRunsInString(std::string* s, absl::string_view remove) {
  std::string::iterator dest = s->begin();
  std::string::iterator src_end = s->end();
  for (std::string::iterator src = s->begin(); src != src_end;) {
    if (remove.find(*src) == absl::string_view::npos) {
      *(dest++) = *(src++);
    } else {
      // Skip to the end of this run of chars that are in 'remove'.
      for (++src; src != src_end; ++src) {
        if (remove.find(*src) == absl::string_view::npos) {
          if (dest != s->begin()) {
            // This is an internal run; collapse it.
            *(dest++) = remove[0];
          }
          *(dest++) = *(src++);
          break;
        }
      }
    }
  }
  s->erase(dest, src_end);
}

// ----------------------------------------------------------------------
// RemoveNullsInString
//    Removes any internal \0 characters from the std::string.
// ----------------------------------------------------------------------
void RemoveNullsInString(std::string* s) {
  s->erase(std::remove(s->begin(), s->end(), '\0'), s->end());
}
