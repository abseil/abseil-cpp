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
//
// parallel hash set: by Steven Gregory Popovitch (greg7mdp@gmail.com)
// -------------------------------------------------------------------
//
// Using an array of hash tables as a single hash table, where the target
// hash table within the array is determined by the low bits of the hash.
//
// The benefits are:
//   1. the flat_hash_Map is very efficient, but when resizing the peak 
//      memory usage is quite high (assuming resizing occurs when table 
//      is 85% full, peak memory usage is:
//             O((sizeof(value_type) + 1) * hash.size() * 3 / 0.85)
//      This is because when resizing, we have to copy values from the 
//      original array to the newly allocated double size array , hence the 
//      3 factor.
//      ==> using the array of hash table allow each table of the arry to 
//          resize in turn, which drastically reduces the high memory usage.
//
//   2. when the hash table is being written to, individual hash tables can 
//      be locked, drastically reducing multithreading waits.

// When heterogeneous lookup is enabled, functions that take key_type act as if
// they have an overload set like:
//
//   iterator find(const key_type& key);
//   template <class K>
//   iterator find(const K& key);
//
//   size_type erase(const key_type& key);
//   template <class K>
//   size_type erase(const K& key);
//
//   std::pair<iterator, iterator> equal_range(const key_type& key);
//   template <class K>
//   std::pair<iterator, iterator> equal_range(const K& key);
//
// When heterogeneous lookup is disabled, only the explicit `key_type` overloads
// exist.
//
// find() also supports passing the hash explicitly:
//
//   iterator find(const key_type& key, size_t hash);
//   template <class U>
//   iterator find(const U& key, size_t hash);
//
// In addition the pointer to element and iterator stability guarantees are
// weaker: all iterators and pointers are invalidated after a new element is
// inserted.
//
// IMPLEMENTATION DETAILS
//

#ifndef ABSL_CONTAINER_INTERNAL_PARALLEL_HASH_SET_H_
#define ABSL_CONTAINER_INTERNAL_PARALLEL_HASH_SET_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <array>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/internal/bits.h"
#include "absl/base/internal/endian.h"
#include "absl/base/port.h"
#include "absl/container/internal/common.h"
#include "absl/container/internal/compressed_tuple.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hash_policy_traits.h"
#include "absl/container/internal/hashtable_debug_hooks.h"
#include "absl/container/internal/hashtablez_sampler.h"
#include "absl/container/internal/have_sse.h"
#include "absl/container/internal/layout.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"
#include "absl/utility/utility.h"
#include "absl/synchronization/mutex.h"

namespace absl {
namespace container_internal {


// ----------------------------------------------------------------------------
// Policy: a policy defines how to perform different operations on
// the slots of the hashtable (see hash_policy_traits.h for the full interface
// of policy).
//
// Hash: a (possibly polymorphic) functor that hashes keys of the hashtable. The
// functor should accept a key and return size_t as hash. For best performance
// it is important that the hash function provides high entropy across all bits
// of the hash.
//
// Eq: a (possibly polymorphic) functor that compares two keys for equality. It
// should accept two (of possibly different type) keys and return a bool: true
// if they are equal, false if they are not. If two keys compare equal, then
// their hash values as defined by Hash MUST be equal.
//
// Allocator: an Allocator [http://devdocs.io/cpp/concept/allocator] with which
// the storage of the hashtable will be allocated and the elements will be
// constructed and destroyed.
// ----------------------------------------------------------------------------
template <size_t N,
          template <class, class, class, class> class RefSet,
          class Mutex,
          class Policy, class Hash, class Eq, class Alloc>
class parallel_hash_set 
{
  using PolicyTraits = hash_policy_traits<Policy>;
  using KeyArgImpl =
      KeyArg<IsTransparent<Eq>::value && IsTransparent<Hash>::value>;

  static_assert(N <= 12, "N = 12 means 4096 hash tables!");
  constexpr static size_t num_tables = 1 << N;
  constexpr static size_t mask = num_tables - 1;

public:
  using EmbeddedSet     = RefSet<Policy, Hash, Eq, Alloc>;
  using EmbeddedIterator= typename EmbeddedSet::iterator;
  using EmbeddedConstIterator= typename EmbeddedSet::const_iterator;
  using init_type       = typename PolicyTraits::init_type;
  using key_type        = typename PolicyTraits::key_type;
  using slot_type       = typename PolicyTraits::slot_type;
  using allocator_type  = Alloc;
  using size_type       = size_t;
  using difference_type = ptrdiff_t;
  using hasher          = Hash;
  using key_equal       = Eq;
  using policy_type     = Policy;
  using value_type      = typename PolicyTraits::value_type;
  using reference       = value_type&;
  using const_reference = const value_type&;
  using pointer         = typename absl::allocator_traits<
      allocator_type>::template rebind_traits<value_type>::pointer;
  using const_pointer   = typename absl::allocator_traits<
      allocator_type>::template rebind_traits<value_type>::const_pointer;

