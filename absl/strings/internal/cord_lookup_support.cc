// Copyright 2025 The Abseil Authors.
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

#include "absl/strings/internal/cord_lookup_support.h"

#include "absl/hash/hash.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

size_t HashOfCord(const Cord& v) {
  absl::Hash<Cord> hasher;
  return hasher(v);
}

size_t HashOfCordWithSeed(const Cord& v, size_t seed) {
  absl::Hash<Cord> hasher;
  return absl::hash_internal::HashWithSeed().hash(hasher, v, seed);
}

bool CordEquals(const Cord& lhs, const Cord& rhs) { return lhs == rhs; }

bool CordEquals(const Cord& lhs, absl::string_view rhs) { return lhs == rhs; }

int CordCompare(const Cord& lhs, const Cord& rhs) { return lhs.Compare(rhs); }

int CordCompare(const Cord& lhs, absl::string_view rhs) {
  return lhs.Compare(rhs);
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
