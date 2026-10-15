// Copyright 2020 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////

#include <string> 

#include "absl/strings/escaping.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	std::string str (reinterpret_cast<const char*>(data), size);
	std::string escaped, unescaped;
	escaped = absl::CHexEscape(str);
	absl::CUnescape(escaped, &unescaped);
	if (str != unescaped)
		abort();

	escaped = absl::CEscape(str);
	absl::CUnescape(escaped, &unescaped);
	if (str != unescaped)
		abort();

	escaped = absl::Utf8SafeCEscape(str);
	absl::CUnescape(escaped, &unescaped);
	if (str != unescaped)
		abort();
	
	escaped = absl::Utf8SafeCHexEscape(str);
	absl::CUnescape(escaped, &unescaped);
	if (str != unescaped)
		abort();
	
	std::string encoded, decoded;
	absl::Base64Escape(str, &encoded);
	absl::Base64Unescape(encoded, &decoded);
	if (str != unescaped)
		abort();

	absl::WebSafeBase64Escape(str, &encoded);
	absl::WebSafeBase64Unescape(encoded, &decoded);
	if (str != decoded)
		abort();

	std::string hex_result, bytes_result;
	hex_result = absl::BytesToHexString(str);
	bytes_result = absl::HexStringToBytes(hex_result);
	if (str != decoded)
		abort();

	return 0;
}