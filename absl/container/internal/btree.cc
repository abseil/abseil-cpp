// Copyright 2026 The Abseil Authors.
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

#include "absl/container/internal/btree.h"

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

ABSL_CONST_INIT ABSL_DLL const BtreeEmptyNode<uint8_t> kBtreeEmptyNode8{};
ABSL_CONST_INIT ABSL_DLL const BtreeEmptyNode<uint16_t> kBtreeEmptyNode16{};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
