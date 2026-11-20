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

#ifndef ABSL_CONTAINER_INTERNAL_RAW_LINKED_HASH_SET_H_
#define ABSL_CONTAINER_INTERNAL_RAW_LINKED_HASH_SET_H_

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <list>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/internal/common.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <typename Derived, typename Policy, bool kIsMap = Policy::kIsMap>
class LinkedHashNodeHandleAccess {
 public:
  using key_type = typename Policy::key_type;
  using mapped_type = typename Policy::mapped_type;

  const key_type& key() const {
    return static_cast<const Derived*>(this)->list_.front().first;
  }
  mapped_type& mapped() {
    return static_cast<Derived*>(this)->list_.front().second;
  }
};

template <typename Derived, typename Policy>
class LinkedHashNodeHandleAccess<Derived, Policy, /*kIsMap=*/false> {
 public:
  using value_type = typename Policy::value_type;

  value_type& value() { return static_cast<Derived*>(this)->list_.front(); }
};

// Owning node handle shared by `linked_hash_set` and `linked_hash_map`.
template <typename Policy, typename Alloc>
class LinkedHashNodeHandle
    : public LinkedHashNodeHandleAccess<LinkedHashNodeHandle<Policy, Alloc>,
                                        Policy> {
  using ListType = std::list<typename Policy::value_type, Alloc>;

 public:
  using allocator_type = Alloc;

  constexpr LinkedHashNodeHandle() noexcept = default;
  LinkedHashNodeHandle(LinkedHashNodeHandle&& nh) noexcept = default;
  ~LinkedHashNodeHandle() = default;
  LinkedHashNodeHandle& operator=(LinkedHashNodeHandle&& node) noexcept =
      default;
  bool empty() const noexcept { return list_.empty(); }
  explicit operator bool() const noexcept { return !empty(); }
  allocator_type get_allocator() const { return list_.get_allocator(); }
  void swap(LinkedHashNodeHandle& nh) noexcept { list_.swap(nh.list_); }
  friend void swap(LinkedHashNodeHandle& a, LinkedHashNodeHandle& b) noexcept {
    a.swap(b);
  }

 private:
  friend class LinkedHashNodeHandleAccess<LinkedHashNodeHandle, Policy>;
  template <typename, typename, typename, typename>
  friend class raw_linked_hash_set;

  explicit LinkedHashNodeHandle(ListType list) : list_(std::move(list)) {}
  ListType list_;
};

// Shared container implementation backing linked hash sets and maps.
template <typename Policy, typename Hash, typename Eq, typename Alloc>
class raw_linked_hash_set {
 protected:
  static constexpr bool kIsTransparent =
      IsTransparent<Eq>::value && IsTransparent<Hash>::value;
  using KeyArgImpl = KeyArg<kIsTransparent>;

 public:
  using key_type = typename Policy::key_type;
  using value_type = typename Policy::value_type;
  using hasher = Hash;
  using key_equal = Eq;
  using allocator_type = Alloc;
  using difference_type = ptrdiff_t;

 protected:
  template <class K>
  using key_arg = typename KeyArgImpl::template Select<K, key_type>;

  using ListType = std::list<value_type, allocator_type>;

  template <class Fn>
  class Wrapped {
    template <typename K>
    static const K& ToKey(const K& k) {
      return k;
    }
    static const key_type& ToKey(typename ListType::const_iterator it) {
      return Policy::key(*it);
    }
    static const key_type& ToKey(typename ListType::iterator it) {
      return Policy::key(*it);
    }

    Fn fn_;

    friend raw_linked_hash_set;

   public:
    using is_transparent = void;

    Wrapped() = default;
    explicit Wrapped(Fn fn) : fn_(std::move(fn)) {}

    template <class... Args>
    auto operator()(Args&&... args) const
        -> decltype(this->fn_(ToKey(args)...)) {
      return fn_(ToKey(args)...);
    }
  };

  using SetType =
      absl::flat_hash_set<typename ListType::iterator, Wrapped<hasher>,
                          Wrapped<key_equal>, allocator_type>;