  // Alias used for heterogeneous lookup functions.
  // `key_arg<K>` evaluates to `K` when the functors are transparent and to
  // `key_type` otherwise. It permits template argument deduction on `K` for the
  // transparent case.
  // --------------------------------------------------------------------
  template <class K>
  using key_arg         = typename KeyArgImpl::template type<K, key_type>;

protected:
  // --------------------------------------------------------------------
  // MutexLock with the additional set_mutex function, otherwise we could 
  // make the MutexLock from mutex.h a template and use that one.
  // --------------------------------------------------------------------
  class MutexLock_ {
  public:
    explicit MutexLock_(Mutex *mu) : mu_(mu) {
      if (this->mu_)
        this->mu_->Lock();
    }

    void set_mutex(Mutex *mu) {
      assert(mu && this->mu_ == nullptr);
      this->mu_ = mu;
      this->mu_->Lock();
    }

    MutexLock_(const MutexLock_ &)           = delete;  // NOLINT(runtime/mutex)
    MutexLock_(MutexLock_&&)                 = delete;  // NOLINT(runtime/mutex)
    MutexLock_& operator=(const MutexLock_&) = delete;
    MutexLock_& operator=(MutexLock_&&)      = delete;

    ~MutexLock_() { this->mu_->Unlock(); }

  private:
    Mutex * mu_;
  };

  // --------------------------------------------------------------------
  struct Inner : public Mutex
  {
    bool operator==(const Inner& o) const
    {
      MutexLock_ m1(const_cast<Inner *>(this));
      MutexLock_ m2(const_cast<Inner *>(&o));
      return set_ == o.set_;
    }

    EmbeddedSet set_;
  };

private:
  // Give an early error when key_type is not hashable/eq.
  // --------------------------------------------------------------------
  auto KeyTypeCanBeHashed(const Hash& h, const key_type& k) -> decltype(h(k));
  auto KeyTypeCanBeEq(const Eq& eq, const key_type& k)      -> decltype(eq(k, k));

  using AllocTraits     = absl::allocator_traits<allocator_type>;

  static_assert(std::is_lvalue_reference<reference>::value,
                "Policy::element() must return a reference");

  template <typename T>
  struct SameAsElementReference : std::is_same<
      typename std::remove_cv<typename std::remove_reference<reference>::type>::type,
      typename std::remove_cv<typename std::remove_reference<T>::type>::type> {};

  // An enabler for insert(T&&): T must be convertible to init_type or be the
  // same as [cv] value_type [ref].
  // Note: we separate SameAsElementReference into its own type to avoid using
  // reference unless we need to. MSVC doesn't seem to like it in some
  // cases.
  // --------------------------------------------------------------------
  template <class T>
  using RequiresInsertable = typename std::enable_if<
                             absl::disjunction<std::is_convertible<T, init_type>,
                                               SameAsElementReference<T>>::value,
                             int>::type;

  // RequiresNotInit is a workaround for gcc prior to 7.1.
  // See https://godbolt.org/g/Y4xsUh.
  template <class T>
  using RequiresNotInit =
      typename std::enable_if<!std::is_same<T, init_type>::value, int>::type;

  template <class... Ts>
  using IsDecomposable = IsDecomposable<void, PolicyTraits, Hash, Eq, Ts...>;

public:
  static_assert(std::is_same<pointer, value_type*>::value,
                "Allocators with custom pointer types are not supported");
  static_assert(std::is_same<const_pointer, const value_type*>::value,
                "Allocators with custom pointer types are not supported");

  // --------------------- i t e r a t o r ------------------------------
  class iterator {
    friend class parallel_hash_set;

   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = typename parallel_hash_set::value_type;
    using reference         =
        absl::conditional_t<PolicyTraits::constant_iterators::value,
                            const value_type&, value_type&>;
    using pointer           = absl::remove_reference_t<reference>*;
    using difference_type   = typename parallel_hash_set::difference_type;
    using Inner             = typename parallel_hash_set::Inner;
    using EmbeddedSet       = typename parallel_hash_set::EmbeddedSet;
    using EmbeddedIterator  = typename EmbeddedSet::iterator;

    iterator() {}

    reference operator*()  const { return *it_; }
    pointer   operator->() const { return &operator*(); }

    iterator& operator++() {
      assert(inner_); // null inner means we are already at the end
      ++it_;
      skip_empty();
      return *this;
    }
    
    iterator operator++(int) {
      assert(inner_);  // null inner means we are already at the end
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    friend bool operator==(const iterator& a, const iterator& b) {
      return a.inner_ == b.inner_ && (!a.inner_ || a.it_ == b.it_);
    }

    friend bool operator!=(const iterator& a, const iterator& b) {
      return !(a == b);
    }

   private:
    iterator(Inner *inner, Inner *inner_end, const EmbeddedIterator& it) : 
      inner_(inner), inner_end_(inner_end), it_(it)  {  // for begin() and end()
      if (inner)
        it_end_ = inner->set_.end();
    }

    void skip_empty() {
      while (it_ == it_end_) {
        ++inner_;
        if (inner_ == inner_end_) {
          inner_ = nullptr; // marks end()
          break;
        }
        else {
          it_ = inner_->set_.begin();
          it_end_ = inner_->set_.end();
        }
      }
    }

    Inner *inner_      = nullptr;
    Inner *inner_end_  = nullptr;
    EmbeddedIterator it_, it_end_;
  };

