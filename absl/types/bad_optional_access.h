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

#ifndef ABSL_TYPES_BAD_OPTIONAL_ACCESS_H_
#define ABSL_TYPES_BAD_OPTIONAL_ACCESS_H_

#include <stdexcept>

namespace absl {

class bad_optional_access : public std::exception {
 public:
  bad_optional_access() = default;
  ~bad_optional_access() override;
  const char* what() const noexcept override;
};

namespace optional_internal {

// throw delegator
[[noreturn]] void throw_bad_optional_access();

}  // namespace optional_internal
}  // namespace absl

#endif  // ABSL_TYPES_BAD_OPTIONAL_ACCESS_H_
