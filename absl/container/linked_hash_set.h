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
// File: linked_hash_set.h
// -----------------------------------------------------------------------------
//
// This is a simple insertion-ordered set. It provides O(1) amortized
// insertions and lookups, as well as iteration over the set in the insertion
// order.
//
// This class is thread-compatible.
// This class is NOT exception-safe.
//
// Iterators point into the list and should be stable in the face of
// mutations, except for an iterator pointing to an element that was just
// deleted.
//
// This class supports heterogeneous lookups.

#ifndef ABSL_CONTAINER_LINKED_HASH_SET_H_
#define ABSL_CONTAINER_LINKED_HASH_SET_H_

#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/container/hash_container_defaults.h"
#include "absl/container/internal/raw_linked_hash_set.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <typename Key>
struct LinkedHashSetPolicy {
  using key_type = Key;
  using value_type = Key;
  using DefaultHash = absl::DefaultHashContainerHash<Key>;
  using DefaultEq = absl::DefaultHashContainerEq<Key>;
  using DefaultAlloc = std::allocator<Key>;
  static constexpr bool kIsMap = false;

  template <typename V>
  static const V& key(const V& v) {
    return v;
  }
};

}  // namespace container_internal

template <
    typename Key,
    typename KeyHash =
        typename container_internal::LinkedHashSetPolicy<Key>::DefaultHash,
    typename KeyEq =
        typename container_internal::LinkedHashSetPolicy<Key>::DefaultEq,
    typename Alloc =
        typename container_internal::LinkedHashSetPolicy<Key>::DefaultAlloc>
class linked_hash_set
    : public container_internal::raw_linked_hash_set<
          container_internal::LinkedHashSetPolicy<Key>, KeyHash, KeyEq, Alloc> {
  using Base = typename linked_hash_set::raw_linked_hash_set;
  template <class K>
  using key_arg = typename Base::template key_arg<K>;

 public:
  using typename Base::const_iterator;
  using typename Base::iterator;
  using typename Base::key_type;
  using typename Base::value_type;

  linked_hash_set() = default;
  using Base::Base;

  linked_hash_set& operator=(std::initializer_list<value_type> values) {
    Base::operator=(values);
    return *this;
  }

  using Base::insert;

  template <typename K = key_type>
  std::pair<iterator, bool> insert(const key_arg<K>& k) {
    return this->InsertValueAt(this->end(), k);
  }
  template <typename K = key_type,
            std::enable_if_t<!std::is_reference_v<K>, int> = 0>
  std::pair<iterator, bool> insert(key_arg<K>&& k) {
    return this->InsertValueAt(this->end(), std::move(k));
  }

  template <typename K = key_type,
            std::enable_if_t<
                !std::is_convertible_v<const key_arg<K>&, const_iterator> &&
                    !std::is_convertible_v<const key_arg<K>&, iterator>,
                int> = 0>
  iterator insert(const_iterator hint, const key_arg<K>& k) {
    return this->InsertValueAt(hint, k).first;
  }
  template <typename K = key_type,
            std::enable_if_t<
                !std::is_reference_v<K> &&
                    !std::is_convertible_v<key_arg<K>&&, const_iterator> &&
                    !std::is_convertible_v<key_arg<K>&&, iterator>,
                int> = 0>
  iterator insert(const_iterator hint, key_arg<K>&& k) {
    return this->InsertValueAt(hint, std::move(k)).first;
  }

  void insert(std::initializer_list<key_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    for (; first != last; ++first) insert(*first);
  }

  using Base::merge;

  void swap(linked_hash_set& other) noexcept { Base::swap(other); }
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_LINKED_HASH_SET_H_
