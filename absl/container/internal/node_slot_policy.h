// Copyright 2018 The Abseil Authors.
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
// Adapts a policy for nodes.
//
// `Policy` may optionally customize allocation/deallocation and accounting via
// `new_element()`, `delete_element()`, and `element_space_used()`.

#ifndef ABSL_CONTAINER_INTERNAL_NODE_SLOT_POLICY_H_
#define ABSL_CONTAINER_INTERNAL_NODE_SLOT_POLICY_H_

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class Reference, class Policy>
struct node_slot_policy {
  static_assert(std::is_lvalue_reference_v<Reference>);

 private:
  using Elem = std::remove_cv_t<std::remove_reference_t<Reference>>;

 public:
  using slot_type = Elem*;

  template <class Alloc, class... Args>
  static slot_type new_element(Alloc* alloc, Args&&... args) {
    using ElemAlloc =
        typename std::allocator_traits<Alloc>::template rebind_alloc<Elem>;
    ElemAlloc elem_alloc(*alloc);
    slot_type res = std::allocator_traits<ElemAlloc>::allocate(elem_alloc, 1);
    std::allocator_traits<ElemAlloc>::construct(elem_alloc, res,
                                                std::forward<Args>(args)...);
    return res;
  }

  template <class Alloc>
  static void delete_element(Alloc* alloc, slot_type elem) {
    using ElemAlloc =
        typename std::allocator_traits<Alloc>::template rebind_alloc<Elem>;
    ElemAlloc elem_alloc(*alloc);
    std::allocator_traits<ElemAlloc>::destroy(elem_alloc, elem);
    std::allocator_traits<ElemAlloc>::deallocate(elem_alloc, elem, 1);
  }

  static size_t element_space_used(const Elem*) { return sizeof(Elem); }

  template <class Alloc, class... Args>
  static void construct(Alloc* alloc, slot_type* slot, Args&&... args) {
    *slot = Policy::new_element(alloc, std::forward<Args>(args)...);
  }

  template <class Alloc>
  static void destroy(Alloc* alloc, slot_type* slot) {
    Policy::delete_element(alloc, *slot);
  }

  // Returns true_type to indicate that transfer can use memcpy.
  template <class Alloc>
  static std::true_type transfer(Alloc*, slot_type* new_slot,
                                 slot_type* old_slot) {
    *new_slot = *old_slot;
    return {};
  }

  static size_t space_used(const slot_type* slot) {
    if (slot == nullptr) return Policy::element_space_used(nullptr);
    return Policy::element_space_used(*slot);
  }

  static Reference element(slot_type* slot) { return **slot; }
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_NODE_SLOT_POLICY_H_
