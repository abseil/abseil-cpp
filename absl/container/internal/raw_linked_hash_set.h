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
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/container/hash_container_defaults.h"
#include "absl/container/internal/common.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/raw_hash_set.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <typename Policy, typename... Params>
class raw_linked_hash_set;

template <typename Policy, typename Hash, typename Eq, typename Alloc>
struct InstantiateRawLinkedHashSet {
  using type = typename ApplyWithoutDefaultSuffix<
      raw_linked_hash_set,
      TypeList<void, typename Policy::DefaultHash, typename Policy::DefaultEq,
               typename Policy::DefaultAlloc>,
      TypeList<Policy, Hash, Eq, Alloc>>::type;
};

// Intrusive doubly-linked list pointers storing node addresses, or a shifted
// sentinel address for the list end.
struct LinkedListLink {
  void* prev;
  void* next;
};

// Heap-allocated list node placing the element storage union at offset 0,
// followed by any alignment padding and the intrusive list pointers:
//
//   [ ValueStorage | padding | LinkedListLink (prev, next) ]
template <typename ValueType>
struct LinkedListNode {
  // Union deferring element construction and destruction to the allocator.
  union ValueStorage {
    ValueStorage() noexcept {}
    ~ValueStorage() noexcept {}
    ValueType value;
  };

  LinkedListNode() noexcept = default;
  ~LinkedListNode() noexcept = default;

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
  static constexpr size_t LinkOffset() noexcept {
    static_assert(offsetof(LinkedListNode, storage) == 0);
    return offsetof(LinkedListNode, link);
  }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

  // Steps from a node address or shifted sentinel address to its list link.
  static LinkedListLink* LinkOf(void* ptr) noexcept {
    return reinterpret_cast<LinkedListLink*>(
        reinterpret_cast<uintptr_t>(ptr) + LinkOffset());
  }
  static const LinkedListLink* LinkOf(const void* ptr) noexcept {
    return reinterpret_cast<const LinkedListLink*>(
        reinterpret_cast<uintptr_t>(ptr) + LinkOffset());
  }

  // Returns the shifted address whose link offset lands on the sentinel.
  static void* SentinelPtr(const LinkedListLink* sentinel) noexcept {
    return reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(sentinel) - LinkOffset());
  }

  static void LinkBefore(void* pos, LinkedListNode* node) noexcept {
    LinkedListLink* pos_link = LinkOf(pos);
    node->link.prev = pos_link->prev;
    node->link.next = pos;
    LinkOf(pos_link->prev)->next = node;
    pos_link->prev = node;
  }

  static void Unlink(LinkedListNode* node) noexcept {
    LinkOf(node->link.prev)->next = node->link.next;
    LinkOf(node->link.next)->prev = node->link.prev;
  }

  // Allocates a node and constructs its element in place via the rebound
  // allocator, leaving the link pointers uninitialized for the caller to link.
  template <typename Alloc, typename... Args>
  static LinkedListNode* New(const Alloc& alloc, Args&&... args) {
    using NodeAlloc = typename std::allocator_traits<
        Alloc>::template rebind_alloc<LinkedListNode>;
    using NodeAllocTraits = std::allocator_traits<NodeAlloc>;
    NodeAlloc node_alloc(alloc);
    LinkedListNode* node = NodeAllocTraits::allocate(node_alloc, 1);
    ::new (static_cast<void*>(node)) LinkedListNode();
    NodeAllocTraits::construct(node_alloc, std::addressof(node->storage.value),
                               std::forward<Args>(args)...);
    return node;
  }

  // Destroys the element and deallocates the node via the rebound allocator.
  // The node must already be unlinked from any list.
  template <typename Alloc>
  static void Delete(const Alloc& alloc, LinkedListNode* node) noexcept {
    using NodeAlloc = typename std::allocator_traits<
        Alloc>::template rebind_alloc<LinkedListNode>;
    using NodeAllocTraits = std::allocator_traits<NodeAlloc>;
    NodeAlloc node_alloc(alloc);
    NodeAllocTraits::destroy(node_alloc, std::addressof(node->storage.value));
    node->~LinkedListNode();
    NodeAllocTraits::deallocate(node_alloc, node, 1);
  }

  ValueStorage storage;
  LinkedListLink link;
};

