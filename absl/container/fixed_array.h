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
// -----------------------------------------------------------------------------
// File: fixed_array.h
// -----------------------------------------------------------------------------
//
// A `FixedArray<T>` represents a non-resizable array of `T` where the length of
// the array can be determined at run-time. It is a good replacement for
// non-standard and deprecated uses of `alloca()` and variable length arrays
// within the GCC extension. (See
// https://gcc.gnu.org/onlinedocs/gcc/Variable-Length.html).
//
// `FixedArray` allocates small arrays inline, keeping performance fast by
// avoiding heap operations. It also helps reduce the chances of
// accidentally overflowing your stack if large input is passed to
// your function.

#ifndef ABSL_CONTAINER_FIXED_ARRAY_H_
#define ABSL_CONTAINER_FIXED_ARRAY_H_

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

#include "absl/algorithm/algorithm.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/throw_delegate.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/memory/memory.h"

namespace absl {

constexpr static auto kFixedArrayUseDefault = static_cast<size_t>(-1);

// -----------------------------------------------------------------------------
// FixedArray
// -----------------------------------------------------------------------------
//
// A `FixedArray` provides a run-time fixed-size array, allocating a small array
// inline for efficiency.
//
// Most users should not specify an `inline_elements` argument and let
// `FixedArray` automatically determine the number of elements
// to store inline based on `sizeof(T)`. If `inline_elements` is specified, the
// `FixedArray` implementation will use inline storage for arrays with a
// length <= `inline_elements`.
//
// Note that a `FixedArray` constructed with a `size_type` argument will
// default-initialize its values by leaving trivially constructible types
// uninitialized (e.g. int, int[4], double), and others default-constructed.
// This matches the behavior of c-style arrays and `std::array`, but not
// `std::vector`.
//
// Note that `FixedArray` does not provide a public allocator; if it requires a
// heap allocation, it will do so with global `::operator new[]()` and
// `::operator delete[]()`, even if T provides class-scope overrides for these
// operators.
template <typename T, size_t inlined = kFixedArrayUseDefault>
class FixedArray {
  static_assert(!std::is_array<T>::value || std::extent<T>::value > 0,
                "Arrays with unknown bounds cannot be used with FixedArray.");
  static constexpr size_t kInlineBytesDefault = 256;

  // std::iterator_traits isn't guaranteed to be SFINAE-friendly until C++17,
  // but this seems to be mostly pedantic.
  template <typename Iterator>
  using EnableIfForwardIterator = absl::enable_if_t<std::is_convertible<
      typename std::iterator_traits<Iterator>::iterator_category,
      std::forward_iterator_tag>::value>;

 public:
  using value_type = T;
  using iterator = T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using difference_type = ptrdiff_t;
  using size_type = size_t;

  static constexpr size_type inline_elements =
      inlined == kFixedArrayUseDefault
          ? kInlineBytesDefault / sizeof(value_type)
          : inlined;

  FixedArray(const FixedArray& other)
      : FixedArray(other.begin(), other.end()) {}

  FixedArray(FixedArray&& other) noexcept(
      absl::conjunction<absl::allocator_is_nothrow<std::allocator<value_type>>,
                        std::is_nothrow_move_constructible<value_type>>::value)
      : FixedArray(std::make_move_iterator(other.begin()),
                   std::make_move_iterator(other.end())) {}

  // Creates an array object that can store `n` elements.
  // Note that trivially constructible elements will be uninitialized.
  explicit FixedArray(size_type n) : storage_(n) {
    absl::memory_internal::uninitialized_default_construct_n(storage_.begin(),
                                                             size());
  }

  // Creates an array initialized with `n` copies of `val`.
  FixedArray(size_type n, const value_type& val) : storage_(n) {
    std::uninitialized_fill_n(data(), size(), val);
  }

