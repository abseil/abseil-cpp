// Copyright 2019 The Abseil Authors.
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
// File: inlined_vector.h
// -----------------------------------------------------------------------------
//
// This header file contains the declaration and definition of an "inlined
// vector" which behaves in an equivalent fashion to a `std::vector`, except
// that storage for small sequences of the vector are provided inline without
// requiring any heap allocation.
//
// An `absl::InlinedVector<T, N>` specifies the default capacity `N` as one of
// its template parameters. Instances where `size() <= N` hold contained
// elements in inline space. Typically `N` is very small so that sequences that
// are expected to be short do not require allocations.
//
// An `absl::InlinedVector` does not usually require a specific allocator. If
// the inlined vector grows beyond its initial constraints, it will need to
// allocate (as any normal `std::vector` would). This is usually performed with
// the default allocator (defined as `std::allocator<T>`). Optionally, a custom
// allocator type may be specified as `A` in `absl::InlinedVector<T, N, A>`.

#ifndef ABSL_CONTAINER_INLINED_VECTOR_H_
#define ABSL_CONTAINER_INLINED_VECTOR_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/algorithm/algorithm.h"
#include "absl/base/internal/throw_delegate.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/container/internal/inlined_vector.h"
#include "absl/memory/memory.h"

namespace absl {
// -----------------------------------------------------------------------------
// InlinedVector
// -----------------------------------------------------------------------------
//
// An `absl::InlinedVector` is designed to be a drop-in replacement for
// `std::vector` for use cases where the vector's size is sufficiently small
// that it can be inlined. If the inlined vector does grow beyond its estimated
// capacity, it will trigger an initial allocation on the heap, and will behave
// as a `std:vector`. The API of the `absl::InlinedVector` within this file is
// designed to cover the same API footprint as covered by `std::vector`.
template <typename T, size_t N, typename A = std::allocator<T>>
class InlinedVector {
  static_assert(
      N > 0, "InlinedVector cannot be instantiated with `0` inlined elements.");

  using Storage = inlined_vector_internal::Storage<T, N, A>;
  using rvalue_reference = typename Storage::rvalue_reference;
  using MoveIterator = typename Storage::MoveIterator;
  using AllocatorTraits = typename Storage::AllocatorTraits;
  using IsMemcpyOk = typename Storage::IsMemcpyOk;

  template <typename Iterator>
  using IteratorValueAdapter =
      typename Storage::template IteratorValueAdapter<Iterator>;
  using CopyValueAdapter = typename Storage::CopyValueAdapter;
  using DefaultValueAdapter = typename Storage::DefaultValueAdapter;

  template <typename Iterator>
  using EnableIfAtLeastForwardIterator = absl::enable_if_t<
      inlined_vector_internal::IsAtLeastForwardIterator<Iterator>::value>;

  template <typename Iterator>
  using DisableIfAtLeastForwardIterator = absl::enable_if_t<
      !inlined_vector_internal::IsAtLeastForwardIterator<Iterator>::value>;

 public:
  using allocator_type = typename Storage::allocator_type;
  using value_type = typename Storage::value_type;
  using pointer = typename Storage::pointer;
  using const_pointer = typename Storage::const_pointer;
  using reference = typename Storage::reference;
  using const_reference = typename Storage::const_reference;
  using size_type = typename Storage::size_type;
  using difference_type = typename Storage::difference_type;
  using iterator = typename Storage::iterator;
  using const_iterator = typename Storage::const_iterator;
  using reverse_iterator = typename Storage::reverse_iterator;
  using const_reverse_iterator = typename Storage::const_reverse_iterator;

  // ---------------------------------------------------------------------------
  // InlinedVector Constructors and Destructor
  // ---------------------------------------------------------------------------

  // Creates an empty inlined vector with a value-initialized allocator.
  InlinedVector() noexcept(noexcept(allocator_type())) : storage_() {}

  // Creates an empty inlined vector with a specified allocator.
  explicit InlinedVector(const allocator_type& alloc) noexcept
      : storage_(alloc) {}

  // Creates an inlined vector with `n` copies of `value_type()`.
  explicit InlinedVector(size_type n,
                         const allocator_type& alloc = allocator_type())
      : storage_(alloc) {
    storage_.Initialize(DefaultValueAdapter(), n);
  }

  // Creates an inlined vector with `n` copies of `v`.
  InlinedVector(size_type n, const_reference v,
                const allocator_type& alloc = allocator_type())
      : storage_(alloc) {
    storage_.Initialize(CopyValueAdapter(v), n);
  }

