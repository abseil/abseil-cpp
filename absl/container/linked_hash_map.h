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
// File: linked_hash_map.h
// -----------------------------------------------------------------------------
//
// This is a simple insertion-ordered map. It provides O(1) amortized
// insertions and lookups, as well as iteration over the map in the insertion
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

#ifndef ABSL_CONTAINER_LINKED_HASH_MAP_H_
#define ABSL_CONTAINER_LINKED_HASH_MAP_H_

#include <initializer_list>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/base/throw_delegate.h"
#include "absl/container/internal/raw_linked_hash_set.h"  // IWYU pragma: export

namespace absl {
ABSL_NAMESPACE_BEGIN

template <
    typename Key, typename Value,
    typename KeyHash =
        typename container_internal::LinkedHashMapPolicy<Key,
                                                         Value>::DefaultHash,
    typename KeyEq =
        typename container_internal::LinkedHashMapPolicy<Key, Value>::DefaultEq,
    typename Alloc = typename container_internal::LinkedHashMapPolicy<
        Key, Value>::DefaultAlloc>
class linked_hash_map
    : public container_internal::InstantiateRawLinkedHashSet<
          container_internal::LinkedHashMapPolicy<Key, Value>, KeyHash, KeyEq,
          Alloc>::type {
  using Policy = container_internal::LinkedHashMapPolicy<Key, Value>;
  using Base = typename linked_hash_map::raw_linked_hash_set;
  template <class K>
  using key_arg = typename Base::template key_arg<K>;

 public:
  using mapped_type = Value;
  using typename Base::const_iterator;
  using typename Base::iterator;
  using typename Base::key_type;
  using typename Base::value_type;

  linked_hash_map() = default;
  using Base::Base;

  linked_hash_map& operator=(std::initializer_list<value_type> values) {
    Base::operator=(values);
    return *this;
  }

  template <class K = key_type>
  mapped_type& at(const key_arg<K>& key) {
    auto it = this->find(key);
    if (ABSL_PREDICT_FALSE(it == this->end())) {
      ThrowStdOutOfRange("absl::linked_hash_map::at");
    }
    return it->second;
  }

  template <class K = key_type>
  const mapped_type& at(const key_arg<K>& key) const {
    return const_cast<linked_hash_map*>(this)->at(key);
  }

  template <class K = key_type>
  mapped_type& operator[](const key_arg<K>& key) {
    return this->LazyEmplaceMap(key).first->second;
  }

  template <class K = key_type,
            std::enable_if_t<!std::is_reference_v<K>, int> = 0>
  mapped_type& operator[](key_arg<K>&& key) {
    return this->LazyEmplaceMap(std::forward<key_arg<K>>(key)).first->second;
  }

  using Base::insert;

  std::pair<iterator, bool> insert(const value_type& v) {
    return this->InsertValueAt(this->end(), v);
  }
  std::pair<iterator, bool> insert(value_type&& v) {
    return this->InsertValueAt(this->end(), std::move(v));
  }

  iterator insert(const_iterator, const value_type& v) {
    return insert(v).first;
  }
  iterator insert(const_iterator, value_type&& v) {
    return insert(std::move(v)).first;
  }

  // The last two template parameters ensure that both arguments are rvalues
  // (lvalue arguments are handled by the overloads below). This is necessary
  // for supporting bitfield arguments.
  //
  //   union { int n : 1; };
  //   linked_hash_map<int, int> m;
  //   m.insert_or_assign(n, n);
  template <class K = key_type, class V = mapped_type,
            std::enable_if_t<!std::is_reference_v<K> && !std::is_reference_v<V>,
                             int> = 0>
  std::pair<iterator, bool> insert_or_assign(key_arg<K>&& k, V&& v) {
    return InsertOrAssignInternal(std::forward<key_arg<K>>(k),
                                  std::forward<V>(v));
  }

  template <class K = key_type, class V = mapped_type,
            std::enable_if_t<!std::is_reference_v<K>, int> = 0>
  std::pair<iterator, bool> insert_or_assign(key_arg<K>&& k, const V& v) {
    return InsertOrAssignInternal(std::forward<key_arg<K>>(k), v);
  }

  template <class K = key_type, class V = mapped_type,
            std::enable_if_t<!std::is_reference_v<V>, int> = 0>
  std::pair<iterator, bool> insert_or_assign(const key_arg<K>& k, V&& v) {
    return InsertOrAssignInternal(k, std::forward<V>(v));
  }

  template <class K = key_type, class V = mapped_type>
  std::pair<iterator, bool> insert_or_assign(const key_arg<K>& k, const V& v) {
    return InsertOrAssignInternal(k, v);
  }

  template <class K = key_type, class V = mapped_type,
            std::enable_if_t<!std::is_reference_v<K> && !std::is_reference_v<V>,
                             int> = 0>
  iterator insert_or_assign(const_iterator, key_arg<K>&& k, V&& v) {
    return insert_or_assign(std::forward<key_arg<K>>(k), std::forward<V>(v))
        .first;
  }

  template <class K = key_type, class V = mapped_type,
            std::enable_if_t<!std::is_reference_v<K>, int> = 0>
  iterator insert_or_assign(const_iterator, key_arg<K>&& k, const V& v) {
    return insert_or_assign(std::forward<key_arg<K>>(k), v).first;
  }

  template <class K = key_type, class V = mapped_type,
            std::enable_if_t<!std::is_reference_v<V>, int> = 0>
  iterator insert_or_assign(const_iterator, const key_arg<K>& k, V&& v) {
    return insert_or_assign(k, std::forward<V>(v)).first;
  }

  template <class K = key_type, class V = mapped_type>
  iterator insert_or_assign(const_iterator, const key_arg<K>& k, const V& v) {
    return insert_or_assign(k, v).first;
  }

  template <class K = key_type, class... Args,
            std::enable_if_t<!std::is_reference_v<K>, int> = 0>
  iterator try_emplace(const_iterator, key_arg<K>&& k, Args&&... args) {
    return try_emplace(std::forward<key_arg<K>>(k), std::forward<Args>(args)...)
        .first;
  }

  template <class K = key_type, typename... Args,
            std::enable_if_t<!std::is_reference_v<K>, int> = 0>
  std::pair<iterator, bool> try_emplace(key_arg<K>&& key, Args&&... args) {
    return this->LazyEmplaceMap(std::forward<key_arg<K>>(key),
                                std::forward<Args>(args)...);
  }

  using Base::merge;

  template <
      typename... OtherParams,
      typename = std::enable_if_t<std::is_same_v<
          Alloc, typename container_internal::raw_linked_hash_set<
                     Policy, OtherParams...>::allocator_type>>>
  void splice(
      const_iterator,
      container_internal::raw_linked_hash_set<Policy, OtherParams...>& list,
      const_iterator it) {
    this->SpliceFrom(list, it);
  }

  template <class K = key_type, typename... Args>
  std::pair<iterator, bool> try_emplace(const key_arg<K>& key, Args&&... args) {
    return this->LazyEmplaceMap(key, std::forward<Args>(args)...);
  }

  template <class K = key_type, typename... Args>
  iterator try_emplace(const_iterator, const key_arg<K>& key, Args&&... args) {
    return this->LazyEmplaceMap(key, std::forward<Args>(args)...).first;
  }

  void swap(linked_hash_map& other) noexcept { Base::swap(other); }

 private:
  template <class K, class V>
  std::pair<iterator, bool> InsertOrAssignInternal(K&& k, V&& v) {
    auto [it, inserted] =
        this->LazyEmplaceMap(std::forward<K>(k), std::forward<V>(v));
    if (!inserted) {
      // NOLINTNEXTLINE(bugprone-use-after-move)
      it->second = std::forward<V>(v);
    }
    return {it, inserted};
  }
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_LINKED_HASH_MAP_H_