// Circular head/tail sentinel owning the endpoints of an intrusive list.
template <typename ValueType>
struct LinkedListSentinel {
  using Node = LinkedListNode<ValueType>;

  LinkedListLink head;

  LinkedListSentinel() noexcept { Reset(); }
  LinkedListSentinel(const LinkedListSentinel&) = delete;
  LinkedListSentinel& operator=(const LinkedListSentinel&) = delete;

  void* EndPtr() const noexcept { return Node::SentinelPtr(&head); }

  void Reset() noexcept {
    void* end = EndPtr();
    head.prev = end;
    head.next = end;
  }

  bool empty() const noexcept { return head.next == EndPtr(); }

  // Moves all nodes from another distinct sentinel into this empty sentinel.
  void TakeFrom(LinkedListSentinel& other) noexcept {
    if (other.empty()) {
      Reset();
      return;
    }
    void* end = EndPtr();
    head.next = other.head.next;
    head.prev = other.head.prev;
    Node::LinkOf(head.next)->prev = end;
    Node::LinkOf(head.prev)->next = end;
    other.Reset();
  }

  // Swaps the node chains of two distinct sentinels.
  void Swap(LinkedListSentinel& other) noexcept {
    LinkedListSentinel tmp;
    tmp.TakeFrom(*this);
    TakeFrom(other);
    other.TakeFrom(tmp);
  }
};

// Trivially copyable bidirectional iterator over intrusive list nodes.
template <typename ValueType, bool IsConst>
class LinkedListIterator {
  using VoidPtr = std::conditional_t<IsConst, const void*, void*>;
  using Node = LinkedListNode<ValueType>;
  using NodePtr = std::conditional_t<IsConst, const Node*, Node*>;

 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = ValueType;
  using difference_type = ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const ValueType*, ValueType*>;
  using reference = std::conditional_t<IsConst, const ValueType&, ValueType&>;

  constexpr LinkedListIterator() noexcept = default;

  // Implicitly converts mutable iterators to const iterators.
  template <bool OtherConst,
            std::enable_if_t<IsConst && !OtherConst, int> = 0>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr LinkedListIterator(
      const LinkedListIterator<ValueType, OtherConst>& other) noexcept
      : ptr_(other.ptr_) {}

  reference operator*() const noexcept {
    return static_cast<NodePtr>(ptr_)->storage.value;
  }
  pointer operator->() const noexcept {
    return std::addressof(static_cast<NodePtr>(ptr_)->storage.value);
  }

  LinkedListIterator& operator++() noexcept {
    ptr_ = Node::LinkOf(ptr_)->next;
    return *this;
  }
  LinkedListIterator operator++(int) noexcept {
    LinkedListIterator tmp = *this;
    ptr_ = Node::LinkOf(ptr_)->next;
    return tmp;
  }
  LinkedListIterator& operator--() noexcept {
    ptr_ = Node::LinkOf(ptr_)->prev;
    return *this;
  }
  LinkedListIterator operator--(int) noexcept {
    LinkedListIterator tmp = *this;
    ptr_ = Node::LinkOf(ptr_)->prev;
    return tmp;
  }

  friend bool operator==(LinkedListIterator a, LinkedListIterator b) noexcept {
    return a.ptr_ == b.ptr_;
  }
  friend bool operator!=(LinkedListIterator a, LinkedListIterator b) noexcept {
    return a.ptr_ != b.ptr_;
  }

 private:
  template <typename, bool>
  friend class LinkedListIterator;
  template <typename, typename...>
  friend class raw_linked_hash_set;

  explicit constexpr LinkedListIterator(VoidPtr ptr) noexcept : ptr_(ptr) {}

  VoidPtr ptr_ = nullptr;
};

// Non-owning SwissTable policy storing pointers whose offset 0 is the key,
// sharing node-table lookup and hashing instantiations with node hash
// containers.
template <class Key>
struct LinkedHashIndexPolicy {
  using slot_type = void*;
  using key_type = Key;
  using init_type = void*;

  using DefaultHash = DefaultHashContainerHash<Key>;
  using DefaultEq = DefaultHashContainerEq<Key>;
  using DefaultAlloc = std::allocator<char>;
  using SlotEq = SlotEqFn<Key, /*kDerefSlot=*/true>;