 public:
  using iterator =
      std::conditional_t<Policy::kIsMap, typename ListType::iterator,
                         typename ListType::const_iterator>;
  using const_iterator = typename ListType::const_iterator;
  using reverse_iterator =
      std::conditional_t<Policy::kIsMap, typename ListType::reverse_iterator,
                         typename ListType::const_reverse_iterator>;
  using const_reverse_iterator = typename ListType::const_reverse_iterator;
  using reference = typename ListType::reference;
  using const_reference = typename ListType::const_reference;
  using size_type = typename ListType::size_type;
  using pointer = typename std::allocator_traits<allocator_type>::pointer;
  using const_pointer =
      typename std::allocator_traits<allocator_type>::const_pointer;
  using node_type = LinkedHashNodeHandle<Policy, allocator_type>;
  using insert_return_type = InsertReturnType<iterator, node_type>;

  raw_linked_hash_set() = default;

  explicit raw_linked_hash_set(size_t reservation_size,
                               const hasher& hash = hasher(),
                               const key_equal& eq = key_equal(),
                               const allocator_type& alloc = allocator_type())
      : set_(reservation_size, Wrapped<hasher>(hash), Wrapped<key_equal>(eq),
             alloc),
        list_(alloc) {}

  raw_linked_hash_set(size_t reservation_size, const hasher& hash,
                      const allocator_type& alloc)
      : raw_linked_hash_set(reservation_size, hash, key_equal(), alloc) {}

  raw_linked_hash_set(size_t reservation_size, const allocator_type& alloc)
      : raw_linked_hash_set(reservation_size, hasher(), key_equal(), alloc) {}

  explicit raw_linked_hash_set(const allocator_type& alloc)
      : raw_linked_hash_set(0, hasher(), key_equal(), alloc) {}

  template <class InputIt>
  raw_linked_hash_set(InputIt first, InputIt last, size_t reservation_size = 0,
                      const hasher& hash = hasher(),
                      const key_equal& eq = key_equal(),
                      const allocator_type& alloc = allocator_type())
      : raw_linked_hash_set(reservation_size, hash, eq, alloc) {
    for (; first != last; ++first) {
      if constexpr ((Policy::kIsMap || !kIsTransparent) &&
                    !std::is_same_v<std::decay_t<decltype(*first)>,
                                    value_type>) {
        value_type elem = *first;
        InsertValueAt(list_.cend(), std::move(elem));
      } else {
        InsertValueAt(list_.cend(), *first);
      }
    }
  }

  template <class InputIt>
  raw_linked_hash_set(InputIt first, InputIt last, size_t reservation_size,
                      const hasher& hash, const allocator_type& alloc)
      : raw_linked_hash_set(first, last, reservation_size, hash, key_equal(),
                            alloc) {}

  template <class InputIt>
  raw_linked_hash_set(InputIt first, InputIt last, size_t reservation_size,
                      const allocator_type& alloc)
      : raw_linked_hash_set(first, last, reservation_size, hasher(),
                            key_equal(), alloc) {}

  template <class InputIt>
  raw_linked_hash_set(InputIt first, InputIt last, const allocator_type& alloc)
      : raw_linked_hash_set(first, last, /*reservation_size=*/0, hasher(),
                            key_equal(), alloc) {}

  raw_linked_hash_set(std::initializer_list<value_type> init,
                      size_t reservation_size = 0,
                      const hasher& hash = hasher(),
                      const key_equal& eq = key_equal(),
                      const allocator_type& alloc = allocator_type())
      : raw_linked_hash_set(init.begin(), init.end(), reservation_size, hash,
                            eq, alloc) {}

  raw_linked_hash_set(std::initializer_list<value_type> init,
                      size_t reservation_size, const hasher& hash,
                      const allocator_type& alloc)
      : raw_linked_hash_set(init, reservation_size, hash, key_equal(), alloc) {}

  raw_linked_hash_set(std::initializer_list<value_type> init,
                      size_t reservation_size, const allocator_type& alloc)
      : raw_linked_hash_set(init, reservation_size, hasher(), key_equal(),
                            alloc) {}

  raw_linked_hash_set(std::initializer_list<value_type> init,
                      const allocator_type& alloc)
      : raw_linked_hash_set(init, /*reservation_size=*/0, hasher(), key_equal(),
                            alloc) {}