  // Creates an inlined vector of copies of the values in `list`.
  InlinedVector(std::initializer_list<value_type> list,
                const allocator_type& alloc = allocator_type())
      : InlinedVector(list.begin(), list.end(), alloc) {}

  // Creates an inlined vector with elements constructed from the provided
  // forward iterator range [`first`, `last`).
  //
  // NOTE: The `enable_if` prevents ambiguous interpretation between a call to
  // this constructor with two integral arguments and a call to the above
  // `InlinedVector(size_type, const_reference)` constructor.
  template <typename ForwardIterator,
            EnableIfAtLeastForwardIterator<ForwardIterator>* = nullptr>
  InlinedVector(ForwardIterator first, ForwardIterator last,
                const allocator_type& alloc = allocator_type())
      : storage_(alloc) {
    storage_.Initialize(IteratorValueAdapter<ForwardIterator>(first),
                        std::distance(first, last));
  }

  // Creates an inlined vector with elements constructed from the provided input
  // iterator range [`first`, `last`).
  template <typename InputIterator,
            DisableIfAtLeastForwardIterator<InputIterator>* = nullptr>
  InlinedVector(InputIterator first, InputIterator last,
                const allocator_type& alloc = allocator_type())
      : storage_(alloc) {
    std::copy(first, last, std::back_inserter(*this));
  }

  // Creates a copy of an `other` inlined vector using `other`'s allocator.
  InlinedVector(const InlinedVector& other)
      : InlinedVector(other, *other.storage_.GetAllocPtr()) {}

  // Creates a copy of an `other` inlined vector using a specified allocator.
  InlinedVector(const InlinedVector& other, const allocator_type& alloc)
      : storage_(alloc) {
    if (IsMemcpyOk::value && !other.storage_.GetIsAllocated()) {
      storage_.MemcpyFrom(other.storage_);
    } else {
      storage_.Initialize(IteratorValueAdapter<const_pointer>(other.data()),
                          other.size());
    }
  }

  // Creates an inlined vector by moving in the contents of an `other` inlined
  // vector without performing any allocations. If `other` contains allocated
  // memory, the newly-created instance will take ownership of that memory
  // (leaving `other` empty). However, if `other` does not contain allocated
  // memory (i.e. is inlined), the new inlined vector will perform element-wise
  // move construction of `other`'s elements.
  //
  // NOTE: since no allocation is performed for the inlined vector in either
  // case, the `noexcept(...)` specification depends on whether moving the
  // underlying objects can throw. We assume:
  //  a) Move constructors should only throw due to allocation failure.
  //  b) If `value_type`'s move constructor allocates, it uses the same
  //     allocation function as the `InlinedVector`'s allocator. Thus, the move
  //     constructor is non-throwing if the allocator is non-throwing or
  //     `value_type`'s move constructor is specified as `noexcept`.
  InlinedVector(InlinedVector&& other) noexcept(
      absl::allocator_is_nothrow<allocator_type>::value ||
      std::is_nothrow_move_constructible<value_type>::value)
      : storage_(*other.storage_.GetAllocPtr()) {
    if (IsMemcpyOk::value) {
      storage_.MemcpyFrom(other.storage_);
      other.storage_.SetInlinedSize(0);
    } else if (other.storage_.GetIsAllocated()) {
      storage_.SetAllocatedData(other.storage_.GetAllocatedData(),
                                other.storage_.GetAllocatedCapacity());
      storage_.SetAllocatedSize(other.storage_.GetSize());
      other.storage_.SetInlinedSize(0);
    } else {
      IteratorValueAdapter<MoveIterator> other_values(
          MoveIterator(other.storage_.GetInlinedData()));
      inlined_vector_internal::ConstructElements(
          storage_.GetAllocPtr(), storage_.GetInlinedData(), &other_values,
          other.storage_.GetSize());
      storage_.SetInlinedSize(other.storage_.GetSize());
    }
  }