  template <class Allocator>
  static void construct(Allocator*, slot_type* slot, void* ptr) {
    *slot = ptr;
  }

  // Nodes are owned by the list, so slot destruction is a no-op.
  template <class Allocator>
  static std::true_type destroy(Allocator*, slot_type*) {
    return {};
  }

  // Slots hold raw pointers and relocate trivially.
  template <class Allocator>
  static std::true_type transfer(Allocator*, slot_type* new_slot,
                                 slot_type* old_slot) {
    *new_slot = *old_slot;
    return {};
  }

  static slot_type& element(slot_type* slot) { return *slot; }
  static const slot_type& element(const slot_type* slot) { return *slot; }

  // Invokes the callback with the dereferenced key and raw slot pointer.
  template <class F>
  static auto apply(F&& f, void* ptr)
      -> decltype(std::forward<F>(f)(*static_cast<const Key*>(ptr), ptr)) {
    return std::forward<F>(f)(*static_cast<const Key*>(ptr), ptr);
  }

  // Nodes are owned separately from the hash table slots.
  static size_t space_used(const slot_type*) { return 0; }

  // Shares the type-erased dereferencing hash function with node hash maps.
  template <class Hash, bool kIsAbsl, size_t kSeedShift>
  static constexpr HashSlotFn get_hash_slot_fn() {
    return GetTypeErasedDerefAndApplyToSlotFn<Hash, Key, kIsAbsl, kSeedShift>();
  }
};

template <typename Policy, typename... Params>
class LinkedHashNodeHandle;

template <typename Policy, typename Alloc>
struct InstantiateLinkedHashNodeHandle {
  using type = typename ApplyWithoutDefaultSuffix<
      LinkedHashNodeHandle, TypeList<void, typename Policy::DefaultAlloc>,
      TypeList<Policy, Alloc>>::type;
};

template <typename Derived, typename Policy, bool kIsMap = Policy::kIsMap>
class LinkedHashNodeHandleAccess {
 public:
  using key_type = typename Policy::key_type;
  using mapped_type = typename Policy::mapped_type;

  const key_type& key() const {
    return static_cast<const Derived*>(this)->node_->storage.value.first;
  }
  mapped_type& mapped() const {
    return static_cast<const Derived*>(this)->node_->storage.value.second;
  }
};

template <typename Derived, typename Policy>
class LinkedHashNodeHandleAccess<Derived, Policy, /*kIsMap=*/false> {
 public:
  using value_type = typename Policy::value_type;

  value_type& value() const {
    return static_cast<const Derived*>(this)->node_->storage.value;
  }
};

// Owning node handle for linked hash containers, independent of hash and
// equality functor types so nodes can be transferred across heterogeneous
// container specializations.
template <typename Policy, typename... Params>
class LinkedHashNodeHandle
    : public LinkedHashNodeHandleAccess<LinkedHashNodeHandle<Policy, Params...>,
                                        Policy> {
  using Node = LinkedListNode<typename Policy::value_type>;
  using Alloc = GetFromListOr<typename Policy::DefaultAlloc, 0, Params...>;

  static_assert(
      std::is_same_v<
          typename InstantiateLinkedHashNodeHandle<Policy, Alloc>::type,
          LinkedHashNodeHandle>,
      "Redundant template parameters were passed. Use "
      "InstantiateLinkedHashNodeHandle<> instead");

 public:
  using allocator_type = Alloc;

  constexpr LinkedHashNodeHandle() noexcept = default;
  LinkedHashNodeHandle(LinkedHashNodeHandle&& other) noexcept
      : node_(std::exchange(other.node_, nullptr)),
        alloc_(std::move(other.alloc_)) {
    other.alloc_.reset();
  }
  ~LinkedHashNodeHandle() { Destroy(); }
  LinkedHashNodeHandle& operator=(LinkedHashNodeHandle&& other) noexcept {
    if (this == &other) return *this;
    Destroy();
    if (!other.empty()) {
      node_ = std::exchange(other.node_, nullptr);
      alloc_.emplace(std::move(*other.alloc_));
      other.alloc_.reset();
    }
    return *this;
  }

  bool empty() const noexcept { return node_ == nullptr; }
  explicit operator bool() const noexcept { return !empty(); }
  allocator_type get_allocator() const { return *alloc_; }

  void swap(LinkedHashNodeHandle& other) noexcept {
    if (this == &other) return;
    using std::swap;
    swap(node_, other.node_);
    swap(alloc_, other.alloc_);
  }
  friend void swap(LinkedHashNodeHandle& a, LinkedHashNodeHandle& b) noexcept {
    a.swap(b);
  }

 private:
  friend class LinkedHashNodeHandleAccess<LinkedHashNodeHandle, Policy>;
  template <typename, typename...>
  friend class raw_linked_hash_set;

  LinkedHashNodeHandle(Node* node, const allocator_type& alloc)
      : node_(node), alloc_(alloc) {}

  Node* Release() noexcept {
    alloc_.reset();
    return std::exchange(node_, nullptr);
  }

  void Destroy() noexcept {
    if (node_ != nullptr) {
      Node::Delete(*alloc_, node_);
      node_ = nullptr;
      alloc_.reset();
    }
  }

  Node* node_ = nullptr;
  std::optional<allocator_type> alloc_;
};

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