  raw_linked_hash_set(const raw_linked_hash_set& other)
      : raw_linked_hash_set(0, other.hash_function(), other.key_eq(),
                            other.get_allocator()) {
    reserve(other.size());
    CopyFrom(other);
  }

  raw_linked_hash_set(const raw_linked_hash_set& other,
                      const allocator_type& alloc)
      : raw_linked_hash_set(0, other.hash_function(), other.key_eq(), alloc) {
    reserve(other.size());
    CopyFrom(other);
  }

  raw_linked_hash_set(raw_linked_hash_set&& other) noexcept
      : set_(std::move(other.set_)), list_(std::move(other.list_)) {
    // Since the list and set must agree for other to end up "valid",
    // explicitly clear them.
    other.set_.clear();
    other.list_.clear();
  }

  raw_linked_hash_set(raw_linked_hash_set&& other, const allocator_type& alloc)
      : raw_linked_hash_set(0, other.hash_function(), other.key_eq(), alloc) {
    if (get_allocator() == other.get_allocator()) {
      *this = std::move(other);
    } else {
      reserve(other.size());
      CopyFrom(std::move(other));
    }
  }

  raw_linked_hash_set& operator=(const raw_linked_hash_set& other) {
    if (this != &other) {
      // Make a new set, with other's hash/eq/alloc.
      set_ = SetType(0, other.set_.hash_function(), other.set_.key_eq(),
                     other.get_allocator());
      set_.reserve(other.size());
      // Copy the list, with other's allocator.
      list_ = ListType(other.get_allocator());
      CopyFrom(other);
    }
    return *this;
  }

  raw_linked_hash_set& operator=(raw_linked_hash_set&& other) noexcept {
    if (this != &other) {
      using AllocTraits = std::allocator_traits<allocator_type>;
      if constexpr (AllocTraits::propagate_on_container_move_assignment::
                        value) {
        set_ = std::move(other.set_);
        list_ = std::move(other.list_);
        other.clear();
      } else if (AllocTraits::is_always_equal::value ||
                 get_allocator() == other.get_allocator()) {
        // The allocators are equal, so the list can take over other's nodes.
        // std::list's move assignment can not be used here: with an allocator
        // that does not propagate on move assignment it element-assigns, which
        // is ill-formed for value types that are not assignable, such as
        // linked_hash_map's std::pair<const K, V>.
        clear();
        set_ = std::move(other.set_);
        list_.splice(list_.end(), other.list_);
        other.set_.clear();
      } else if constexpr (!AllocTraits::is_always_equal::value) {
        // The list can not take over other's nodes, so move each element into
        // a new node allocated with this container's allocator and rebuild the
        // index over them, adopting other's hash and equality state. This is
        // the same pattern as raw_hash_set::move_assign.
        clear();
        set_ = SetType(other.size(), other.set_.hash_function(),
                       other.set_.key_eq(), get_allocator());
        CopyFrom(std::move(other));
      }
    }
    return *this;
  }

  raw_linked_hash_set& operator=(std::initializer_list<value_type> values) {
    clear();
    reserve(values.size());
    for (const value_type& v : values) InsertValueAt(list_.cend(), v);
    return *this;
  }

  // Derive size from set_, as list::size might be O(N).
  size_type size() const { return set_.size(); }
  size_type max_size() const noexcept { return ~size_type{}; }
  bool empty() const { return set_.empty(); }

  // Iteration is list-like, in insertion order.
  // These are all forwarded.
  iterator begin() { return list_.begin(); }
  iterator end() { return list_.end(); }
  const_iterator begin() const { return list_.begin(); }
  const_iterator end() const { return list_.end(); }
  const_iterator cbegin() const { return list_.cbegin(); }
  const_iterator cend() const { return list_.cend(); }
  reverse_iterator rbegin() { return list_.rbegin(); }
  reverse_iterator rend() { return list_.rend(); }
  const_reverse_iterator rbegin() const { return list_.rbegin(); }
  const_reverse_iterator rend() const { return list_.rend(); }
  const_reverse_iterator crbegin() const { return list_.crbegin(); }
  const_reverse_iterator crend() const { return list_.crend(); }
  reference front() { return list_.front(); }
  reference back() { return list_.back(); }
  const_reference front() const { return list_.front(); }
  const_reference back() const { return list_.back(); }