  // Creates an array initialized with the elements from the input
  // range. The array's size will always be `std::distance(first, last)`.
  // REQUIRES: Iterator must be a forward_iterator or better.
  template <typename Iterator, EnableIfForwardIterator<Iterator>* = nullptr>
  FixedArray(Iterator first, Iterator last)
      : storage_(std::distance(first, last)) {
    std::uninitialized_copy(first, last, data());
  }

  FixedArray(std::initializer_list<value_type> init_list)
      : FixedArray(init_list.begin(), init_list.end()) {}

  ~FixedArray() noexcept {
    for (const StorageElement& cur : storage_) {
      cur.~StorageElement();
    }
  }

  // Assignments are deleted because they break the invariant that the size of a
  // `FixedArray` never changes.
  void operator=(FixedArray&&) = delete;
  void operator=(const FixedArray&) = delete;

  // FixedArray::size()
  //
  // Returns the length of the fixed array.
  size_type size() const { return storage_.size(); }

  // FixedArray::max_size()
  //
  // Returns the largest possible value of `std::distance(begin(), end())` for a
  // `FixedArray<T>`. This is equivalent to the most possible addressable bytes
  // over the number of bytes taken by T.
  constexpr size_type max_size() const {
    return std::numeric_limits<difference_type>::max() / sizeof(value_type);
  }

  // FixedArray::empty()
  //
  // Returns whether or not the fixed array is empty.
  bool empty() const { return size() == 0; }

  // FixedArray::memsize()
  //
  // Returns the memory size of the fixed array in bytes.
  size_t memsize() const { return size() * sizeof(value_type); }

  // FixedArray::data()
  //
  // Returns a const T* pointer to elements of the `FixedArray`. This pointer
  // can be used to access (but not modify) the contained elements.
  const_pointer data() const { return AsValueType(storage_.begin()); }

  // Overload of FixedArray::data() to return a T* pointer to elements of the
  // fixed array. This pointer can be used to access and modify the contained
  // elements.
  pointer data() { return AsValueType(storage_.begin()); }

  // FixedArray::operator[]
  //
  // Returns a reference the ith element of the fixed array.
  // REQUIRES: 0 <= i < size()
  reference operator[](size_type i) {
    assert(i < size());
    return data()[i];
  }

  // Overload of FixedArray::operator()[] to return a const reference to the
  // ith element of the fixed array.
  // REQUIRES: 0 <= i < size()
  const_reference operator[](size_type i) const {
    assert(i < size());
    return data()[i];
  }

  // FixedArray::at
  //
  // Bounds-checked access.  Returns a reference to the ith element of the
  // fiexed array, or throws std::out_of_range
  reference at(size_type i) {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      base_internal::ThrowStdOutOfRange("FixedArray::at failed bounds check");
    }
    return data()[i];
  }