template <typename Key, typename Value>
struct LinkedHashMapPolicy {
  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<const key_type, mapped_type>;
  using DefaultHash = absl::DefaultHashContainerHash<Key>;
  using DefaultEq = absl::DefaultHashContainerEq<Key>;
  using DefaultAlloc = std::allocator<value_type>;
  static constexpr bool kIsMap = true;

  static const key_type& key(const value_type& v) { return v.first; }
};

// Shared container implementation backing linked hash sets and maps.
template <typename Policy, typename... Params>
class raw_linked_hash_set {
  using Hash = GetFromListOr<typename Policy::DefaultHash, 0, Params...>;
  using Eq = GetFromListOr<typename Policy::DefaultEq, 1, Params...>;
  using Alloc = GetFromListOr<typename Policy::DefaultAlloc, 2, Params...>;

  static_assert(
      std::is_same_v<
          typename InstantiateRawLinkedHashSet<Policy, Hash, Eq, Alloc>::type,
          raw_linked_hash_set>,
      "Redundant template parameters were passed. Use "
      "InstantiateRawLinkedHashSet<> instead");

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
  using size_type = typename std::allocator_traits<allocator_type>::size_type;
  using pointer = typename std::allocator_traits<allocator_type>::pointer;
  using const_pointer =
      typename std::allocator_traits<allocator_type>::const_pointer;
  using reference = value_type&;
  using const_reference = const value_type&;
  using iterator = LinkedListIterator<value_type, /*IsConst=*/!Policy::kIsMap>;
  using const_iterator = LinkedListIterator<value_type, /*IsConst=*/true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using node_type =
      typename InstantiateLinkedHashNodeHandle<Policy, allocator_type>::type;
  using insert_return_type = InsertReturnType<iterator, node_type>;

 protected:
  template <class K>
  using key_arg = typename KeyArgImpl::template Select<K, key_type>;

  using Node = LinkedListNode<value_type>;
  using CharAlloc = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<char>;
  using SetType =
      typename InstantiateRawHashSet<LinkedHashIndexPolicy<key_type>, hasher,
                                     key_equal, CharAlloc>::type;

 public:
  raw_linked_hash_set() = default;

  explicit raw_linked_hash_set(size_t reservation_size,
                               const hasher& hash = hasher(),
                               const key_equal& eq = key_equal(),
                               const allocator_type& alloc = allocator_type())
      : set_(reservation_size, hash, eq, CharAlloc(alloc)) {}

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
    insert(first, last);
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
      : set_(std::move(other.set_)) {
    // Reset the moved-from hash index so the source remains valid and empty.
    other.set_.clear();
    sentinel_.TakeFrom(other.sentinel_);
  }

  raw_linked_hash_set(raw_linked_hash_set&& other, const allocator_type& alloc)
      : raw_linked_hash_set(0, other.hash_function(), other.key_eq(), alloc) {
    if (get_allocator() == other.get_allocator()) {
      set_ = std::move(other.set_);
      other.set_.clear();
      sentinel_.TakeFrom(other.sentinel_);
    } else {
      reserve(other.size());
      CopyFrom(std::move(other));
    }
  }