  // Creates an inlined vector by moving in the contents of an `other` inlined
  // vector, performing allocations with the specified `alloc` allocator. If
  // `other`'s allocator is not equal to `alloc` and `other` contains allocated
  // memory, this move constructor will create a new allocation.
  //
  // NOTE: since allocation is performed in this case, this constructor can
  // only be `noexcept` if the specified allocator is also `noexcept`. If this
  // is the case, or if `other` contains allocated memory, this constructor
  // performs element-wise move construction of its contents.
  //
  // Only in the case where `other`'s allocator is equal to `alloc` and `other`
  // contains allocated memory will the newly created inlined vector take
  // ownership of `other`'s allocated memory.
  InlinedVector(InlinedVector&& other, const allocator_type& alloc) noexcept(
      absl::allocator_is_nothrow<allocator_type>::value)
      : storage_(alloc) {
    if (IsMemcpyOk::value) {
      storage_.MemcpyFrom(other.storage_);
      other.storage_.SetInlinedSize(0);
    } else if ((*storage_.GetAllocPtr() == *other.storage_.GetAllocPtr()) &&
               other.storage_.GetIsAllocated()) {
      storage_.SetAllocatedData(other.storage_.GetAllocatedData(),
                                other.storage_.GetAllocatedCapacity());
      storage_.SetAllocatedSize(other.storage_.GetSize());
      other.storage_.SetInlinedSize(0);
    } else {
      storage_.Initialize(
          IteratorValueAdapter<MoveIterator>(MoveIterator(other.data())),
          other.size());
    }
  }

  ~InlinedVector() {}

  // ---------------------------------------------------------------------------
  // InlinedVector Member Accessors
  // ---------------------------------------------------------------------------

  // `InlinedVector::empty()`
  //
  // Checks if the inlined vector has no elements.
  bool empty() const noexcept { return !size(); }

  // `InlinedVector::size()`
  //
  // Returns the number of elements in the inlined vector.
  size_type size() const noexcept { return storage_.GetSize(); }

  // `InlinedVector::max_size()`
  //
  // Returns the maximum number of elements the vector can hold.
  size_type max_size() const noexcept {
    // One bit of the size storage is used to indicate whether the inlined
    // vector is allocated. As a result, the maximum size of the container that
    // we can express is half of the max for `size_type`.
    return (std::numeric_limits<size_type>::max)() / 2;
  }

  // `InlinedVector::capacity()`
  //
  // Returns the number of elements that can be stored in the inlined vector
  // without requiring a reallocation of underlying memory.
  //
  // NOTE: For most inlined vectors, `capacity()` should equal the template
  // parameter `N`. For inlined vectors which exceed this capacity, they
  // will no longer be inlined and `capacity()` will equal its capacity on the
  // allocated heap.
  size_type capacity() const noexcept {
    return storage_.GetIsAllocated() ? storage_.GetAllocatedCapacity()
                                     : static_cast<size_type>(N);
  }

  // `InlinedVector::data()`
  //
  // Returns a `pointer` to elements of the inlined vector. This pointer can be
  // used to access and modify the contained elements.
  // Only results within the range [`0`, `size()`) are defined.
  pointer data() noexcept {
    return storage_.GetIsAllocated() ? storage_.GetAllocatedData()
                                     : storage_.GetInlinedData();
  }

  // Overload of `InlinedVector::data()` to return a `const_pointer` to elements
  // of the inlined vector. This pointer can be used to access (but not modify)
  // the contained elements.
  const_pointer data() const noexcept {
    return storage_.GetIsAllocated() ? storage_.GetAllocatedData()
                                     : storage_.GetInlinedData();
  }

  // `InlinedVector::operator[]()`
  //
  // Returns a `reference` to the `i`th element of the inlined vector using the
  // array operator.
  reference operator[](size_type i) {
    assert(i < size());
    return data()[i];
  }

  // Overload of `InlinedVector::operator[]()` to return a `const_reference` to
  // the `i`th element of the inlined vector.
  const_reference operator[](size_type i) const {
    assert(i < size());
    return data()[i];
  }

  // `InlinedVector::at()`
  //
  // Returns a `reference` to the `i`th element of the inlined vector.
  reference at(size_type i) {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      base_internal::ThrowStdOutOfRange(
          "`InlinedVector::at(size_type)` failed bounds check");
    }
    return data()[i];
  }

