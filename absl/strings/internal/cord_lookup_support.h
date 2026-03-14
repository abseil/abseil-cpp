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
//
// -----------------------------------------------------------------------------
// File: cord_lookup_support.h
// -----------------------------------------------------------------------------
//
// Functions to support heterogeneous lookup of `absL::Cord` values within
// associative containers having string keys.

#ifndef ABSL_STRINGS_INTERNAL_CORD_LOOKUP_SUPPORT_H_
#define ABSL_STRINGS_INTERNAL_CORD_LOOKUP_SUPPORT_H_

#include <stdint.h>

#include "absl/base/config.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

class Cord;

namespace cord_internal {

size_t HashOfCord(const Cord& v);

size_t HashOfCordWithSeed(const Cord& v, size_t seed);

bool CordEquals(const Cord& lhs, const Cord& rhs);

bool CordEquals(const Cord& lhs, absl::string_view rhs);

int CordCompare(const Cord& lhs, const Cord& rhs);

int CordCompare(const Cord& lhs, absl::string_view rhs);

}  // namespace cord_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORD_LOOKUP_SUPPORT_H_