  // --------------------- c o n s t   i t e r a t o r -----------------
  class const_iterator {
    friend class parallel_hash_set;

   public:
    using iterator_category = typename iterator::iterator_category;
    using value_type        = typename parallel_hash_set::value_type;
    using reference         = typename parallel_hash_set::const_reference;
    using pointer           = typename parallel_hash_set::const_pointer;
    using difference_type   = typename parallel_hash_set::difference_type;
    using Inner             = typename parallel_hash_set::Inner;

    const_iterator() {}
    // Implicit construction from iterator.
    const_iterator(iterator i) : iter_(std::move(i)) {}

    reference operator*()  const { return *(iter_); }
    pointer   operator->() const { return iter_.operator->(); }

    const_iterator& operator++() {
      ++iter_;
      return *this;
    }
    const_iterator operator++(int) { return iter_++; }

    friend bool operator==(const const_iterator& a, const const_iterator& b) {
      return a.iter_ == b.iter_;
    }
    friend bool operator!=(const const_iterator& a, const const_iterator& b) {
      return !(a == b);
    }

   private:
    const_iterator(const Inner *inner, const Inner *inner_end, const EmbeddedIterator& it)
        : iter_(const_cast<Inner**>(inner), 
                const_cast<Inner**>(inner_end),
                const_cast<EmbeddedIterator*>(it)) {}

    iterator iter_;
  };

  using node_type = node_handle<Policy, hash_policy_traits<Policy>, Alloc>;
  using insert_return_type = InsertReturnType<iterator, node_type>;

  // ------------------------- c o n s t r u c t o r s ------------------

  parallel_hash_set() noexcept(
      std::is_nothrow_default_constructible<EmbeddedSet>::value) {}

  explicit parallel_hash_set(size_t bucket_count, 
                             const hasher& hash          = hasher(),
                             const key_equal& eq         = key_equal(),
                             const allocator_type& alloc = allocator_type()) {
    for (auto& inner : sets_)
      inner.set_ = EmbeddedSet(bucket_count / N, hash, eq, alloc);
  }

  parallel_hash_set(size_t bucket_count, 
                    const hasher& hash,
                    const allocator_type& alloc)
      : parallel_hash_set(bucket_count, hash, key_equal(), alloc) {}

  parallel_hash_set(size_t bucket_count, const allocator_type& alloc)
      : parallel_hash_set(bucket_count, hasher(), key_equal(), alloc) {}

  explicit parallel_hash_set(const allocator_type& alloc)
      : parallel_hash_set(0, hasher(), key_equal(), alloc) {}

  template <class InputIter>
  parallel_hash_set(InputIter first, InputIter last, size_t bucket_count = 0,
                    const hasher& hash = hasher(), const key_equal& eq = key_equal(),
                    const allocator_type& alloc = allocator_type())
    : parallel_hash_set(bucket_count, hash, eq, alloc) {
    insert(first, last);
  }

  template <class InputIter>
  parallel_hash_set(InputIter first, InputIter last, size_t bucket_count,
                    const hasher& hash, const allocator_type& alloc)
    : parallel_hash_set(first, last, bucket_count, hash, key_equal(), alloc) {}

  template <class InputIter>
  parallel_hash_set(InputIter first, InputIter last, size_t bucket_count,
                    const allocator_type& alloc)
    : parallel_hash_set(first, last, bucket_count, hasher(), key_equal(), alloc) {}

  template <class InputIter>
  parallel_hash_set(InputIter first, InputIter last, const allocator_type& alloc)
    : parallel_hash_set(first, last, 0, hasher(), key_equal(), alloc) {}

  // Instead of accepting std::initializer_list<value_type> as the first
  // argument like std::unordered_set<value_type> does, we have two overloads
  // that accept std::initializer_list<T> and std::initializer_list<init_type>.
  // This is advantageous for performance.
  //
  //   // Turns {"abc", "def"} into std::initializer_list<std::string>, then copies
  //   // the strings into the set.
  //   std::unordered_set<std::string> s = {"abc", "def"};
  //
  //   // Turns {"abc", "def"} into std::initializer_list<const char*>, then
  //   // copies the strings into the set.
  //   absl::flat_hash_set<std::string> s = {"abc", "def"};
  //
  // The same trick is used in insert().
  //
  // The enabler is necessary to prevent this constructor from triggering where
  // the copy constructor is meant to be called.
  //
  //   absl::flat_hash_set<int> a, b{a};
  //
  // RequiresNotInit<T> is a workaround for gcc prior to 7.1.
  // --------------------------------------------------------------------
  template <class T, RequiresNotInit<T> = 0, RequiresInsertable<T> = 0>
  parallel_hash_set(std::initializer_list<T> init, size_t bucket_count = 0,
                    const hasher& hash = hasher(), const key_equal& eq = key_equal(),
                    const allocator_type& alloc = allocator_type())
    : parallel_hash_set(init.begin(), init.end(), bucket_count, hash, eq, alloc) {}

