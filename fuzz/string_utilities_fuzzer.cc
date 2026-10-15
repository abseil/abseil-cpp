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

#include <fuzzer/FuzzedDataProvider.h>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"


extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	FuzzedDataProvider fuzzed_data(data, size);
	float float_value = fuzzed_data.ConsumeFloatingPoint<float>();
	double double_value = fuzzed_data.ConsumeFloatingPoint<double>();
	int int_value = fuzzed_data.ConsumeIntegral<int>();
	bool bool_value = fuzzed_data.ConsumeBool();
	std::string str1 = fuzzed_data.ConsumeRandomLengthString();
	std::string str2 = fuzzed_data.ConsumeRemainingBytesAsString();

	std::string float_str = absl::StrFormat("%g", float_value);
	std::string double_str = absl::StrFormat("%g", double_value);
	std::string int_str = absl::StrFormat("%d", int_value);
	std::string bool_str = absl::StrFormat("%d", bool_value);
	
	if (!absl::SimpleAtof(float_str, &float_value))
		return 0;
	if (!absl::SimpleAtod(double_str, &double_value))
		return 0;
	if (!absl::SimpleAtoi(int_str, &int_value))
		return 0;
	if (!absl::SimpleAtob(bool_str, &bool_value))
		return 0;

	absl::StrAppend(&str1, str2);
	std::string str_result = absl::StrCat(str1, float_value, double_value, int_value, bool_value);
	std::vector<std::string> v = absl::StrSplit(str_result, ".");
	absl::StrJoin(v, ".");
	return 0;
}