  // Overload of FixedArray::at() to return a const reference to the ith element
  // of the fixed array.
  const_reference at(size_type i) const {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      base_internal::ThrowStdOutOfRange("FixedArray::at failed bounds check");
    }
    return data()[i];
  }

  // FixedArray::front()
  //
  // Returns a reference to the first element of the fixed array.
  reference front() { return *begin(); }

  // Overload of FixedArray::front() to return a reference to the first element
  // of a fixed array of const values.
  const_reference front() const { return *begin(); }

  // FixedArray::back()
  //
  // Returns a reference to the last element of the fixed array.
  reference back() { return *(end() - 1); }

  // Overload of FixedArray::back() to return a reference to the last element
  // of a fixed array of const values.
  const_reference back() const { return *(end() - 1); }

  // FixedArray::begin()
  //
  // Returns an iterator to the beginning of the fixed array.
  iterator begin() { return data(); }

  // Overload of FixedArray::begin() to return a const iterator to the
  // beginning of the fixed array.
  const_iterator begin() const { return data(); }

  // FixedArray::cbegin()
  //
  // Returns a const iterator to the beginning of the fixed array.
  const_iterator cbegin() const { return begin(); }

  // FixedArray::end()
  //
  // Returns an iterator to the end of the fixed array.
  iterator end() { return data() + size(); }

  // Overload of FixedArray::end() to return a const iterator to the end of the
  // fixed array.
  const_iterator end() const { return data() + size(); }

  // FixedArray::cend()
  //
  // Returns a const iterator to the end of the fixed array.
  const_iterator cend() const { return end(); }

  // FixedArray::rbegin()
  //
  // Returns a reverse iterator from the end of the fixed array.
  reverse_iterator rbegin() { return reverse_iterator(end()); }

  // Overload of FixedArray::rbegin() to return a const reverse iterator from
  // the end of the fixed array.
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  // FixedArray::crbegin()
  //
  // Returns a const reverse iterator from the end of the fixed array.
  const_reverse_iterator crbegin() const { return rbegin(); }

  // FixedArray::rend()
  //
  // Returns a reverse iterator from the beginning of the fixed array.
  reverse_iterator rend() { return reverse_iterator(begin()); }

  // Overload of FixedArray::rend() for returning a const reverse iterator
  // from the beginning of the fixed array.
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  // FixedArray::crend()
  //
  // Returns a reverse iterator from the beginning of the fixed array.
  const_reverse_iterator crend() const { return rend(); }

  // FixedArray::fill()
  //
  // Assigns the given `value` to all elements in the fixed array.
  void fill(const value_type& val) { std::fill(begin(), end(), val); }

  // Relational operators. Equality operators are elementwise using
  // `operator==`, while order operators order FixedArrays lexicographically.
  friend bool operator==(const FixedArray& lhs, const FixedArray& rhs) {
    return absl::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
  }

  friend bool operator!=(const FixedArray& lhs, const FixedArray& rhs) {
    return !(lhs == rhs);
  }

  friend bool operator<(const FixedArray& lhs, const FixedArray& rhs) {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(),
                                        rhs.end());
  }

  friend bool operator>(const FixedArray& lhs, const FixedArray& rhs) {
    return rhs < lhs;
  }

  friend bool operator<=(const FixedArray& lhs, const FixedArray& rhs) {
    return !(rhs < lhs);
  }

  friend bool operator>=(const FixedArray& lhs, const FixedArray& rhs) {
    return !(lhs < rhs);
  }

 private:
  // StorageElement
  //
  // For FixedArrays with a C-style-array value_type, StorageElement is a POD
  // wrapper struct called StorageElementWrapper that holds the value_type
  // instance inside. This is needed for construction and destruction of the
  // entire array regardless of how many dimensions it has. For all other cases,
  // StorageElement is just an alias of value_type.
  //
  // Maintainer's Note: The simpler solution would be to simply wrap value_type
  // in a struct whether it's an array or not. That causes some paranoid
  // diagnostics to misfire, believing that 'data()' returns a pointer to a
  // single element, rather than the packed array that it really is.
  // e.g.:
  //
  //     FixedArray<char> buf(1);
  //     sprintf(buf.data(), "foo");
  //
  //     error: call to int __builtin___sprintf_chk(etc...)
  //     will always overflow destination buffer [-Werror]
  //
  template <typename OuterT = value_type,
            typename InnerT = absl::remove_extent_t<OuterT>,
            size_t InnerN = std::extent<OuterT>::value>
  struct StorageElementWrapper {
    InnerT array[InnerN];
  };

  using StorageElement =
      absl::conditional_t<std::is_array<value_type>::value,
                          StorageElementWrapper<value_type>, value_type>;

  static pointer AsValueType(pointer ptr) { return ptr; }
  static pointer AsValueType(StorageElementWrapper<value_type>* ptr) {
    return std::addressof(ptr->array);
  }

  static_assert(sizeof(StorageElement) == sizeof(value_type), "");
  static_assert(alignof(StorageElement) == alignof(value_type), "");

  struct NonEmptyInlinedStorage {
    using StorageElementBuffer =
        absl::aligned_storage_t<sizeof(StorageElement),
                                alignof(StorageElement)>;
    StorageElement* data() {
      return reinterpret_cast<StorageElement*>(inlined_storage_.data());
    }

#ifdef ADDRESS_SANITIZER
    void* RedzoneBegin() { return &redzone_begin_; }
    void* RedzoneEnd() { return &redzone_end_ + 1; }
#endif  // ADDRESS_SANITIZER

    void AnnotateConstruct(size_t);
    void AnnotateDestruct(size_t);

    ADDRESS_SANITIZER_REDZONE(redzone_begin_);
    std::array<StorageElementBuffer, inline_elements> inlined_storage_;
    ADDRESS_SANITIZER_REDZONE(redzone_end_);
  };

  struct EmptyInlinedStorage {
    StorageElement* data() { return nullptr; }
    void AnnotateConstruct(size_t) {}
    void AnnotateDestruct(size_t) {}
  };

  using InlinedStorage =
      absl::conditional_t<inline_elements == 0, EmptyInlinedStorage,
                          NonEmptyInlinedStorage>;

  // Storage
  //
  // An instance of Storage manages the inline and out-of-line memory for
  // instances of FixedArray. This guarantees that even when construction of
  // individual elements fails in the FixedArray constructor body, the
  // destructor for Storage will still be called and out-of-line memory will be
  // properly deallocated.
  //
  class Storage : public InlinedStorage {
   public:
    explicit Storage(size_type n) : data_(CreateStorage(n)), size_(n) {}
    ~Storage() noexcept {
      if (UsingInlinedStorage(size())) {
        this->AnnotateDestruct(size());
      } else {
        std::allocator<StorageElement>().deallocate(begin(), size());
      }
    }

    size_type size() const { return size_; }
    StorageElement* begin() const { return data_; }
    StorageElement* end() const { return begin() + size(); }

   private:
    static bool UsingInlinedStorage(size_type n) {
      return n <= inline_elements;
    }

    StorageElement* CreateStorage(size_type n) {
      if (UsingInlinedStorage(n)) {
        this->AnnotateConstruct(n);
        return InlinedStorage::data();
      } else {
        return std::allocator<StorageElement>().allocate(n);
      }
    }

    StorageElement* const data_;
    const size_type size_;
  };

  const Storage storage_;
};

