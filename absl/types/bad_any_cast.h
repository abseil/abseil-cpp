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

#ifndef ABSL_TYPES_BAD_ANY_CAST_H_
#define ABSL_TYPES_BAD_ANY_CAST_H_

#include <typeinfo>

namespace absl {

////////////////////////
// [any.bad_any_cast] //
////////////////////////

// Objects of type bad_any_cast are thrown by a failed any_cast.
class bad_any_cast : public std::bad_cast {
 public:
  ~bad_any_cast() override;
  const char* what() const noexcept override;
};

//////////////////////////////////////////////
// Implementation-details beyond this point //
//////////////////////////////////////////////

namespace any_internal {

[[noreturn]] void ThrowBadAnyCast();

}  // namespace any_internal
}  // namespace absl

#endif  // ABSL_TYPES_BAD_ANY_CAST_H_
