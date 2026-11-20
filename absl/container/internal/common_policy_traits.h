// Copyright 2022 The Abseil Authors.
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

#ifndef ABSL_CONTAINER_INTERNAL_COMMON_POLICY_TRAITS_H_
#define ABSL_CONTAINER_INTERNAL_COMMON_POLICY_TRAITS_H_

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class Policy, class = void>
struct policy_trait_element_is_owner : std::false_type {};

template <class Policy>
struct policy_trait_element_is_owner<
    Policy,
    std::enable_if_t<!std::is_void_v<typename Policy::element_is_owner>>>
    : Policy::element_is_owner {};

// Defines how slots are initialized/destroyed/moved.
template <class Policy, class = void>
struct common_policy_traits {
  // The actual object stored in the container.
  using slot_type = typename Policy::slot_type;
  using reference = decltype(Policy::element(std::declval<slot_type*>()));
  using value_type = std::remove_reference_t<reference>;

  // PRECONDITION: `slot` is UNINITIALIZED
  // POSTCONDITION: `slot` is INITIALIZED
  template <class Alloc, class... Args>
  static void construct(Alloc* alloc, slot_type* slot, Args&&... args) {
    Policy::construct(alloc, slot, std::forward<Args>(args)...);
  }

  // PRECONDITION: `slot` is INITIALIZED
  // POSTCONDITION: `slot` is UNINITIALIZED
  // Returns std::true_type in case destroy is trivial.
  template <class Alloc>
  static auto destroy(Alloc* alloc, slot_type* slot) {
    return Policy::destroy(alloc, slot);
  }

  // Transfers the `old_slot` to `new_slot`. Any memory allocated by the
  // allocator inside `old_slot` to `new_slot` can be transferred.
  //
  // OPTIONAL: defaults to:
  //
  //     clone(new_slot, std::move(*old_slot));
  //     destroy(old_slot);
  //
  // PRECONDITION: `new_slot` is UNINITIALIZED and `old_slot` is INITIALIZED
  // POSTCONDITION: `new_slot` is INITIALIZED and `old_slot` is
  //                UNINITIALIZED
  template <class Alloc>
  static void transfer(Alloc* alloc, slot_type* new_slot, slot_type* old_slot) {
    transfer_impl(alloc, new_slot, old_slot);
  }

  // PRECONDITION: `slot` is INITIALIZED
  // POSTCONDITION: `slot` is INITIALIZED
  // Note: we use remove_const_t so that the two overloads have different args
  // in the case of sets with explicitly const value_types.
  template <class P = Policy>
  static auto element(std::remove_const_t<slot_type>* slot)
      -> decltype(P::element(slot)) {
    return P::element(slot);
  }
  template <class P = Policy>
  static auto element(const slot_type* slot) -> decltype(P::element(slot)) {
    return P::element(slot);
  }

  static constexpr bool transfer_uses_memcpy() {
    return std::is_same_v<decltype(transfer_impl<std::allocator<char>>(
                              nullptr, nullptr, nullptr)),
                          std::true_type>;
  }

  // Returns true if destroy is trivial and can be omitted.
  template <class Alloc>
  static constexpr bool destroy_is_trivial() {
    return std::is_same_v<decltype(destroy<Alloc>(nullptr, nullptr)),
                          std::true_type>;
  }

 private:
  template <class Alloc, class P = Policy, class = void>
  struct HasTransfer : std::false_type {};

  template <class Alloc, class P>
  struct HasTransfer<Alloc, P,
                     std::void_t<decltype(P::transfer(
                         std::declval<Alloc*>(), std::declval<slot_type*>(),
                         std::declval<slot_type*>()))>> : std::true_type {};

  // Policy::transfer returns std::true_type if transfer uses memcpy (e.g. in
  // node_slot_policy). The conditional_t in the else-if branch keeps value_type
  // dependent on Alloc for incomplete mapped types.
  template <class Alloc>
  static auto transfer_impl(Alloc* alloc, slot_type* new_slot,
                            slot_type* old_slot) {
    if constexpr (HasTransfer<Alloc>::value) {
      return Policy::transfer(alloc, new_slot, old_slot);
    } else if constexpr (absl::is_trivially_relocatable<std::conditional_t<
                             false, Alloc, value_type>>::value) {
      // TODO(b/247130232): remove casts after fixing warnings.
      // TODO(b/251814870): remove casts after fixing warnings.
      using MutableValue = std::remove_const_t<value_type>;
      std::memcpy(static_cast<void*>(std::launder(
                      const_cast<MutableValue*>(&element(new_slot)))),
                  static_cast<const void*>(&element(old_slot)),
                  sizeof(value_type));
      return std::true_type{};
    } else {
      construct(alloc, new_slot, std::move(element(old_slot)));
      destroy(alloc, old_slot);
    }
  }
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_COMMON_POLICY_TRAITS_H_