  ~raw_linked_hash_set() {
    if constexpr (Policy::kIsMap) {
      static_assert(memory_internal::OffsetOf<value_type>::kFirst == 0);
    }
    void* cur = sentinel_.head.next;
    void* end = sentinel_.EndPtr();
    sentinel_.Reset();
    DeleteList(cur, end);
  }

  raw_linked_hash_set& operator=(const raw_linked_hash_set& other) {
    if (this != &other) {
      // Destroy existing nodes before the hash index may replace its allocator.
      clear();
      const SetType empty_set(0, other.hash_function(), other.key_eq(),
                              other.set_.get_allocator());
      set_ = empty_set;
      set_.reserve(other.size());
      CopyFrom(other);
    }
    return *this;
  }

  raw_linked_hash_set& operator=(raw_linked_hash_set&& other) noexcept {
    if (this != &other) {
      using AllocTraits = std::allocator_traits<allocator_type>;
      constexpr bool kStealUnconditionally =
          AllocTraits::propagate_on_container_move_assignment::value ||
          AllocTraits::is_always_equal::value;
      if (kStealUnconditionally || get_allocator() == other.get_allocator()) {
        // Destroy existing nodes before the hash index may replace its
        // allocator.
        clear();
        set_ = std::move(other.set_);
        other.set_.clear();
        sentinel_.TakeFrom(other.sentinel_);
      } else if constexpr (!kStealUnconditionally) {
        // The allocators are unequal and do not propagate, so move each
        // element into a new node allocated with this container's allocator
        // and rebuild the index over them, adopting other's hash and equality.
        clear();
        set_ = SetType(0, other.hash_function(), other.key_eq(),
                       set_.get_allocator());
        set_.reserve(other.size());
        CopyFrom(std::move(other));
      }
    }
    return *this;
  }

  raw_linked_hash_set& operator=(std::initializer_list<value_type> values) {
    clear();
    reserve(values.size());
    insert(values.begin(), values.end());
    return *this;
  }

  size_type size() const { return set_.size(); }
  size_type max_size() const noexcept { return ~size_type{}; }
  bool empty() const { return set_.empty(); }

