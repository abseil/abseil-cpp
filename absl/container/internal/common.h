// Copyright 2018 The Abseil Authors.
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

#ifndef ABSL_CONTAINER_INTERNAL_CONTAINER_H_
#define ABSL_CONTAINER_INTERNAL_CONTAINER_H_

#include <type_traits>

#include "absl/meta/type_traits.h"

namespace absl {
namespace container_internal {

template <class, class = void>
struct IsTransparent : std::false_type {};
template <class T>
struct IsTransparent<T, absl::void_t<typename T::is_transparent>>
    : std::true_type {};

template <bool is_transparent>
struct KeyArg {
  // Transparent. Forward `K`.
  template <typename K, typename key_type>
  using type = K;
};

template <>
struct KeyArg<false> {
  // Not transparent. Always use `key_type`.
  template <typename K, typename key_type>
  using type = key_type;
};

}  // namespace container_internal
}   // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_CONTAINER_H_