  // Overload of `InlinedVector::at()` to return a `const_reference` to the
  // `i`th element of the inlined vector.
  const_reference at(size_type i) const {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      base_internal::ThrowStdOutOfRange(
          "`InlinedVector::at(size_type) const` failed bounds check");
    }
    return data()[i];
  }

  // `InlinedVector::front()`
  //
  // Returns a `reference` to the first element of the inlined vector.
  reference front() {
    assert(!empty());
    return at(0);
  }

  // Overload of `InlinedVector::front()` returns a `const_reference` to the
  // first element of the inlined vector.
  const_reference front() const {
    assert(!empty());
    return at(0);
  }

  // `InlinedVector::back()`
  //
  // Returns a `reference` to the last element of the inlined vector.
  reference back() {
    assert(!empty());
    return at(size() - 1);
  }

  // Overload of `InlinedVector::back()` to return a `const_reference` to the
  // last element of the inlined vector.
  const_reference back() const {
    assert(!empty());
    return at(size() - 1);
  }

  // `InlinedVector::begin()`
  //
  // Returns an `iterator` to the beginning of the inlined vector.
  iterator begin() noexcept { return data(); }

  // Overload of `InlinedVector::begin()` to return a `const_iterator` to
  // the beginning of the inlined vector.
  const_iterator begin() const noexcept { return data(); }

  // `InlinedVector::end()`
  //
  // Returns an `iterator` to the end of the inlined vector.
  iterator end() noexcept { return data() + size(); }

  // Overload of `InlinedVector::end()` to return a `const_iterator` to the
  // end of the inlined vector.
  const_iterator end() const noexcept { return data() + size(); }

  // `InlinedVector::cbegin()`
  //
  // Returns a `const_iterator` to the beginning of the inlined vector.
  const_iterator cbegin() const noexcept { return begin(); }

  // `InlinedVector::cend()`
  //
  // Returns a `const_iterator` to the end of the inlined vector.
  const_iterator cend() const noexcept { return end(); }

  // `InlinedVector::rbegin()`
  //
  // Returns a `reverse_iterator` from the end of the inlined vector.
  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

  // Overload of `InlinedVector::rbegin()` to return a
  // `const_reverse_iterator` from the end of the inlined vector.
  const_reverse_iterator rbegin() const noexcept {
    return const_reverse_iterator(end());
  }

  // `InlinedVector::rend()`
  //
  // Returns a `reverse_iterator` from the beginning of the inlined vector.
  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

  // Overload of `InlinedVector::rend()` to return a `const_reverse_iterator`
  // from the beginning of the inlined vector.
  const_reverse_iterator rend() const noexcept {
    return const_reverse_iterator(begin());
  }

  // `InlinedVector::crbegin()`
  //
  // Returns a `const_reverse_iterator` from the end of the inlined vector.
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  // `InlinedVector::crend()`
  //
  // Returns a `const_reverse_iterator` from the beginning of the inlined
  // vector.
  const_reverse_iterator crend() const noexcept { return rend(); }

  // `InlinedVector::get_allocator()`
  //
  // Returns a copy of the allocator of the inlined vector.
  allocator_type get_allocator() const { return *storage_.GetAllocPtr(); }

  // ---------------------------------------------------------------------------
  // InlinedVector Member Mutators
  // ---------------------------------------------------------------------------

  // `InlinedVector::operator=()`
  //
  // Replaces the contents of the inlined vector with copies of the elements in
  // the provided `std::initializer_list`.
  InlinedVector& operator=(std::initializer_list<value_type> list) {
    assign(list.begin(), list.end());
    return *this;
  }

  // Overload of `InlinedVector::operator=()` to replace the contents of the
  // inlined vector with the contents of `other`.
  InlinedVector& operator=(const InlinedVector& other) {
    if (ABSL_PREDICT_TRUE(this != std::addressof(other))) {
      const_pointer other_data = other.data();
      assign(other_data, other_data + other.size());
    }
    return *this;
  }

  // Overload of `InlinedVector::operator=()` to replace the contents of the
  // inlined vector with the contents of `other`.
  //
  // NOTE: As a result of calling this overload, `other` may be empty or it's
  // contents may be left in a moved-from state.
  InlinedVector& operator=(InlinedVector&& other) {
    if (ABSL_PREDICT_FALSE(this == std::addressof(other))) return *this;

    if (IsMemcpyOk::value || other.storage_.GetIsAllocated()) {
      inlined_vector_internal::DestroyElements(storage_.GetAllocPtr(), data(),
                                               size());
      storage_.DeallocateIfAllocated();
      storage_.MemcpyFrom(other.storage_);
      other.storage_.SetInlinedSize(0);
    } else {
      storage_.Assign(IteratorValueAdapter<MoveIterator>(
                          MoveIterator(other.storage_.GetInlinedData())),
                      other.size());
    }

    return *this;
  }

  // `InlinedVector::assign()`
  //
  // Replaces the contents of the inlined vector with `n` copies of `v`.
  void assign(size_type n, const_reference v) {
    storage_.Assign(CopyValueAdapter(v), n);
  }

  // Overload of `InlinedVector::assign()` to replace the contents of the
  // inlined vector with copies of the values in the provided
  // `std::initializer_list`.
  void assign(std::initializer_list<value_type> list) {
    assign(list.begin(), list.end());
  }

  // Overload of `InlinedVector::assign()` to replace the contents of the
  // inlined vector with the forward iterator range [`first`, `last`).
  template <typename ForwardIterator,
            EnableIfAtLeastForwardIterator<ForwardIterator>* = nullptr>
  void assign(ForwardIterator first, ForwardIterator last) {
    storage_.Assign(IteratorValueAdapter<ForwardIterator>(first),
                    std::distance(first, last));
  }

  // Overload of `InlinedVector::assign()` to replace the contents of the
  // inlined vector with the input iterator range [`first`, `last`).
  template <typename InputIterator,
            DisableIfAtLeastForwardIterator<InputIterator>* = nullptr>
  void assign(InputIterator first, InputIterator last) {
    size_type i = 0;
    for (; i < size() && first != last; ++i, static_cast<void>(++first)) {
      at(i) = *first;
    }

    erase(data() + i, data() + size());

    std::copy(first, last, std::back_inserter(*this));
  }

  // `InlinedVector::resize()`
  //
  // Resizes the inlined vector to contain `n` elements. If `n` is smaller than
  // the inlined vector's current size, extra elements are destroyed. If `n` is
  // larger than the initial size, new elements are value-initialized.
  void resize(size_type n) { storage_.Resize(DefaultValueAdapter(), n); }

  // Overload of `InlinedVector::resize()` to resize the inlined vector to
  // contain `n` elements where, if `n` is larger than `size()`, the new values
  // will be copy-constructed from `v`.
  void resize(size_type n, const_reference v) {
    storage_.Resize(CopyValueAdapter(v), n);
  }

  // `InlinedVector::insert()`
  //
  // Copies `v` into `pos`, returning an `iterator` pointing to the newly
  // inserted element.
  iterator insert(const_iterator pos, const_reference v) {
    return emplace(pos, v);
  }

  // Overload of `InlinedVector::insert()` for moving `v` into `pos`, returning
  // an iterator pointing to the newly inserted element.
  iterator insert(const_iterator pos, rvalue_reference v) {
    return emplace(pos, std::move(v));
  }

  // Overload of `InlinedVector::insert()` for inserting `n` contiguous copies
  // of `v` starting at `pos`. Returns an `iterator` pointing to the first of
  // the newly inserted elements.
  iterator insert(const_iterator pos, size_type n, const_reference v) {
    assert(pos >= begin() && pos <= end());
    if (ABSL_PREDICT_FALSE(n == 0)) {
      return const_cast<iterator>(pos);
    }
    value_type copy = v;
    std::pair<iterator, iterator> it_pair = ShiftRight(pos, n);
    std::fill(it_pair.first, it_pair.second, copy);
    UninitializedFill(it_pair.second, it_pair.first + n, copy);
    return it_pair.first;
  }

  // Overload of `InlinedVector::insert()` for copying the contents of the
  // `std::initializer_list` into the vector starting at `pos`. Returns an
  // `iterator` pointing to the first of the newly inserted elements.
  iterator insert(const_iterator pos, std::initializer_list<value_type> list) {
    return insert(pos, list.begin(), list.end());
  }

  // Overload of `InlinedVector::insert()` for inserting elements constructed
  // from the forward iterator range [`first`, `last`). Returns an `iterator`
  // pointing to the first of the newly inserted elements.
  //
  // NOTE: The `enable_if` is intended to disambiguate the two three-argument
  // overloads of `insert()`.
  template <typename ForwardIterator,
            EnableIfAtLeastForwardIterator<ForwardIterator>* = nullptr>
  iterator insert(const_iterator pos, ForwardIterator first,
                  ForwardIterator last) {
    assert(pos >= begin() && pos <= end());
    if (ABSL_PREDICT_FALSE(first == last)) {
      return const_cast<iterator>(pos);
    }
    auto n = std::distance(first, last);
    std::pair<iterator, iterator> it_pair = ShiftRight(pos, n);
    size_type used_spots = it_pair.second - it_pair.first;
    auto open_spot = std::next(first, used_spots);
    std::copy(first, open_spot, it_pair.first);
    UninitializedCopy(open_spot, last, it_pair.second);
    return it_pair.first;
  }

  // Overload of `InlinedVector::insert()` for inserting elements constructed
  // from the input iterator range [`first`, `last`). Returns an `iterator`
  // pointing to the first of the newly inserted elements.
  template <typename InputIterator,
            DisableIfAtLeastForwardIterator<InputIterator>* = nullptr>
  iterator insert(const_iterator pos, InputIterator first, InputIterator last) {
    assert(pos >= begin());
    assert(pos <= end());

    size_type index = std::distance(cbegin(), pos);
    for (size_type i = index; first != last; ++i, static_cast<void>(++first)) {
      insert(data() + i, *first);
    }

    return iterator(data() + index);
  }

  // `InlinedVector::emplace()`
  //
  // Constructs and inserts an object in the inlined vector at the given `pos`,
  // returning an `iterator` pointing to the newly emplaced element.
  template <typename... Args>
  iterator emplace(const_iterator pos, Args&&... args) {
    assert(pos >= begin());
    assert(pos <= end());
    if (ABSL_PREDICT_FALSE(pos == end())) {
      emplace_back(std::forward<Args>(args)...);
      return end() - 1;
    }

    T new_t = T(std::forward<Args>(args)...);

    auto range = ShiftRight(pos, 1);
    if (range.first == range.second) {
      // constructing into uninitialized memory
      Construct(range.first, std::move(new_t));
    } else {
      // assigning into moved-from object
      *range.first = T(std::move(new_t));
    }

    return range.first;
  }

  // `InlinedVector::emplace_back()`
  //
  // Constructs and appends a new element to the end of the inlined vector,
  // returning a `reference` to the emplaced element.
  template <typename... Args>
  reference emplace_back(Args&&... args) {
    return storage_.EmplaceBack(std::forward<Args>(args)...);
  }

  // `InlinedVector::push_back()`
  //
  // Appends a copy of `v` to the end of the inlined vector.
  void push_back(const_reference v) { static_cast<void>(emplace_back(v)); }

  // Overload of `InlinedVector::push_back()` for moving `v` into a newly
  // appended element.
  void push_back(rvalue_reference v) {
    static_cast<void>(emplace_back(std::move(v)));
  }

  // `InlinedVector::pop_back()`
  //
  // Destroys the element at the end of the inlined vector and shrinks the size
  // by `1` (unless the inlined vector is empty, in which case this is a no-op).
  void pop_back() noexcept {
    assert(!empty());

    AllocatorTraits::destroy(*storage_.GetAllocPtr(), data() + (size() - 1));
    storage_.SubtractSize(1);
  }

  // `InlinedVector::erase()`
  //
  // Erases the element at `pos` of the inlined vector, returning an `iterator`
  // pointing to the first element following the erased element.
  //
  // NOTE: May return the end iterator, which is not dereferencable.
  iterator erase(const_iterator pos) {
    assert(pos >= begin());
    assert(pos < end());

    return storage_.Erase(pos, pos + 1);
  }

  // Overload of `InlinedVector::erase()` for erasing all elements in the
  // range [`from`, `to`) in the inlined vector. Returns an `iterator` pointing
  // to the first element following the range erased or the end iterator if `to`
  // was the end iterator.
  iterator erase(const_iterator from, const_iterator to) {
    assert(from >= begin());
    assert(from <= to);
    assert(to <= end());

    if (ABSL_PREDICT_TRUE(from != to)) {
      return storage_.Erase(from, to);
    } else {
      return const_cast<iterator>(from);
    }
  }

  // `InlinedVector::clear()`
  //
  // Destroys all elements in the inlined vector, sets the size of `0` and
  // deallocates the heap allocation if the inlined vector was allocated.
  void clear() noexcept {
    inlined_vector_internal::DestroyElements(storage_.GetAllocPtr(), data(),
                                             size());
    storage_.DeallocateIfAllocated();
    storage_.SetInlinedSize(0);
  }

  // `InlinedVector::reserve()`
  //
  // Enlarges the underlying representation of the inlined vector so it can hold
  // at least `n` elements. This method does not change `size()` or the actual
  // contents of the vector.
  //
  // NOTE: If `n` does not exceed `capacity()`, `reserve()` will have no
  // effects. Otherwise, `reserve()` will reallocate, performing an n-time
  // element-wise move of everything contained.
  void reserve(size_type n) { storage_.Reserve(n); }

  // `InlinedVector::shrink_to_fit()`
  //
  // Reduces memory usage by freeing unused memory. After this call, calls to
  // `capacity()` will be equal to `max(N, size())`.
  //
  // If `size() <= N` and the elements are currently stored on the heap, they
  // will be moved to the inlined storage and the heap memory will be
  // deallocated.
  //
  // If `size() > N` and `size() < capacity()` the elements will be moved to a
  // smaller heap allocation.
  void shrink_to_fit() {
    if (storage_.GetIsAllocated()) {
      storage_.ShrinkToFit();
    }
  }

  // `InlinedVector::swap()`
  //
  // Swaps the contents of this inlined vector with the contents of `other`.
  void swap(InlinedVector& other) {
    if (ABSL_PREDICT_TRUE(this != std::addressof(other))) {
      storage_.Swap(std::addressof(other.storage_));
    }
  }

 private:
  template <typename H, typename TheT, size_t TheN, typename TheA>
  friend H AbslHashValue(H h, const absl::InlinedVector<TheT, TheN, TheA>& a);

  void ResetAllocation(pointer new_data, size_type new_capacity,
                       size_type new_size) {
    if (storage_.GetIsAllocated()) {
      Destroy(storage_.GetAllocatedData(),
              storage_.GetAllocatedData() + size());
      assert(begin() == storage_.GetAllocatedData());
      AllocatorTraits::deallocate(*storage_.GetAllocPtr(),
                                  storage_.GetAllocatedData(),
                                  storage_.GetAllocatedCapacity());
    } else {
      Destroy(storage_.GetInlinedData(), storage_.GetInlinedData() + size());
    }

    storage_.SetAllocatedData(new_data, new_capacity);
    storage_.SetAllocatedSize(new_size);
  }

  template <typename... Args>
  reference Construct(pointer p, Args&&... args) {
    absl::allocator_traits<allocator_type>::construct(
        *storage_.GetAllocPtr(), p, std::forward<Args>(args)...);
    return *p;
  }

  template <typename Iterator>
  void UninitializedCopy(Iterator src, Iterator src_last, pointer dst) {
    for (; src != src_last; ++dst, ++src) Construct(dst, *src);
  }

  template <typename... Args>
  void UninitializedFill(pointer dst, pointer dst_last, const Args&... args) {
    for (; dst != dst_last; ++dst) Construct(dst, args...);
  }

  // Destroy [`from`, `to`) in place.
  void Destroy(pointer from, pointer to) {
    for (pointer cur = from; cur != to; ++cur) {
      absl::allocator_traits<allocator_type>::destroy(*storage_.GetAllocPtr(),
                                                      cur);
    }
#if !defined(NDEBUG)
    // Overwrite unused memory with `0xab` so we can catch uninitialized usage.
    // Cast to `void*` to tell the compiler that we don't care that we might be
    // scribbling on a vtable pointer.
    if (from != to) {
      auto len = sizeof(value_type) * std::distance(from, to);
      std::memset(reinterpret_cast<void*>(from), 0xab, len);
    }
#endif  // !defined(NDEBUG)
  }

  // Shift all elements from `position` to `end()` by `n` places to the right.
  // If the vector needs to be enlarged, memory will be allocated.
  // Returns `iterator`s pointing to the start of the previously-initialized
  // portion and the start of the uninitialized portion of the created gap.
  // The number of initialized spots is `pair.second - pair.first`. The number
  // of raw spots is `n - (pair.second - pair.first)`.
  //
  // Updates the size of the InlinedVector internally.
  std::pair<iterator, iterator> ShiftRight(const_iterator position,
                                           size_type n) {
    iterator start_used = const_cast<iterator>(position);
    iterator start_raw = const_cast<iterator>(position);
    size_type s = size();
    size_type required_size = s + n;

    if (required_size > capacity()) {
      // Compute new capacity by repeatedly doubling current capacity
      size_type new_capacity = capacity();
      while (new_capacity < required_size) {
        new_capacity <<= 1;
      }
      // Move everyone into the new allocation, leaving a gap of `n` for the
      // requested shift.
      pointer new_data =
          AllocatorTraits::allocate(*storage_.GetAllocPtr(), new_capacity);
      size_type index = position - begin();
      UninitializedCopy(std::make_move_iterator(data()),
                        std::make_move_iterator(data() + index), new_data);
      UninitializedCopy(std::make_move_iterator(data() + index),
                        std::make_move_iterator(data() + s),
                        new_data + index + n);
      ResetAllocation(new_data, new_capacity, s);

      // New allocation means our iterator is invalid, so we'll recalculate.
      // Since the entire gap is in new space, there's no used space to reuse.
      start_raw = begin() + index;
      start_used = start_raw;
    } else {
      // If we had enough space, it's a two-part move. Elements going into
      // previously-unoccupied space need an `UninitializedCopy()`. Elements
      // going into a previously-occupied space are just a `std::move()`.
      iterator pos = const_cast<iterator>(position);
      iterator raw_space = end();
      size_type slots_in_used_space = raw_space - pos;
      size_type new_elements_in_used_space = (std::min)(n, slots_in_used_space);
      size_type new_elements_in_raw_space = n - new_elements_in_used_space;
      size_type old_elements_in_used_space =
          slots_in_used_space - new_elements_in_used_space;

      UninitializedCopy(
          std::make_move_iterator(pos + old_elements_in_used_space),
          std::make_move_iterator(raw_space),
          raw_space + new_elements_in_raw_space);
      std::move_backward(pos, pos + old_elements_in_used_space, raw_space);

      // If the gap is entirely in raw space, the used space starts where the
      // raw space starts, leaving no elements in used space. If the gap is
      // entirely in used space, the raw space starts at the end of the gap,
      // leaving all elements accounted for within the used space.
      start_used = pos;
      start_raw = pos + new_elements_in_used_space;
    }
    storage_.AddSize(n);
    return std::make_pair(start_used, start_raw);
  }

  Storage storage_;
};