  void pop_front() { erase(begin()); }
  void pop_back() { erase(std::prev(end())); }

  ABSL_ATTRIBUTE_REINITIALIZES void clear() {
    set_.clear();
    list_.clear();
  }

  void reserve(size_t n) { set_.reserve(n); }
  size_t capacity() const { return set_.capacity(); }
  size_t bucket_count() const { return set_.bucket_count(); }
  float load_factor() const { return set_.load_factor(); }

  hasher hash_function() const { return set_.hash_function().fn_; }
  key_equal key_eq() const { return set_.key_eq().fn_; }
  allocator_type get_allocator() const { return list_.get_allocator(); }

  template <class K = key_type>
  size_type erase(const key_arg<K>& key) {
    auto found = set_.find(key);
    if (found == set_.end()) return 0;
    auto list_it = *found;
    // Erase set entry first since it refers to the list element.
    set_.erase(found);
    list_.erase(list_it);
    return 1;
  }

  iterator erase(const_iterator position) {
    auto found = set_.find(position);
    assert(*found == position);
    set_.erase(found);
    return list_.erase(position);
  }

  template <bool kMap = Policy::kIsMap, std::enable_if_t<kMap, int> = 0>
  iterator erase(iterator position) {
    return erase(static_cast<const_iterator>(position));
  }

  template <bool kMap = Policy::kIsMap, std::enable_if_t<kMap, int> = 0>
  iterator erase(iterator first, iterator last) {
    while (first != last) first = erase(first);
    return first;
  }

  iterator erase(const_iterator first, const_iterator last) {
    while (first != last) first = erase(first);
    if constexpr (Policy::kIsMap) {
      if (first == end()) return end();
      return *set_.find(first);
    } else {
      return first;
    }
  }

  template <class K = key_type>
  iterator find(const key_arg<K>& key) {
    auto found = set_.find(key);
    if (found == set_.end()) return end();
    return *found;
  }

  template <class K = key_type>
  const_iterator find(const key_arg<K>& key) const {
    auto found = set_.find(key);
    if (found == set_.end()) return end();
    return *found;
  }

  template <class K = key_type>
  size_type count(const key_arg<K>& key) const {
    return contains(key) ? 1 : 0;
  }
  template <class K = key_type>
  bool contains(const key_arg<K>& key) const {
    return set_.contains(key);
  }

  template <class K = key_type>
  std::pair<iterator, iterator> equal_range(const key_arg<K>& key) {
    auto iter = set_.find(key);
    if (iter == set_.end()) return {end(), end()};
    return {*iter, std::next(*iter)};
  }

  template <class K = key_type>
  std::pair<const_iterator, const_iterator> equal_range(
      const key_arg<K>& key) const {
    auto iter = set_.find(key);
    if (iter == set_.end()) return {end(), end()};
    return {*iter, std::next(*iter)};
  }

  insert_return_type insert(node_type&& node) {
    if (node.empty()) return {end(), false, node_type()};
    if (auto [set_itr, inserted] = set_.emplace(node.list_.begin()); inserted) {
      list_.splice(list_.end(), node.list_);
      return {*set_itr, true, node_type()};
    } else {
      return {*set_itr, false, std::move(node)};
    }
  }