template <typename T, size_t N>
constexpr size_t FixedArray<T, N>::inline_elements;

template <typename T, size_t N>
constexpr size_t FixedArray<T, N>::kInlineBytesDefault;

template <typename T, size_t N>
void FixedArray<T, N>::NonEmptyInlinedStorage::AnnotateConstruct(size_t n) {
#ifdef ADDRESS_SANITIZER
  if (!n) return;
  ANNOTATE_CONTIGUOUS_CONTAINER(data(), RedzoneEnd(), RedzoneEnd(), data() + n);
  ANNOTATE_CONTIGUOUS_CONTAINER(RedzoneBegin(), data(), data(), RedzoneBegin());
#endif                   // ADDRESS_SANITIZER
  static_cast<void>(n);  // Mark used when not in asan mode
}

template <typename T, size_t N>
void FixedArray<T, N>::NonEmptyInlinedStorage::AnnotateDestruct(size_t n) {
#ifdef ADDRESS_SANITIZER
  if (!n) return;
  ANNOTATE_CONTIGUOUS_CONTAINER(data(), RedzoneEnd(), data() + n, RedzoneEnd());
  ANNOTATE_CONTIGUOUS_CONTAINER(RedzoneBegin(), data(), RedzoneBegin(), data());
#endif                   // ADDRESS_SANITIZER
  static_cast<void>(n);  // Mark used when not in asan mode
}

}  // namespace absl
#endif  // ABSL_CONTAINER_FIXED_ARRAY_H_