  parallel_hash_set(std::initializer_list<init_type> init, size_t bucket_count = 0,
                    const hasher& hash = hasher(), const key_equal& eq = key_equal(),
                    const allocator_type& alloc = allocator_type())
    : parallel_hash_set(init.begin(), init.end(), bucket_count, hash, eq, alloc) {}

  template <class T, RequiresNotInit<T> = 0, RequiresInsertable<T> = 0>
  parallel_hash_set(std::initializer_list<T> init, size_t bucket_count,
                    const hasher& hash, const allocator_type& alloc)
    : parallel_hash_set(init, bucket_count, hash, key_equal(), alloc) {}

  parallel_hash_set(std::initializer_list<init_type> init, size_t bucket_count,
                    const hasher& hash, const allocator_type& alloc)
    : parallel_hash_set(init, bucket_count, hash, key_equal(), alloc) {}

  template <class T, RequiresNotInit<T> = 0, RequiresInsertable<T> = 0>
  parallel_hash_set(std::initializer_list<T> init, size_t bucket_count,
                    const allocator_type& alloc)
    : parallel_hash_set(init, bucket_count, hasher(), key_equal(), alloc) {}

  parallel_hash_set(std::initializer_list<init_type> init, size_t bucket_count,
                    const allocator_type& alloc)
    : parallel_hash_set(init, bucket_count, hasher(), key_equal(), alloc) {}

  template <class T, RequiresNotInit<T> = 0, RequiresInsertable<T> = 0>
  parallel_hash_set(std::initializer_list<T> init, const allocator_type& alloc)
    : parallel_hash_set(init, 0, hasher(), key_equal(), alloc) {}
  
  parallel_hash_set(std::initializer_list<init_type> init,
                    const allocator_type& alloc)
    : parallel_hash_set(init, 0, hasher(), key_equal(), alloc) {}

  parallel_hash_set(const parallel_hash_set& that)
    : parallel_hash_set(that, AllocTraits::select_on_container_copy_construction(
                          that.alloc_ref())) {}

  parallel_hash_set(const parallel_hash_set& that, const allocator_type& a)
    : parallel_hash_set(0, that.hash_ref(), that.eq_ref(), a) {
    for (size_t i=0; i<num_tables; ++i)
      sets_[i].set_ = { that.sets_[i].set_, a };
  }
  
  parallel_hash_set(parallel_hash_set&& that) noexcept(
      std::is_nothrow_copy_constructible<EmbeddedSet>::value)
    : parallel_hash_set(std::move(that), that.alloc_ref()) {
  }

  parallel_hash_set(parallel_hash_set&& that, const allocator_type& a)
  {
    for (size_t i=0; i<num_tables; ++i)
      sets_[i].set_ = { std::move(that.sets_[i]).set_, a };
  }

  parallel_hash_set& operator=(const parallel_hash_set& that) {
    for (size_t i=0; i<num_tables; ++i)
      sets_[i].set_ = that.sets_[i].set_;
    return *this;
  }

  parallel_hash_set& operator=(parallel_hash_set&& that) noexcept(
      absl::allocator_traits<allocator_type>::is_always_equal::value &&
      std::is_nothrow_move_assignable<EmbeddedSet>::value) {
    for (size_t i=0; i<num_tables; ++i)
      sets_[i].set_ = std::move(that.sets_[i].set_);
    return *this;
  }

  ~parallel_hash_set() {}

  iterator begin() {
    auto it = iterator(&sets_[0], &sets_[0] + num_tables, sets_[0].set_.begin());
    it.skip_empty();
    return it;
  }