  // Iteration is list-like, in insertion order.
  iterator begin() { return iterator(sentinel_.head.next); }
  iterator end() { return iterator(sentinel_.EndPtr()); }
  const_iterator begin() const { return const_iterator(sentinel_.head.next); }
  const_iterator end() const { return const_iterator(sentinel_.EndPtr()); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }
  reverse_iterator rbegin() { return reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend() const { return rend(); }

  reference front() {
    assert(!empty());
    return static_cast<Node*>(sentinel_.head.next)->storage.value;
  }
  reference back() {
    assert(!empty());
    return static_cast<Node*>(sentinel_.head.prev)->storage.value;
  }
  const_reference front() const {
    assert(!empty());
    return static_cast<const Node*>(sentinel_.head.next)->storage.value;
  }
  const_reference back() const {
    assert(!empty());
    return static_cast<const Node*>(sentinel_.head.prev)->storage.value;
  }

  void pop_front() { erase(begin()); }
  void pop_back() { erase(std::prev(end())); }

  ABSL_ATTRIBUTE_REINITIALIZES void clear() {
    void* cur = sentinel_.head.next;
    void* end = sentinel_.EndPtr();
    sentinel_.Reset();
    set_.clear();
    DeleteList(cur, end);
  }

  void reserve(size_t n) { set_.reserve(n); }
  size_t capacity() const { return set_.capacity(); }
  size_t bucket_count() const { return set_.bucket_count(); }
  float load_factor() const { return set_.load_factor(); }

  hasher hash_function() const { return set_.hash_function(); }
  key_equal key_eq() const { return set_.key_eq(); }
  allocator_type get_allocator() const {
    return allocator_type(set_.get_allocator());
  }

  template <class K = key_type>
  size_type erase(const key_arg<K>& key) {
    auto found = set_.find(key);
    if (found == set_.end()) return 0;
    Node* node = static_cast<Node*>(*found);
    // Erase the hash entry before destroying the node it references.
    set_.erase(found);
    Node::Unlink(node);
    Node::Delete(set_.get_allocator(), node);
    return 1;
  }

  iterator erase(const_iterator position) {
    Node* node = static_cast<Node*>(RawPtr(position));
    auto found = set_.find(Policy::key(node->storage.value));
    assert(found != set_.end() && *found == node);
    set_.erase(found);
    void* next = node->link.next;
    Node::Unlink(node);
    Node::Delete(set_.get_allocator(), node);
    return iterator(next);
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
    return iterator(RawPtr(first));
  }

  template <class K = key_type>
  iterator find(const key_arg<K>& key) {
    auto found = set_.find(key);
    if (found == set_.end()) return end();
    return iterator(*found);
  }

  template <class K = key_type>
  const_iterator find(const key_arg<K>& key) const {
    auto found = set_.find(key);
    if (found == set_.end()) return end();
    return const_iterator(*found);
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
    auto iter = find(key);
    if (iter == end()) return {end(), end()};
    return {iter, std::next(iter)};
  }

  template <class K = key_type>
  std::pair<const_iterator, const_iterator> equal_range(
      const key_arg<K>& key) const {
    auto iter = find(key);
    if (iter == end()) return {end(), end()};
    return {iter, std::next(iter)};
  }

  void insert(std::initializer_list<value_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    for (; first != last; ++first) InsertRangeElement(*first);
  }

  insert_return_type insert(node_type&& node) {
    if (node.empty()) return {end(), false, node_type()};
    assert(get_allocator() == node.get_allocator());
    auto [set_iter, inserted] = set_.insert(node.node_);
    if (!inserted) {
      return {iterator(*set_iter), false, std::move(node)};
    }
    Node* n = node.Release();
    Node::LinkBefore(sentinel_.EndPtr(), n);
    return {iterator(n), true, node_type()};
  }

  iterator insert(const_iterator, node_type&& node) {
    return insert(std::move(node)).position;
  }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return EmplaceAt(end(), std::forward<Args>(args)...);
  }

  template <typename... Args>
  iterator emplace_hint(const_iterator hint, Args&&... args) {
    return EmplaceAt(Policy::kIsMap ? end() : hint, std::forward<Args>(args)...)
        .first;
  }

  template <
      typename... OtherParams,
      typename = std::enable_if_t<std::is_same_v<
          Alloc, typename raw_linked_hash_set<
                     Policy, OtherParams...>::allocator_type>>>
  void merge(raw_linked_hash_set<Policy, OtherParams...>& src) {
    auto itr = src.begin();
    while (itr != src.end()) {
      if (contains(Policy::key(*itr))) {
        ++itr;
      } else {
        insert(src.extract(itr++));
      }
    }
  }

  template <
      typename... OtherParams,
      typename = std::enable_if_t<std::is_same_v<
          Alloc, typename raw_linked_hash_set<
                     Policy, OtherParams...>::allocator_type>>>
  void merge(raw_linked_hash_set<Policy, OtherParams...>&& src) {
    merge(src);
  }

  node_type extract(const_iterator position) {
    Node* node = static_cast<Node*>(RawPtr(position));
    auto found = set_.find(Policy::key(node->storage.value));
    assert(found != set_.end() && *found == node);
    set_.erase(found);
    Node::Unlink(node);
    return node_type(node, get_allocator());
  }

  template <class K = key_type,
            std::enable_if_t<!std::is_same_v<K, iterator>, int> = 0>
  node_type extract(const key_arg<K>& key) {
    auto found = set_.find(key);
    if (found == set_.end()) return node_type();
    Node* node = static_cast<Node*>(*found);
    set_.erase(found);
    Node::Unlink(node);
    return node_type(node, get_allocator());
  }