// -----------------------------------------------------------------------------
// InlinedVector Non-Member Functions
// -----------------------------------------------------------------------------

// `swap()`
//
// Swaps the contents of two inlined vectors. This convenience function
// simply calls `InlinedVector::swap()`.
template <typename T, size_t N, typename A>
void swap(absl::InlinedVector<T, N, A>& a,
          absl::InlinedVector<T, N, A>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

// `operator==()`
//
// Tests the equivalency of the contents of two inlined vectors.
template <typename T, size_t N, typename A>
bool operator==(const absl::InlinedVector<T, N, A>& a,
                const absl::InlinedVector<T, N, A>& b) {
  auto a_data = a.data();
  auto a_size = a.size();
  auto b_data = b.data();
  auto b_size = b.size();
  return absl::equal(a_data, a_data + a_size, b_data, b_data + b_size);
}

// `operator!=()`
//
// Tests the inequality of the contents of two inlined vectors.
template <typename T, size_t N, typename A>
bool operator!=(const absl::InlinedVector<T, N, A>& a,
                const absl::InlinedVector<T, N, A>& b) {
  return !(a == b);
}

// `operator<()`
//
// Tests whether the contents of one inlined vector are less than the contents
// of another through a lexicographical comparison operation.
template <typename T, size_t N, typename A>
bool operator<(const absl::InlinedVector<T, N, A>& a,
               const absl::InlinedVector<T, N, A>& b) {
  auto a_data = a.data();
  auto a_size = a.size();
  auto b_data = b.data();
  auto b_size = b.size();
  return std::lexicographical_compare(a_data, a_data + a_size, b_data,
                                      b_data + b_size);
}

// `operator>()`
//
// Tests whether the contents of one inlined vector are greater than the
// contents of another through a lexicographical comparison operation.
template <typename T, size_t N, typename A>
bool operator>(const absl::InlinedVector<T, N, A>& a,
               const absl::InlinedVector<T, N, A>& b) {
  return b < a;
}

// `operator<=()`
//
// Tests whether the contents of one inlined vector are less than or equal to
// the contents of another through a lexicographical comparison operation.
template <typename T, size_t N, typename A>
bool operator<=(const absl::InlinedVector<T, N, A>& a,
                const absl::InlinedVector<T, N, A>& b) {
  return !(b < a);
}

// `operator>=()`
//
// Tests whether the contents of one inlined vector are greater than or equal to
// the contents of another through a lexicographical comparison operation.
template <typename T, size_t N, typename A>
bool operator>=(const absl::InlinedVector<T, N, A>& a,
                const absl::InlinedVector<T, N, A>& b) {
  return !(a < b);
}

// `AbslHashValue()`
//
// Provides `absl::Hash` support for `absl::InlinedVector`. You do not normally
// call this function directly.
template <typename H, typename TheT, size_t TheN, typename TheA>
H AbslHashValue(H h, const absl::InlinedVector<TheT, TheN, TheA>& a) {
  auto a_data = a.data();
  auto a_size = a.size();
  return H::combine(H::combine_contiguous(std::move(h), a_data, a_size),
                    a_size);
}

}  // namespace absl

#endif  // ABSL_CONTAINER_INLINED_VECTOR_H_