  iterator       end()          { return iterator(); }
  const_iterator begin()  const { return const_cast<parallel_hash_set *>(this)->begin(); }
  const_iterator end()    const { return const_cast<parallel_hash_set *>(this)->end(); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend()   const { return end(); }

  bool empty() const { return !size(); }

  size_t size() const { 
    size_t sz = 0;
    for (const auto& inner : sets_)
      sz += inner.set_.size();
    return sz; 
  }
  
  size_t capacity() const { 
    size_t c = 0;
    for (const auto& inner : sets_)
      c += inner.set_.capacity();
    return c; 
  }

  size_t max_size() const { return (std::numeric_limits<size_t>::max)(); }

  ABSL_ATTRIBUTE_REINITIALIZES void clear() {
    for (auto& inner : sets_)
      inner.set_.clear();
  }

  // This overload kicks in when the argument is an rvalue of insertable and
  // decomposable type other than init_type.
  //
  //   flat_hash_map<std::string, int> m;
  //   m.insert(std::make_pair("abc", 42));
  // --------------------------------------------------------------------
  template <class T, RequiresInsertable<T> = 0,
            typename std::enable_if<IsDecomposable<T>::value, int>::type = 0,
            T* = nullptr>
  std::pair<iterator, bool> insert(T&& value) {
    return emplace(std::forward<T>(value));
  }

  // This overload kicks in when the argument is a bitfield or an lvalue of
  // insertable and decomposable type.
  //
  //   union { int n : 1; };
  //   flat_hash_set<int> s;
  //   s.insert(n);
  //
  //   flat_hash_set<std::string> s;
  //   const char* p = "hello";
  //   s.insert(p);
  //
  // TODO(romanp): Once we stop supporting gcc 5.1 and below, replace
  // RequiresInsertable<T> with RequiresInsertable<const T&>.
  // We are hitting this bug: https://godbolt.org/g/1Vht4f.
  // --------------------------------------------------------------------
  template <
      class T, RequiresInsertable<T> = 0,
      typename std::enable_if<IsDecomposable<const T&>::value, int>::type = 0>
  std::pair<iterator, bool> insert(const T& value) {
    return emplace(value);
  }

  // This overload kicks in when the argument is an rvalue of init_type. Its
  // purpose is to handle brace-init-list arguments.
  //
  //   flat_hash_set<std::pair<std::string, int>> s;
  //   s.insert({"abc", 42});
  // --------------------------------------------------------------------
  std::pair<iterator, bool> insert(init_type&& value) {
    return emplace(std::move(value));
  }

  template <class T, RequiresInsertable<T> = 0,
            typename std::enable_if<IsDecomposable<T>::value, int>::type = 0,
            T* = nullptr>
  iterator insert(const_iterator, T&& value) {
    return insert(std::forward<T>(value)).first;
  }

  // TODO(romanp): Once we stop supporting gcc 5.1 and below, replace
  // RequiresInsertable<T> with RequiresInsertable<const T&>.
  // We are hitting this bug: https://godbolt.org/g/1Vht4f.
  // --------------------------------------------------------------------
  template <
      class T, RequiresInsertable<T> = 0,
      typename std::enable_if<IsDecomposable<const T&>::value, int>::type = 0>
  iterator insert(const_iterator, const T& value) {
    return insert(value).first;
  }

  iterator insert(const_iterator, init_type&& value) {
    return insert(std::move(value)).first;
  }

  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    for (; first != last; ++first) insert(*first);
  }