  iterator insert(const_iterator, node_type&& node) {
    return insert(std::move(node)).position;
  }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return EmplaceAt(list_.end(), std::forward<Args>(args)...);
  }

  template <typename... Args>
  iterator emplace_hint(const_iterator hint, Args&&... args) {
    return EmplaceAt(Policy::kIsMap ? list_.cend() : hint,
                     std::forward<Args>(args)...)
        .first;
  }

  template <typename OtherHash, typename OtherEq>
  void merge(raw_linked_hash_set<Policy, OtherHash, OtherEq, Alloc>& src) {
    auto itr = src.begin();
    while (itr != src.end()) {
      if (contains(Policy::key(*itr))) {
        ++itr;
      } else {
        insert(src.extract(itr++));
      }
    }
  }

  template <typename OtherHash, typename OtherEq>
  void merge(raw_linked_hash_set<Policy, OtherHash, OtherEq, Alloc>&& src) {
    merge(src);
  }

  node_type extract(const_iterator position) {
    set_.erase(Policy::key(*position));
    ListType extracted_node_list(get_allocator());
    extracted_node_list.splice(extracted_node_list.end(), list_, position);
    return node_type(std::move(extracted_node_list));
  }

  template <class K = key_type,
            std::enable_if_t<!std::is_same_v<K, iterator>, int> = 0>
  node_type extract(const key_arg<K>& key) {
    auto node = set_.extract(key);
    if (node.empty()) return node_type();
    ListType extracted_node_list(get_allocator());
    extracted_node_list.splice(extracted_node_list.end(), list_, node.value());
    return node_type(std::move(extracted_node_list));
  }

  void swap(raw_linked_hash_set& other) noexcept {
    using std::swap;
    swap(set_, other.set_);
    swap(list_, other.list_);
  }

  friend bool operator==(const raw_linked_hash_set& a,
                         const raw_linked_hash_set& b) {
    if (a.size() != b.size()) return false;
    const raw_linked_hash_set* outer = &a;
    const raw_linked_hash_set* inner = &b;
    if (outer->capacity() > inner->capacity()) std::swap(outer, inner);
    for (const value_type& elem : *outer) {
      if constexpr (Policy::kIsMap) {
        auto it = inner->find(elem.first);
        if (it == inner->end()) return false;
        if (it->second != elem.second) return false;
      } else {
        if (!inner->contains(elem)) return false;
      }
    }
    return true;
  }

  friend bool operator!=(const raw_linked_hash_set& a,
                         const raw_linked_hash_set& b) {
    return !(a == b);
  }

  void rehash(size_t n) { set_.rehash(n); }

 protected:
  template <typename U>
  std::pair<iterator, bool> InsertValueAt(const_iterator hint, U&& val) {
    bool constructed = false;
    auto set_iter = set_.lazy_emplace(Policy::key(val), [&](const auto& ctor) {
      constructed = true;
      ctor(list_.emplace(hint, std::forward<U>(val)));
    });
    return {*set_iter, constructed};
  }

  template <typename... Args>
  std::pair<iterator, bool> EmplaceAt(const_iterator hint, Args&&... args) {
    ListType node_donor(get_allocator());
    auto list_iter =
        node_donor.emplace(node_donor.end(), std::forward<Args>(args)...);
    auto ins = set_.insert(list_iter);
    if (!ins.second) return {*ins.first, false};
    list_.splice(hint, node_donor, list_iter);
    return {list_iter, true};
  }

  template <typename K, typename... Args>
  std::pair<iterator, bool> LazyEmplaceMap(K&& key, Args&&... args) {
    bool constructed = false;
    auto set_iter = set_.lazy_emplace(
        key, [this, &constructed, &key, &args...](const auto& ctor) {
          auto list_iter =
              list_.emplace(list_.end(), std::piecewise_construct,
                            std::forward_as_tuple(std::forward<K>(key)),
                            std::forward_as_tuple(std::forward<Args>(args)...));
          constructed = true;
          ctor(list_iter);
        });
    return {*set_iter, constructed};
  }

  template <typename H, typename E>
  void SpliceFrom(raw_linked_hash_set<Policy, H, E, Alloc>& list,
                  const_iterator it) {
    if constexpr (std::is_same_v<raw_linked_hash_set<Policy, H, E, Alloc>,
                                 raw_linked_hash_set>) {
      if (&list == this) {
        list_.splice(list_.end(), list.list_, it);
        return;
      }
    }
    insert(list.extract(it));
  }

 private:
  template <typename Other>
  void CopyFrom(Other&& other) {
    for (auto& elem : other.list_) {
      set_.insert(list_.insert(list_.end(), std::move(elem)));
    }
    if constexpr (!std::is_const_v<std::remove_reference_t<Other>>) {
      // When moving from `other`, `std::move(elem)` above mutates the elements
      // of `other.list_` in place, invalidating `other.set_`'s index over them.
      // Clear `other` so its `set_` and `list_` remain consistent and empty.
      other.clear();
    }
    assert(set_.size() == list_.size());
  }

  // The set component, used for speedy lookups.
  SetType set_;

  // The list component, used for maintaining insertion order.
  ListType list_;
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_RAW_LINKED_HASH_SET_H_