  void swap(raw_linked_hash_set& other) noexcept {
    if (this == &other) return;
    set_.swap(other.set_);
    sentinel_.Swap(other.sentinel_);
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
  template <typename, typename...>
  friend class raw_linked_hash_set;

  static void* RawPtr(const_iterator it) noexcept {
    return const_cast<void*>(it.ptr_);
  }

  template <typename U>
  std::pair<iterator, bool> InsertValueAt(const_iterator hint, U&& val) {
    bool constructed = false;
    auto set_iter = set_.lazy_emplace(Policy::key(val), [&](const auto& ctor) {
      Node* node = Node::New(set_.get_allocator(), std::forward<U>(val));
      constructed = true;
      ctor(node);
      Node::LinkBefore(RawPtr(hint), node);
    });
    return {iterator(*set_iter), constructed};
  }

  template <typename... Args>
  std::pair<iterator, bool> EmplaceAt(const_iterator hint, Args&&... args) {
    Node* node = Node::New(set_.get_allocator(), std::forward<Args>(args)...);
    auto [set_iter, inserted] = set_.insert(node);
    if (!inserted) {
      Node::Delete(set_.get_allocator(), node);
      return {iterator(*set_iter), false};
    }
    Node::LinkBefore(RawPtr(hint), node);
    return {iterator(node), true};
  }

  template <typename K, typename... Args>
  std::pair<iterator, bool> LazyEmplaceMap(K&& key, Args&&... args) {
    bool constructed = false;
    auto set_iter = set_.lazy_emplace(
        key, [this, &constructed, &key, &args...](const auto& ctor) {
          Node* node =
              Node::New(set_.get_allocator(), std::piecewise_construct,
                        std::forward_as_tuple(std::forward<K>(key)),
                        std::forward_as_tuple(std::forward<Args>(args)...));
          constructed = true;
          ctor(node);
          Node::LinkBefore(sentinel_.EndPtr(), node);
        });
    return {iterator(*set_iter), constructed};
  }

  template <typename... OtherParams>
  void SpliceFrom(raw_linked_hash_set<Policy, OtherParams...>& list,
                  const_iterator it) {
    if constexpr (std::is_same_v<raw_linked_hash_set<Policy, OtherParams...>,
                                 raw_linked_hash_set>) {
      if (&list == this) {
        Node* node = static_cast<Node*>(RawPtr(it));
        if (node != sentinel_.head.prev) {
          Node::Unlink(node);
          Node::LinkBefore(sentinel_.EndPtr(), node);
        }
        return;
      }
    }
    insert(list.extract(it));
  }

 private:
  void DeleteList(void* cur, void* end) noexcept {
    while (cur != end) {
      Node* node = static_cast<Node*>(cur);
      cur = node->link.next;
      Node::Delete(set_.get_allocator(), node);
    }
  }

  template <typename Elem>
  void InsertRangeElement(Elem&& elem) {
    if constexpr (Policy::kIsMap) {
      InsertMapRangeElement(std::forward<Elem>(elem));
    } else {
      InsertSetRangeElement(std::forward<Elem>(elem));
    }
  }

  void InsertMapRangeElement(const value_type& v) { InsertValueAt(end(), v); }
  void InsertMapRangeElement(value_type&& v) {
    InsertValueAt(end(), std::move(v));
  }

  template <typename K = key_type>
  void InsertSetRangeElement(const key_arg<K>& k) {
    InsertValueAt(end(), k);
  }
  template <typename K = key_type,
            std::enable_if_t<!std::is_reference_v<K>, int> = 0>
  void InsertSetRangeElement(key_arg<K>&& k) {
    InsertValueAt(end(), std::move(k));
  }

  template <typename Other>
  void CopyFrom(Other&& other) {
    constexpr bool kIsMove = !std::is_const_v<std::remove_reference_t<Other>>;
    void* other_end = other.sentinel_.EndPtr();
    for (void* cur = other.sentinel_.head.next; cur != other_end;
         cur = static_cast<Node*>(cur)->link.next) {
      Node* src_node = static_cast<Node*>(cur);
      if constexpr (kIsMove) {
        InsertValueAt(end(), std::move(src_node->storage.value));
      } else {
        InsertValueAt(end(), std::as_const(src_node->storage.value));
      }
    }
    if constexpr (kIsMove) {
      // Moving elements in place invalidates the source hash index, so clear
      // the source container to leave it consistent and empty.
      other.clear();
    }
  }

  // The hash index storing pointers to each node's value.
  SetType set_;

  // Circular sentinel for the intrusive doubly-linked list maintaining
  // insertion order.
  LinkedListSentinel<value_type> sentinel_;
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_RAW_LINKED_HASH_SET_H_