  template <class T, RequiresNotInit<T> = 0, RequiresInsertable<const T&> = 0>
  void insert(std::initializer_list<T> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  void insert(std::initializer_list<init_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  insert_return_type insert(node_type&& node) {
    if (!node) 
      return {end(), false, node_type()};
    auto& k = node.key();
    size_t hash  = hash_ref()(k);
    Inner& inner = sets_[subidx(hash)];
    auto&  set   = inner.set_;

    MutexLock_ m(&inner);
    auto   res  = set.insert(std::move(node), hash);
    return { make_iterator(&inner, res.position),
             res.inserted,
             res.inserted ? node_type() : std::move(res.node) };
  }

  iterator insert(const_iterator, node_type&& node) {
    return insert(std::move(node)).first;
  }

  struct ReturnKey_ {
    template <class Key, class... Args>
    Key operator()(Key&& k, const Args&...) const {
      return std::forward<Key>(k);
    }
  };

  template <class K, class... Args>
  std::pair<iterator, bool> emplace_decomposable(const K& key, Args&&... args)
  {
    size_t hash  = hash_ref()(key);
    Inner& inner = sets_[subidx(hash)];
    auto&  set   = inner.set_;
    MutexLock_ m(&inner);
    return make_rv(&inner, set.emplace_decomposable(key, hash, std::forward<Args>(args)...));
  }

  struct EmplaceDecomposable {
    template <class K, class... Args>
    std::pair<iterator, bool> operator()(const K& key, Args&&... args) const {
      return s.emplace_decomposable(key, std::forward<Args>(args)...);
    }
    parallel_hash_set& s;
  };

  // This overload kicks in if we can deduce the key from args. This enables us
  // to avoid constructing value_type if an entry with the same key already
  // exists.
  //
  // For example:
  //
  //   flat_hash_map<std::string, std::string> m = {{"abc", "def"}};
  //   // Creates no std::string copies and makes no heap allocations.
  //   m.emplace("abc", "xyz");
  // --------------------------------------------------------------------
  template <class... Args, typename std::enable_if<
                               IsDecomposable<Args...>::value, int>::type = 0>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return PolicyTraits::apply(EmplaceDecomposable{*this},
                               std::forward<Args>(args)...);
  }

  // This overload kicks in if we cannot deduce the key from args. It constructs
  // value_type unconditionally and then either moves it into the table or
  // destroys.
  // --------------------------------------------------------------------
  template <class... Args, typename std::enable_if<
                               !IsDecomposable<Args...>::value, int>::type = 0>
  std::pair<iterator, bool> emplace(Args&&... args) {
    typename std::aligned_storage<sizeof(slot_type), alignof(slot_type)>::type
        raw;
    slot_type* slot = reinterpret_cast<slot_type*>(&raw);

    PolicyTraits::construct(&alloc_ref(), slot, std::forward<Args>(args)...);
    const auto& elem = PolicyTraits::element(slot);
    size_t hash  = hash_ref()(PolicyTraits::key(slot));
    Inner& inner = sets_[subidx(hash)];
    auto&  set   = inner.set_;
    MutexLock_ m(&inner);
    typename EmbeddedSet::template InsertSlotWithHash<true> f {
            inner, std::move(*slot), hash};
    return make_rv(PolicyTraits::apply(f, elem));
  }

  template <class... Args>
  iterator emplace_hint(const_iterator, Args&&... args) {
    return emplace(std::forward<Args>(args)...).first;
  }

  iterator make_iterator(Inner* inner, const EmbeddedIterator it)
  {
    if (it == inner->set_.end())
      return iterator();
    return iterator(inner, &sets_[0] + num_tables, it);
  }

  std::pair<iterator, bool> make_rv(Inner* inner, 
                                    const std::pair<EmbeddedIterator, bool>& res)
  {
    return {iterator(inner, &sets_[0] + num_tables, res.first), res.second};
  }

  template <class K = key_type, class F>
  iterator lazy_emplace(const key_arg<K>& key, F&& f) {
    auto hash = hash_ref()(key);
    Inner& inner = sets_[subidx(hash)];
    auto&  set   = inner.set_;
    MutexLock_ m(&inner);
    return make_iterator(&inner, set.lazy_emplace(key, hash, std::forward<F>(f)));
  }

  // Extension API: support for heterogeneous keys.
  //
  //   std::unordered_set<std::string> s;
  //   // Turns "abc" into std::string.
  //   s.erase("abc");
  //
  //   flat_hash_set<std::string> s;
  //   // Uses "abc" directly without copying it into std::string.
  //   s.erase("abc");
  // --------------------------------------------------------------------
  template <class K = key_type>
  size_type erase(const key_arg<K>& key) {
    auto hash = hash_ref()(key);
    Inner& inner = sets_[subidx(hash)];
    auto&  set   = inner.set_;
    MutexLock_ m(&inner);
    auto it   = set.find(key, hash);
    if (it == set.end()) 
      return 0;
    set.erase(it);
    return 1;
  }

  // Erases the element pointed to by `it`.  Unlike `std::unordered_set::erase`,
  // this method returns void to reduce algorithmic complexity to O(1).  In
  // order to erase while iterating across a map, use the following idiom (which
  // also works for standard containers):
  //
  // for (auto it = m.begin(), end = m.end(); it != end;) {
  //   if (<pred>) {
  //     m.erase(it++);
  //   } else {
  //     ++it;
  //   }
  // }
  // --------------------------------------------------------------------
  void erase(const_iterator cit) { 
    erase(cit.iter_); 
  }

  // This overload is necessary because otherwise erase<K>(const K&) would be
  // a better match if non-const iterator is passed as an argument.
  // --------------------------------------------------------------------
  void erase(iterator it) {
    assert(it.inner_ != nullptr);
    it.inner_->set_.erase(it.it_);
  }

  iterator erase(const_iterator first, const_iterator last) {
    while (first != last) {
      erase(first++);
    }
    return last.iter_;
  }

  // Moves elements from `src` into `this`.
  // If the element already exists in `this`, it is left unmodified in `src`.
  // --------------------------------------------------------------------
  template <typename E = Eq>
  void merge(parallel_hash_set<N, RefSet, Mutex, Policy, Hash, E, Alloc>& src) {  // NOLINT
    assert(this != &src);
    if (this != &src)
    {
        for (size_t i=0; i<num_tables; ++i)
        {
          MutexLock_ m1(&sets_[i]);
          MutexLock_ m2(&src.sets_[i]);
          sets_[i].set_.merge(src.sets_[i].set_);
        }
    }
  }

  template <typename E = Eq>
  void merge(parallel_hash_set<N, RefSet, Mutex, Policy, Hash, E, Alloc>&& src) {
    merge(src);
  }

  node_type extract(const_iterator position) {
    return position.iter_.inner_->set_.extract(EmbeddedConstIterator(position.iter_.it_));
  }

  template <
      class K = key_type,
      typename std::enable_if<!std::is_same<K, iterator>::value, int>::type = 0>
  node_type extract(const key_arg<K>& key) {
    auto it = find(key);
    return it == end() ? node_type() : extract(const_iterator{it});
  }

  void swap(parallel_hash_set& that) noexcept(
      IsNoThrowSwappable<EmbeddedSet>() &&
      (!AllocTraits::propagate_on_container_swap::value ||
       IsNoThrowSwappable<allocator_type>())) {
    using std::swap;
    for (size_t i=0; i<num_tables; ++i)
    {
      MutexLock_ m1(&sets_[i]);
      MutexLock_ m2(&that.sets_[i]);
      swap(sets_[i].set_, that.sets_[i].set_);
    }
  }

  void rehash(size_t n) {
    size_t nn = n / num_tables;
    for (auto& inner : sets_)
    {
      MutexLock_ m(&inner);
      inner.set_.rehash(nn);
    }
  }

  void reserve(size_t n) { rehash(GrowthToLowerboundCapacity(n)); }

  // Extension API: support for heterogeneous keys.
  //
  //   std::unordered_set<std::string> s;
  //   // Turns "abc" into std::string.
  //   s.count("abc");
  //
  //   ch_set<std::string> s;
  //   // Uses "abc" directly without copying it into std::string.
  //   s.count("abc");
  // --------------------------------------------------------------------
  template <class K = key_type>
  size_t count(const key_arg<K>& key) const {
    return find(key) == end() ? 0 : 1;
  }

  // Issues CPU prefetch instructions for the memory needed to find or insert
  // a key.  Like all lookup functions, this support heterogeneous keys.
  //
  // NOTE: This is a very low level operation and should not be used without
  // specific benchmarks indicating its importance.
  // --------------------------------------------------------------------
  template <class K = key_type>
  void prefetch(const key_arg<K>& key) const {
    (void)key;
#if defined(__GNUC__)
    size_t hash = hash_ref()(key);
    Inner& inner = sets_[subidx(hash)];
    auto&  set   = inner.set_;
    MutexLock_ m(&inner);
    set.prefetch_hash(hash);
#endif  // __GNUC__
  }

  // The API of find() has two extensions.
  //
  // 1. The hash can be passed by the user. It must be equal to the hash of the
  // key.
  //
  // 2. The type of the key argument doesn't have to be key_type. This is so
  // called heterogeneous key support.
  // --------------------------------------------------------------------
  template <class K = key_type>
  iterator find(const key_arg<K>& key, size_t hash) {
    Inner& inner = sets_[subidx(hash)];
    auto&  set   = inner.set_;
    MutexLock_ m(&inner);
    auto  it = set.find(key, hash);
    return make_iterator(&inner, it);
  }

  template <class K = key_type>
  iterator find(const key_arg<K>& key) {
    return find(key, hash_ref()(key));
  }

  template <class K = key_type>
  const_iterator find(const key_arg<K>& key, size_t hash) const {
    return const_cast<parallel_hash_set*>(this)->find(key, hash);
  }

  template <class K = key_type>
  const_iterator find(const key_arg<K>& key) const {
    return find(key, hash_ref()(key));
  }

  template <class K = key_type>
  bool contains(const key_arg<K>& key) const {
    return find(key) != end();
  }

  template <class K = key_type>
  std::pair<iterator, iterator> equal_range(const key_arg<K>& key) {
    auto it = find(key);
    if (it != end()) return {it, std::next(it)};
    return {it, it};
  }

  template <class K = key_type>
  std::pair<const_iterator, const_iterator> equal_range(
      const key_arg<K>& key) const {
    auto it = find(key);
    if (it != end()) return {it, std::next(it)};
    return {it, it};
  }

  size_t bucket_count() const {
    size_t sz = 0;
    for (const auto& inner : sets_)
    {
      MutexLock_ m(const_cast<Inner *>(&inner));
      sz += inner.set_.bucket_count();
    }
    return sz; 
  }

  float load_factor() const {
    size_t capacity = bucket_count();
    return capacity ? static_cast<float>(static_cast<double>(size()) / capacity) : 0;
  }

  float max_load_factor() const { return 1.0f; }
  void max_load_factor(float) {
    // Does nothing.
  }

  hasher hash_function() const { return hash_ref(); }
  key_equal key_eq() const { return eq_ref(); }
  allocator_type get_allocator() const { return alloc_ref(); }

  friend bool operator==(const parallel_hash_set& a, const parallel_hash_set& b) {
    return std::equal(a.sets_.begin(), a.sets_.end(), b.sets_.begin());
  }

  friend bool operator!=(const parallel_hash_set& a, const parallel_hash_set& b) {
    return !(a == b);
  }

  friend void swap(parallel_hash_set& a,
                   parallel_hash_set& b) noexcept(noexcept(a.swap(b))) {
    a.swap(b);
  }

private:
  template <class Container, typename Enabler>
  friend struct absl::container_internal::hashtable_debug_internal::
      HashtableDebugAccess;

  struct FindElement {
    template <class K, class... Args>
    const_iterator operator()(const K& key, Args&&...) const {
      return s.find(key);
    }
    const parallel_hash_set& s;
  };

  struct HashElement {
    template <class K, class... Args>
    size_t operator()(const K& key, Args&&...) const {
      return h(key);
    }
    const hasher& h;
  };

  template <class K1>
  struct EqualElement {
    template <class K2, class... Args>
    bool operator()(const K2& lhs, Args&&...) const {
      return eq(lhs, rhs);
    }
    const K1& rhs;
    const key_equal& eq;
  };

  // "erases" the object from the container, except that it doesn't actually
  // destroy the object. It only updates all the metadata of the class.
  // This can be used in conjunction with Policy::transfer to move the object to
  // another place.
  // --------------------------------------------------------------------
  void erase_meta_only(const_iterator cit) {
    auto &it = cit.iter_;
    assert(it.set_ != nullptr);
    it.set_.erase_meta_only(const_iterator(it.it_));
  }

  void drop_deletes_without_resize() ABSL_ATTRIBUTE_NOINLINE {
    for (auto& inner : sets_)
    {
      MutexLock_ m(&inner)
      inner.set_.drop_deletes_without_resize();
    }
  }

  void rehash_and_grow_if_necessary() {
    for (auto& inner : sets_)
    {
      MutexLock_ m(&inner)
      inner.set_.rehash_and_grow_if_necessary();
    }
  }

  bool has_element(const value_type& elem) const {
    size_t hash  = PolicyTraits::apply(HashElement{hash_ref()}, elem);
    Inner& inner = sets_[subidx(hash)];
    auto&  set   = inner.set_;
    MutexLock_ m(const_cast<Inner *>(&inner));
    return set.has_element(elem, hash);
  }

  // TODO(alkis): Optimize this assuming *this and that don't overlap.
  // --------------------------------------------------------------------
  parallel_hash_set& move_assign(parallel_hash_set&& that, std::true_type) {
    parallel_hash_set tmp(std::move(that));
    swap(tmp);
    return *this;
  }

  parallel_hash_set& move_assign(parallel_hash_set&& that, std::false_type) {
    parallel_hash_set tmp(std::move(that), alloc_ref());
    swap(tmp);
    return *this;
  }

protected:
  template <class K>
  std::tuple<Inner*, size_t, bool> 
  find_or_prepare_insert(const K& key, MutexLock_ &mutexlock) {
    auto hash    = hash_ref()(key);
    Inner& inner = sets_[subidx(hash)];
    auto&  set   = inner.set_;
    mutexlock.set_mutex(&inner);
    auto  p   = set.find_or_prepare_insert(key, hash); // std::pair<size_t, bool>
    return std::make_tuple(&inner, p.first, p.second);
  }

  iterator iterator_at(Inner *inner, 
                       const EmbeddedIterator& it) { 
    return {inner, &sets_[0] + num_tables, it}; 
  }
  const_iterator iterator_at(Inner *inner, 
                             const EmbeddedIterator& it) const { 
    return {inner, &sets_[0] + num_tables, it}; 
  }

  static size_t subidx(size_t hashval) {
    return (hashval ^ (hashval >> N)) & mask;
  }

  static size_t subcnt() {
    return num_tables;
  }

private:
  friend struct RawHashSetTestOnlyAccess;

  size_t growth_left() { 
    size_t sz = 0;
    for (const auto& set : sets_)
      sz += set.growth_left();
    return sz; 
  }

  hasher&       hash_ref()        { return sets_[0].set_.hash_ref(); }
  const hasher& hash_ref() const  { return sets_[0].set_.hash_ref(); }
  key_equal&       eq_ref()       { return sets_[0].set_.eq_ref(); }
  const key_equal& eq_ref() const { return sets_[0].set_.eq_ref(); }
  allocator_type&  alloc_ref()    { return sets_[0].set_.alloc_ref(); }
  const allocator_type& alloc_ref() const { 
    return sets_[0].set_.alloc_ref();
  }

  std::array<Inner, num_tables> sets_;
};

namespace hashtable_debug_internal {
template <typename Set>
struct HashtableDebugAccess<Set, absl::void_t<typename Set::parallel_hash_set>> {
  using Traits = typename Set::PolicyTraits;
  using Slot   = typename Traits::slot_type;

  static size_t GetNumProbes(const Set& c,
                             const typename Set::key_type& key) {
    HashtableDebugAccess<typename Set::EmbeddedSet> debug;
    size_t hashval = c.hash_ref()(key);
    return debug.GetNumProbes(c.sets_[c.subidx(hashval)], key);
  }

  static size_t AllocatedByteSize(const Set& c) {
    HashtableDebugAccess<typename Set::EmbeddedSet> debug;
    size_t m = 0;
    for (const auto& set : c.sets_)
      m += debug.AllocatedByteSize(set);
    return m;
  }

  static size_t LowerBoundAllocatedByteSize(size_t size) {
    size_t capacity = GrowthToLowerboundCapacity(size);
    if (capacity == 0) return 0;
    auto layout = Set::MakeLayout(NormalizeCapacity(capacity));
    size_t m = layout.AllocSize();
    size_t per_slot = Traits::space_used(static_cast<const Slot*>(nullptr));
    if (per_slot != ~size_t{}) {
      m += per_slot * size;
    }
    return m;
  }
};

}  // namespace hashtable_debug_internal
}  // namespace container_internal
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_PARALLEL_HASH_SET_H_
