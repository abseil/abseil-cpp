//
// Copyright 2017 The Abseil Authors.
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
// span.h
// -----------------------------------------------------------------------------
//
// This header file defines a `Span<T>` type for holding a reference to existing
// array data. The `Span` object, much like the `absl::string_view` object,
// does not own such data itself, and the data being referenced by the span must
// outlive the span itself. Unlike `view` type references, a span can hold a
// reference to mutable data (and can mutate it for underlying types of
// non-const T.) A span provides a lightweight way to pass a reference to such
// data.
//
// Additionally, this header file defines `MakeSpan()` and `MakeConstSpan()`
// factory functions, for clearly creating spans of type `Span<T>` or read-only
// `Span<const T>` when such types may be difficult to identify due to issues
// with implicit conversion.
//
// The C++20 standard includes a `std::span` type. As of January 2026, the
// differences between `absl::Span` and `std::span` are:
//    * `absl::Span` has `operator==` (which is likely a design bug,
//       per https://abseil.io/blog/20180531-regular-types)
//    * `absl::Span` has the factory functions `MakeSpan()` and
//      `MakeConstSpan()`
//    * bounds-checked access to `absl::Span` is accomplished with `at()`
//      however `std::span` now supports the same as of the draft C++26 standard
//    * `absl::Span` has compiler-provided move and copy constructors and
//      assignment.
//    * `absl::Span` has no `bytes()`, `size_bytes()`, `as_bytes()`, or
//      `as_writable_bytes()` methods
//    * `absl::Span<T, N>` supports a static extent like `std::span<T, N>`, but
//      class template argument deduction and `MakeSpan()` always produce
//      dynamic-extent spans, and static-extent spans have no relational
//      operators of their own.
//    * `absl::Span` has an explicit mutable-reference constructor
//    * `absl::Span::subspan(pos, len)` always truncates `len` to
//      `size() - pos`, whereas `std::span::subspan()` only truncates when the
//      `len` parameter is defaulted.
//
// For more information, see the class comments below.
#ifndef ABSL_TYPES_SPAN_H_
#define ABSL_TYPES_SPAN_H_

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/hardening.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"  // TODO(strel): remove this include
#include "absl/base/throw_delegate.h"
#include "absl/hash/internal/weakly_mixed_integer.h"
#include "absl/meta/type_traits.h"
#include "absl/types/internal/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// dynamic_extent
//
// The extent of a `Span` whose size is only known at runtime.
// `absl::Span<T>` is shorthand for `absl::Span<T, absl::dynamic_extent>`.
inline constexpr size_t dynamic_extent = static_cast<size_t>(-1);

template <typename T, size_t Extent = dynamic_extent>
class Span;

ABSL_NAMESPACE_END
}  // namespace absl

// If std::ranges is available, mark Span as satisfying the `view` and
// `borrowed_range` concepts, just like std::span.
#if __has_include(<version>)
#include <version>  // NOLINT(misc-include-cleaner)
#endif
#if defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 201911L
#include <ranges>  // NOLINT(build/c++20)
template <typename T, size_t Extent>
// NOLINTNEXTLINE(build/c++20)
inline constexpr bool std::ranges::enable_view<absl::Span<T, Extent>> = true;
template <typename T, size_t Extent>
// NOLINTNEXTLINE(build/c++20)
inline constexpr bool
    std::ranges::enable_borrowed_range<absl::Span<T, Extent>> = true;
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN

//------------------------------------------------------------------------------
// Span
//------------------------------------------------------------------------------
//
// A `Span` is an "array reference" type for holding a reference of contiguous
// array data; the `Span` object does not and cannot own such data itself. A
// span provides an easy way to provide overloads for anything operating on
// contiguous sequences without needing to manage pointers and array lengths
// manually.

// A span is conceptually a pointer (ptr) and a length (size) into an already
// existing array of contiguous memory; the array it represents references the
// elements "ptr[0] .. ptr[size-1]". Passing a properly-constructed `Span`
// instead of raw pointers avoids many issues related to index out of bounds
// errors.
//
// Spans may also be constructed from containers holding contiguous sequences.
// Such containers must supply `data()` and `size() const` methods (e.g
// `std::vector<T>`, `absl::InlinedVector<T, N>`). All implicit conversions to
// `absl::Span` from such containers will create spans of type `const T`;
// spans which can mutate their values (of type `T`) must use explicit
// constructors.
//
// A `Span<T>` is somewhat analogous to an `absl::string_view`, but for an array
// of elements of type `T`, and unlike an `absl::string_view`, a span can hold a
// reference to mutable data. A user of `Span` must ensure that the data being
// pointed to outlives the `Span` itself.
//
// You can construct a `Span<T>` in several ways:
//
//   * Explicitly from a reference to a container type
//   * Explicitly from a pointer and size
//   * Implicitly from a container type (but only for spans of type `const T`)
//   * Using the `MakeSpan()` or `MakeConstSpan()` factory functions.
//
// Examples:
//
//   // Construct a Span explicitly from a container:
//   std::vector<int> v = {1, 2, 3, 4, 5};
//   auto span = absl::Span<const int>(v);
//
//   // Construct a Span explicitly from a C-style array:
//   int a[5] =  {1, 2, 3, 4, 5};
//   auto span = absl::Span<const int>(a);
//
//   // Construct a Span implicitly from a container
//   void MyRoutine(absl::Span<const int> a) {
//     ...
//   }
//   std::vector v = {1,2,3,4,5};
//   MyRoutine(v)                     // convert to Span<const T>
//
// Note that `Span` objects, in addition to requiring that the memory they
// point to remains alive, must also ensure that such memory does not get
// reallocated. Therefore, to avoid undefined behavior, containers with
// associated spans should not invoke operations that may reallocate memory
// (such as resizing) or invalidate iterators into the container.
//
// One common use for a `Span` is when passing arguments to a routine that can
// accept a variety of array types (e.g. a `std::vector`, `absl::InlinedVector`,
// a C-style array, etc.). Instead of creating overloads for each case, you
// can simply specify a `Span` as the argument to such a routine.
//
// Example:
//
//   void MyRoutine(absl::Span<const int> a) {
//     ...
//   }
//
//   std::vector v = {1,2,3,4,5};
//   MyRoutine(v);
//
//   absl::InlinedVector<int, 4> my_inline_vector;
//   MyRoutine(my_inline_vector);
//
//   // Explicit constructor from pointer,size
//   int* my_array = new int[10];
//   MyRoutine(absl::Span<const int>(my_array, 10));
//
// Static extents
//
// `Span<T, N>` refers to exactly `N` elements, like `std::span<T, N>`. Only
// conversions that cannot change the size are implicit:
//
//   void SetKey(absl::Span<const uint8_t, 32> key);
//
//   std::array<uint8_t, 32> key = ...;
//   SetKey(key);                                     // OK, implicit
//   absl::Span<const uint8_t> dynamic = key;         // OK, implicit
//   SetKey(absl::Span<const uint8_t, 32>(dynamic));  // explicit, size checked
//   SetKey(dynamic.first<32>());                     // explicit, size checked
//
// The `Span<T>` documented below is the dynamic-extent case,
// `Span<T, dynamic_extent>`. The static-extent case is defined after it.
template <typename T>
class ABSL_ATTRIBUTE_VIEW Span<T, dynamic_extent> {
 private:
  // Used to determine whether a Span can be constructed from a container of
  // type C.
  template <typename C>
  using EnableIfConvertibleFrom =
      std::enable_if_t<!std::is_same_v<Span, std::remove_reference_t<C>> &&
                       span_internal::HasData<T, C>::value &&
                       span_internal::HasSize<C>::value>;

  // Used to SFINAE-enable a function when the slice elements are const.
  template <typename U>
  using EnableIfValueIsConst = std::enable_if_t<std::is_const_v<T>, U>;

  // Used to SFINAE-enable a function when the slice elements are mutable.
  template <typename U>
  using EnableIfValueIsMutable = std::enable_if_t<!std::is_const_v<T>, U>;

 public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  // TODO(b/316099902) - pointer should be absl_nullable, but this makes it hard
  // to recognize foreach loops as safe. absl_nullability_unknown is currently
  // used to suppress -Wnullability-completeness warnings.
  using pointer = T* absl_nullability_unknown;
  using const_pointer = const T* absl_nullability_unknown;
  using reference = T&;
  using const_reference = const T&;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using absl_internal_is_view = std::true_type;

  // NOLINTNEXTLINE
  static constexpr size_type npos = static_cast<size_type>(-1);
  static constexpr size_type extent = dynamic_extent;

  constexpr Span() noexcept : Span(nullptr, 0) {}
  constexpr Span(pointer array ABSL_ATTRIBUTE_LIFETIME_BOUND,
                 size_type length) noexcept
      : ptr_(array), len_(length) {}

  // Implicit conversion constructors
  template <size_t N>
  constexpr Span(T(  // NOLINT(google-explicit-constructor)
      &a ABSL_ATTRIBUTE_LIFETIME_BOUND)[N]) noexcept
      : Span(a, N) {}

  // Explicit reference constructor for a mutable `Span<T>` type. Can be
  // replaced with MakeSpan() to infer the type parameter.
  template <typename V, typename = EnableIfConvertibleFrom<V>,
            typename = EnableIfValueIsMutable<V>,
            typename = span_internal::EnableIfNotIsView<V>>
  explicit Span(
      V& v
          ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept  // NOLINT(runtime/references)
      : Span(span_internal::GetData(v), v.size()) {}

  // Implicit reference constructor for a read-only `Span<const T>` type
  template <typename V, typename = EnableIfConvertibleFrom<V>,
            typename = EnableIfValueIsConst<V>,
            typename = span_internal::EnableIfNotIsView<V>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr Span(const V& v ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : Span(span_internal::GetData(v), v.size()) {}

  // Overloads of the above two functions that are only enabled for view types.
  // This is so we can drop the ABSL_ATTRIBUTE_LIFETIME_BOUND annotation. These
  // overloads must be made unique by using a different template parameter list
  // (hence the = 0 for the IsView enabler).
  template <typename V, typename = EnableIfConvertibleFrom<V>,
            typename = EnableIfValueIsMutable<V>,
            span_internal::EnableIfIsView<V> = 0>
  explicit Span(V& v) noexcept  // NOLINT(runtime/references)
      : Span(span_internal::GetData(v), v.size()) {}
  template <typename V, typename = EnableIfConvertibleFrom<V>,
            typename = EnableIfValueIsConst<V>,
            span_internal::EnableIfIsView<V> = 0>
  constexpr Span(const V& v) noexcept  // NOLINT(google-explicit-constructor)
      : Span(span_internal::GetData(v), v.size()) {}

  // Implicit constructor from an initializer list, making it possible to pass a
  // brace-enclosed initializer list to a function expecting a `Span`. Such
  // spans constructed from an initializer list must be of type `Span<const T>`.
  //
  //   void Process(absl::Span<const int> x);
  //   Process({1, 2, 3});
  //
  // Note that as always the array referenced by the span must outlive the span.
  // Since an initializer list constructor acts as if it is fed a temporary
  // array (cf. C++ standard [dcl.init.list]/5), it's safe to use this
  // constructor only when the `std::initializer_list` itself outlives the span.
  // In order to meet this requirement it's sufficient to ensure that neither
  // the span nor a copy of it is used outside of the expression in which it's
  // created:
  //
  //   // Assume that this function uses the array directly, not retaining any
  //   // copy of the span or pointer to any of its elements.
  //   void Process(absl::Span<const int> ints);
  //
  //   // Okay: the std::initializer_list<int> will reference a temporary array
  //   // that isn't destroyed until after the call to Process returns.
  //   Process({ 17, 19 });
  //
  //   // Not okay: the storage used by the std::initializer_list<int> is not
  //   // allowed to be referenced after the first line.
  //   absl::Span<const int> ints = { 17, 19 };
  //   Process(ints);
  //
  //   // Not okay for the same reason as above: even when the elements of the
  //   // initializer list expression are not temporaries the underlying array
  //   // is, so the initializer list must still outlive the span.
  //   const int foo = 17;
  //   absl::Span<const int> ints = { foo };
  //   Process(ints);
  //
  template <typename LazyT = T,
            typename = EnableIfValueIsConst<LazyT>>
  Span(std::initializer_list<value_type> v
           ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept  // NOLINT(runtime/explicit)
      : Span(v.begin(), v.size()) {}

  // Implicit conversion from a static-extent span, e.g. from `Span<int, 4>` to
  // `Span<int>` or `Span<const int>`.
  template <typename U, size_t N,
            std::enable_if_t<N != dynamic_extent &&
                                 std::is_convertible_v<U (*)[], T (*)[]>,
                             int> = 0>
  constexpr Span(  // NOLINT(google-explicit-constructor)
      Span<U, N> other) noexcept
      : Span(other.data(), N) {}

  // Accessors

  // Span::data()
  //
  // Returns a pointer to the span's underlying array of data (which is held
  // outside the span).
  constexpr pointer data() const noexcept { return ptr_; }

  // Span::size()
  //
  // Returns the size of this span.
  constexpr size_type size() const noexcept { return len_; }

  // Span::length()
  //
  // Returns the length (size) of this span.
  constexpr size_type length() const noexcept { return size(); }

  // Span::empty()
  //
  // Returns a boolean indicating whether or not this span is considered empty.
  constexpr bool empty() const noexcept { return size() == 0; }

  // Span::operator[]
  //
  // Returns a reference to the i'th element of this span.
  constexpr reference operator[](size_type i) const noexcept {
    absl::base_internal::HardeningAssertLT(i, size());
    return ptr_[i];
  }

  // Span::at()
  //
  // Returns a reference to the i'th element of this span.
  constexpr reference at(size_type i) const {
    return ABSL_PREDICT_TRUE(i < size())  //
               ? *(data() + i)
               : (ThrowStdOutOfRange("Span::at failed bounds check"),
                  *(data() + i));
  }

  // Span::front()
  //
  // Returns a reference to the first element of this span. The span must not
  // be empty.
  constexpr reference front() const noexcept {
    absl::base_internal::HardeningAssertGT(size(), static_cast<size_t>(0));
    return *data();
  }

  // Span::back()
  //
  // Returns a reference to the last element of this span. The span must not
  // be empty.
  constexpr reference back() const noexcept {
    absl::base_internal::HardeningAssertGT(size(), static_cast<size_t>(0));
    return *(data() + size() - 1);
  }

  // Span::begin()
  //
  // Returns an iterator pointing to the first element of this span, or `end()`
  // if the span is empty.
  constexpr iterator begin() const noexcept { return data(); }

  // Span::cbegin()
  //
  // Returns a const iterator pointing to the first element of this span, or
  // `end()` if the span is empty.
  constexpr const_iterator cbegin() const noexcept { return begin(); }

  // Span::end()
  //
  // Returns an iterator pointing just beyond the last element at the
  // end of this span. This iterator acts as a placeholder; attempting to
  // access it results in undefined behavior.
  constexpr iterator end() const noexcept { return data() + size(); }

  // Span::cend()
  //
  // Returns a const iterator pointing just beyond the last element at the
  // end of this span. This iterator acts as a placeholder; attempting to
  // access it results in undefined behavior.
  constexpr const_iterator cend() const noexcept { return end(); }

  // Span::rbegin()
  //
  // Returns a reverse iterator pointing to the last element at the end of this
  // span, or `rend()` if the span is empty.
  constexpr reverse_iterator rbegin() const noexcept {
    return reverse_iterator(end());
  }

  // Span::crbegin()
  //
  // Returns a const reverse iterator pointing to the last element at the end of
  // this span, or `crend()` if the span is empty.
  constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  // Span::rend()
  //
  // Returns a reverse iterator pointing just before the first element
  // at the beginning of this span. This pointer acts as a placeholder;
  // attempting to access its element results in undefined behavior.
  constexpr reverse_iterator rend() const noexcept {
    return reverse_iterator(begin());
  }

  // Span::crend()
  //
  // Returns a reverse const iterator pointing just before the first element
  // at the beginning of this span. This pointer acts as a placeholder;
  // attempting to access its element results in undefined behavior.
  constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  // Span mutations

  // Span::remove_prefix()
  //
  // Removes the first `n` elements from the span.
  void remove_prefix(size_type n) noexcept {
    absl::base_internal::HardeningAssertGE(size(), n);
    ptr_ += n;
    len_ -= n;
  }

  // Span::remove_suffix()
  //
  // Removes the last `n` elements from the span.
  void remove_suffix(size_type n) noexcept {
    absl::base_internal::HardeningAssertGE(size(), n);
    len_ -= n;
  }

  // Span::subspan()
  //
  // Returns a `Span` starting at element `pos` and of length `len`. Both `pos`
  // and `len` are of type `size_type` and thus non-negative. Parameter `pos`
  // must be <= size(). Any `len` value that points past the end of the span
  // will be trimmed to at most size() - `pos`. A default `len` value of `npos`
  // ensures the returned subspan continues until the end of the span.
  //
  // Note that trimming behavior differs from `std::span::subspan()`.
  // `std::span::subspan()` requires `len == npos || pos + len <= size()`.
  // In other words, `std::span::subspan()` only trims `len` when its value is
  // defaulted.
  //
  // Examples:
  //
  //   std::vector<int> vec = {10, 11, 12, 13};
  //   absl::MakeSpan(vec).subspan(1, 2);  // {11, 12}
  //   absl::MakeSpan(vec).subspan(2, 8);  // {12, 13}
  //   absl::MakeSpan(vec).subspan(1);     // {11, 12, 13}
  //   absl::MakeSpan(vec).subspan(4);     // {}
  //   absl::MakeSpan(vec).subspan(5);     // throws std::out_of_range
  constexpr Span subspan(size_type pos = 0, size_type len = npos) const {
    return (pos <= size()) ? Span(data() + pos, (std::min)(size() - pos, len))
                           : (ThrowStdOutOfRange("pos > size()"), Span());
  }

  // Span::first()
  //
  // Returns a `Span` containing first `len` elements. Parameter `len` is of
  // type `size_type` and thus non-negative. `len` value must be <= size().
  //
  // Examples:
  //
  //   std::vector<int> vec = {10, 11, 12, 13};
  //   absl::MakeSpan(vec).first(1);  // {10}
  //   absl::MakeSpan(vec).first(3);  // {10, 11, 12}
  //   absl::MakeSpan(vec).first(5);  // throws std::out_of_range
  constexpr Span first(size_type len) const {
    return (len <= size()) ? Span(data(), len)
                           : (ThrowStdOutOfRange("len > size()"), Span());
  }

  // Span::last()
  //
  // Returns a `Span` containing last `len` elements. Parameter `len` is of
  // type `size_type` and thus non-negative. `len` value must be <= size().
  //
  // Examples:
  //
  //   std::vector<int> vec = {10, 11, 12, 13};
  //   absl::MakeSpan(vec).last(1);  // {13}
  //   absl::MakeSpan(vec).last(3);  // {11, 12, 13}
  //   absl::MakeSpan(vec).last(5);  // throws std::out_of_range
  constexpr Span last(size_type len) const {
    return (len <= size()) ? Span(size() - len + data(), len)
                           : (ThrowStdOutOfRange("len > size()"), Span());
  }

  // Span::first<Count>(), Span::last<Count>(), Span::subspan<Offset, Count>()
  //
  // Like `first()`, `last()` and `subspan()` above, but return a static-extent
  // `Span<T, Count>`. Unlike `subspan(pos, len)`, `subspan<Offset, Count>()`
  // never truncates: `Offset + Count` must be <= size(). Without a `Count`, the
  // result is a dynamic-extent span holding the rest of the elements.
  //
  // Examples:
  //
  //   std::vector<int> vec = {10, 11, 12, 13};
  //   absl::MakeSpan(vec).first<2>();       // Span<int, 2>{10, 11}
  //   absl::MakeSpan(vec).last<1>();        // Span<int, 1>{13}
  //   absl::MakeSpan(vec).subspan<1, 2>();  // Span<int, 2>{11, 12}
  //   absl::MakeSpan(vec).subspan<1>();     // Span<int>{11, 12, 13}
  //   absl::MakeSpan(vec).first<5>();       // throws std::out_of_range
  template <size_t Count>
  constexpr Span<T, Count> first() const {
    return Span<T, Count>(first(Count));
  }

  template <size_t Count>
  constexpr Span<T, Count> last() const {
    return Span<T, Count>(last(Count));
  }

  template <size_t Offset, size_t Count = dynamic_extent>
  constexpr Span<T, Count> subspan() const {
    if constexpr (Count == dynamic_extent) {
      return subspan(Offset);
    } else {
      return subspan(Offset).template first<Count>();
    }
  }

  // Support for absl::Hash.
  template <typename H>
  friend H AbslHashValue(H h, Span v) {
    return H::combine_contiguous(std::move(h), v.data(), v.size());
  }

 private:
  pointer ptr_;
  size_type len_;
};

//------------------------------------------------------------------------------
// Span<T, Extent>
//------------------------------------------------------------------------------
//
// A `Span` with a static extent refers to exactly `Extent` elements. The size
// is part of the type, so the span only stores a pointer.
//
// Construction:
//
//   * Implicitly from `T[Extent]`, `std::array<U, Extent>` and
//     `Span<U, Extent>`, where `U*` converts to `T*` by adding qualifiers.
//   * Explicitly from a dynamic-extent `Span<U>`, from a pointer and size, and
//     from containers that `Span<T>` can be constructed from. The size must
//     equal `Extent`, which is checked by Abseil hardening.
//
// A static-extent span converts implicitly to `Span<T>`. There are no
// relational operators between two static-extent spans.
template <typename T, size_t Extent>
class ABSL_ATTRIBUTE_VIEW Span {
 private:
  // SFINAE-enables a conversion from `U*` to `T*` that only adds qualifiers.
  // Unlike `std::is_convertible_v<U*, T*>`, this rejects derived-to-base.
  template <typename U>
  using EnableIfCompatible =
      std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>, int>;

  template <typename V>
  using RemoveCvRef = std::remove_cv_t<std::remove_reference_t<V>>;

  // SFINAE-enables construction from the containers that `Span<T>` can be
  // constructed from, except pointers, arrays and spans. Those have dedicated
  // constructors, so that a size mismatch is a compile-time error.
  template <typename V>
  using EnableIfContainer =
      std::enable_if_t<!std::is_pointer_v<RemoveCvRef<V>> &&
                           !std::is_array_v<RemoveCvRef<V>> &&
                           !span_internal::IsStdArray<RemoveCvRef<V>>::value &&
                           !span_internal::IsSpan<RemoveCvRef<V>>::value &&
                           std::is_constructible_v<Span<T>, V>,
                       int>;

 public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using pointer = T* absl_nullability_unknown;
  using const_pointer = const T* absl_nullability_unknown;
  using reference = T&;
  using const_reference = const T&;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using absl_internal_is_view = std::true_type;

  // NOLINTNEXTLINE
  static constexpr size_type npos = static_cast<size_type>(-1);
  static constexpr size_type extent = Extent;

  // Only spans of zero elements are default-constructible.
  template <size_t E = Extent, std::enable_if_t<E == 0, int> = 0>
  constexpr Span() noexcept : ptr_(nullptr) {}

  // Explicit constructor from a pointer and size. `length` must equal `Extent`.
  constexpr explicit Span(pointer array ABSL_ATTRIBUTE_LIFETIME_BOUND,
                          size_type length) noexcept
      : ptr_(array) {
    absl::base_internal::HardeningAssert(length == Extent);
  }

  // Implicit conversion constructors from arrays of exactly `Extent` elements.
  constexpr Span(  // NOLINT(google-explicit-constructor)
      T (&a ABSL_ATTRIBUTE_LIFETIME_BOUND)[Extent]) noexcept
      : ptr_(a) {}

  template <typename U, EnableIfCompatible<U> = 0>
  constexpr Span(  // NOLINT(google-explicit-constructor)
      std::array<U, Extent>& a ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : ptr_(a.data()) {}

  template <typename U, EnableIfCompatible<const U> = 0>
  constexpr Span(  // NOLINT(google-explicit-constructor)
      const std::array<U, Extent>& a ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : ptr_(a.data()) {}

  // Implicit conversion from a span with the same extent, e.g. from
  // `Span<int, 4>` to `Span<const int, 4>`.
  template <typename U, EnableIfCompatible<U> = 0,
            std::enable_if_t<!std::is_same_v<U, T>, int> = 0>
  constexpr Span(  // NOLINT(google-explicit-constructor)
      Span<U, Extent> other) noexcept
      : ptr_(other.data()) {}

  // Explicit conversion from a dynamic-extent span. `other.size()` must equal
  // `Extent`.
  template <typename U, EnableIfCompatible<U> = 0>
  constexpr explicit Span(Span<U> other) noexcept
      : Span(other.data(), other.size()) {}

  // Explicit conversion from a container. `v.size()` must equal `Extent`. As
  // with `Span<T>`, a span of mutable elements needs a mutable container.
  template <typename V, EnableIfContainer<V> = 0,
            span_internal::EnableIfNotIsView<RemoveCvRef<V>> = 0>
  constexpr explicit Span(V&& v ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : Span(Span<T>(std::forward<V>(v))) {}

  // Overload of the above for view types, without the lifetime annotation.
  template <typename V, EnableIfContainer<V> = 0,
            span_internal::EnableIfIsView<RemoveCvRef<V>> = 0>
  constexpr explicit Span(V&& v) noexcept : Span(Span<T>(std::forward<V>(v))) {}

  // Accessors. See the dynamic-extent `Span<T>` above for documentation.

  constexpr pointer data() const noexcept { return ptr_; }
  constexpr size_type size() const noexcept { return Extent; }
  constexpr size_type length() const noexcept { return size(); }
  constexpr bool empty() const noexcept { return Extent == 0; }

  constexpr reference operator[](size_type i) const noexcept {
    absl::base_internal::HardeningAssertLT(i, size());
    return ptr_[i];
  }

  constexpr reference at(size_type i) const {
    return ABSL_PREDICT_TRUE(i < size())  //
               ? *(data() + i)
               : (ThrowStdOutOfRange("Span::at failed bounds check"),
                  *(data() + i));
  }

  // Unlike for `Span<T>`, calling `front()` or `back()` on a span of zero
  // elements is a compile-time error.
  constexpr reference front() const noexcept {
    static_assert(Extent > 0, "front() requires a non-empty span");
    return *data();
  }

  constexpr reference back() const noexcept {
    static_assert(Extent > 0, "back() requires a non-empty span");
    return *(data() + (Extent - 1));
  }

  constexpr iterator begin() const noexcept { return data(); }
  constexpr const_iterator cbegin() const noexcept { return begin(); }
  constexpr iterator end() const noexcept { return data() + Extent; }
  constexpr const_iterator cend() const noexcept { return end(); }
  constexpr reverse_iterator rbegin() const noexcept {
    return reverse_iterator(end());
  }
  constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  constexpr reverse_iterator rend() const noexcept {
    return reverse_iterator(begin());
  }
  constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  // Span::first<Count>(), Span::last<Count>(), Span::subspan<Offset, Count>()
  //
  // Static-extent subspans. Out-of-range arguments are compile-time errors.
  template <size_t Count>
  constexpr Span<T, Count> first() const noexcept {
    static_assert(Count <= Extent, "Count must not exceed the extent");
    return Span<T, Count>(data(), Count);
  }

  template <size_t Count>
  constexpr Span<T, Count> last() const noexcept {
    static_assert(Count <= Extent, "Count must not exceed the extent");
    return Span<T, Count>(data() + (Extent - Count), Count);
  }

  template <size_t Offset, size_t Count = dynamic_extent>
  constexpr auto subspan() const noexcept {
    static_assert(Offset <= Extent, "Offset must not exceed the extent");
    static_assert(Count == dynamic_extent || Count <= Extent - Offset,
                  "Offset + Count must not exceed the extent");
    constexpr size_type kCount =
        Count == dynamic_extent ? Extent - Offset : Count;
    return Span<T, kCount>(data() + Offset, kCount);
  }

  // Span::first(), Span::last(), Span::subspan()
  //
  // Dynamic-extent subspans, with the same semantics as for `Span<T>`.
  constexpr Span<T> first(size_type len) const {
    return Span<T>(*this).first(len);
  }

  constexpr Span<T> last(size_type len) const {
    return Span<T>(*this).last(len);
  }

  constexpr Span<T> subspan(size_type pos = 0, size_type len = npos) const {
    return Span<T>(*this).subspan(pos, len);
  }

  // Support for absl::Hash. Hashes like a `Span<T>` with the same elements.
  template <typename H>
  friend H AbslHashValue(H h, Span v) {
    return H::combine_contiguous(std::move(h), v.data(), v.size());
  }

 private:
  pointer ptr_;
};

// Class template argument deduction produces dynamic-extent spans, as it did
// before `Span` had an extent parameter.
template <typename T, size_t N>
Span(T (&)[N]) -> Span<T>;

template <typename T>
Span(T* absl_nullability_unknown, size_t) -> Span<T>;

// Span relationals

// Equality is compared element-by-element, while ordering is lexicographical.
// We provide three overloads for each operator to cover any combination on the
// left or right hand side of mutable Span<T>, read-only Span<const T>, and
// convertible-to-read-only Span<T>.
// TODO(zhangxy): Due to MSVC overload resolution bug with partial ordering
// template functions, 5 overloads per operator is needed as a workaround. We
// should update them to 3 overloads per operator using non-deduced context like
// string_view, i.e.
// - (Span<T>, Span<T>)
// - (Span<T>, non_deduced<Span<const T>>)
// - (non_deduced<Span<const T>>, Span<T>)

// operator==
template <typename T>
constexpr bool operator==(Span<T> a, Span<T> b) {
  return span_internal::EqualImpl<Span<const T>>(a, b);
}
template <typename T>
constexpr bool operator==(Span<const T> a, Span<T> b) {
  return span_internal::EqualImpl<Span<const T>>(a, b);
}
template <typename T>
constexpr bool operator==(Span<T> a, Span<const T> b) {
  return span_internal::EqualImpl<Span<const T>>(a, b);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator==(const U& a, Span<T> b) {
  return span_internal::EqualImpl<Span<const T>>(a, b);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator==(Span<T> a, const U& b) {
  return span_internal::EqualImpl<Span<const T>>(a, b);
}

// operator!=
template <typename T>
constexpr bool operator!=(Span<T> a, Span<T> b) {
  return !(a == b);
}
template <typename T>
constexpr bool operator!=(Span<const T> a, Span<T> b) {
  return !(a == b);
}
template <typename T>
constexpr bool operator!=(Span<T> a, Span<const T> b) {
  return !(a == b);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator!=(const U& a, Span<T> b) {
  return !(a == b);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator!=(Span<T> a, const U& b) {
  return !(a == b);
}

// operator<
template <typename T>
constexpr bool operator<(Span<T> a, Span<T> b) {
  return span_internal::LessThanImpl<Span<const T>>(a, b);
}
template <typename T>
constexpr bool operator<(Span<const T> a, Span<T> b) {
  return span_internal::LessThanImpl<Span<const T>>(a, b);
}
template <typename T>
constexpr bool operator<(Span<T> a, Span<const T> b) {
  return span_internal::LessThanImpl<Span<const T>>(a, b);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator<(const U& a, Span<T> b) {
  return span_internal::LessThanImpl<Span<const T>>(a, b);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator<(Span<T> a, const U& b) {
  return span_internal::LessThanImpl<Span<const T>>(a, b);
}

// operator>
template <typename T>
constexpr bool operator>(Span<T> a, Span<T> b) {
  return b < a;
}
template <typename T>
constexpr bool operator>(Span<const T> a, Span<T> b) {
  return b < a;
}
template <typename T>
constexpr bool operator>(Span<T> a, Span<const T> b) {
  return b < a;
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator>(const U& a, Span<T> b) {
  return b < a;
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator>(Span<T> a, const U& b) {
  return b < a;
}

// operator<=
template <typename T>
constexpr bool operator<=(Span<T> a, Span<T> b) {
  return !(b < a);
}
template <typename T>
constexpr bool operator<=(Span<const T> a, Span<T> b) {
  return !(b < a);
}
template <typename T>
constexpr bool operator<=(Span<T> a, Span<const T> b) {
  return !(b < a);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator<=(const U& a, Span<T> b) {
  return !(b < a);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator<=(Span<T> a, const U& b) {
  return !(b < a);
}

// operator>=
template <typename T>
constexpr bool operator>=(Span<T> a, Span<T> b) {
  return !(a < b);
}
template <typename T>
constexpr bool operator>=(Span<const T> a, Span<T> b) {
  return !(a < b);
}
template <typename T>
constexpr bool operator>=(Span<T> a, Span<const T> b) {
  return !(a < b);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator>=(const U& a, Span<T> b) {
  return !(a < b);
}
template <
    typename T, typename U,
    typename = span_internal::EnableIfConvertibleTo<U, absl::Span<const T>>>
constexpr bool operator>=(Span<T> a, const U& b) {
  return !(a < b);
}

// MakeSpan()
//
// Constructs a mutable `Span<T>`, deducing `T` automatically from either a
// container or pointer+size.
//
// Because a read-only `Span<const T>` is implicitly constructed from container
// types regardless of whether the container itself is a const container,
// constructing mutable spans of type `Span<T>` from containers requires
// explicit constructors. The container-accepting version of `MakeSpan()`
// deduces the type of `T` by the constness of the pointer received from the
// container's `data()` member. Similarly, the pointer-accepting version returns
// a `Span<const T>` if `T` is `const`, and a `Span<T>` otherwise.
//
// Examples:
//
//   void MyRoutine(absl::Span<MyComplicatedType> a) {
//     ...
//   };
//   // my_vector is a container of non-const types
//   std::vector<MyComplicatedType> my_vector;
//
//   // Constructing a Span implicitly attempts to create a Span of type
//   // `Span<const T>`
//   MyRoutine(my_vector);                // error, type mismatch
//
//   // Explicitly constructing the Span is verbose
//   MyRoutine(absl::Span<MyComplicatedType>(my_vector));
//
//   // Use MakeSpan() to make an absl::Span<T>
//   MyRoutine(absl::MakeSpan(my_vector));
//
//   // Construct a span from an array ptr+size
//   absl::Span<T> my_span() {
//     return absl::MakeSpan(&array[0], num_elements_);
//   }
//
// NOTE: To avoid undefined behavior if the container is empty, use `.data()`
// or pass the container directly instead of using `&v[0]` or `&v[v.size()]`.
//
template <int&... ExplicitArgumentBarrier, typename T>
constexpr Span<T> MakeSpan(T* absl_nullable ptr ABSL_ATTRIBUTE_LIFETIME_BOUND,
                           size_t size) noexcept {
  return Span<T>(ptr, size);
}

template <int&... ExplicitArgumentBarrier, typename T>
Span<T> MakeSpan(T* absl_nullable begin ABSL_ATTRIBUTE_LIFETIME_BOUND,
                 T* absl_nullable end) noexcept {
  absl::base_internal::HardeningAssertLE(begin, end);
  return Span<T>(begin, static_cast<size_t>(end - begin));
}

template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto MakeSpan(C& c) noexcept  // NOLINT(runtime/references)
    -> std::enable_if_t<span_internal::IsView<C>::value,
                        decltype(absl::MakeSpan(span_internal::GetData(c),
                                                c.size()))> {
  return MakeSpan(span_internal::GetData(c), c.size());
}

template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto MakeSpan(
    C& c ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept  // NOLINT(runtime/references)
    -> std::enable_if_t<!span_internal::IsView<C>::value,
                        decltype(absl::MakeSpan(span_internal::GetData(c),
                                                c.size()))> {
  return MakeSpan(span_internal::GetData(c), c.size());
}

template <int&... ExplicitArgumentBarrier, typename T, size_t N>
constexpr Span<T> MakeSpan(
    T (&array ABSL_ATTRIBUTE_LIFETIME_BOUND)[N]) noexcept {
  return Span<T>(array, N);
}

// MakeConstSpan()
//
// Constructs a `Span<const T>` as with `MakeSpan`, deducing `T` automatically,
// but always returning a `Span<const T>`.
//
// Examples:
//
//   void ProcessInts(absl::Span<const int> some_ints);
//
//   // Call with a pointer and size.
//   int array[3] = { 0, 0, 0 };
//   ProcessInts(absl::MakeConstSpan(&array[0], 3));
//
//   // Call with a [begin, end) pair.
//   ProcessInts(absl::MakeConstSpan(&array[0], &array[3]));
//
//   // Call directly with an array.
//   ProcessInts(absl::MakeConstSpan(array));
//
//   // Call with a contiguous container.
//   std::vector<int> some_ints = ...;
//   ProcessInts(absl::MakeConstSpan(some_ints));
//   ProcessInts(absl::MakeConstSpan(std::vector<int>{ 0, 0, 0 }));
//
template <int&... ExplicitArgumentBarrier, typename T>
constexpr Span<const T> MakeConstSpan(
    T* absl_nullable ptr ABSL_ATTRIBUTE_LIFETIME_BOUND, size_t size) noexcept {
  return Span<const T>(ptr, size);
}

template <int&... ExplicitArgumentBarrier, typename T>
Span<const T> MakeConstSpan(T* absl_nullable begin
                                ABSL_ATTRIBUTE_LIFETIME_BOUND,
                            T* absl_nullable end) noexcept {
  absl::base_internal::HardeningAssertLE(begin, end);
  return Span<const T>(begin, end - begin);
}

template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto MakeConstSpan(const C& c) noexcept
    -> std::enable_if_t<span_internal::IsView<C>::value,
                        decltype(MakeSpan(c))> {
  return MakeSpan(c);
}

template <int&... ExplicitArgumentBarrier, typename C>
constexpr auto MakeConstSpan(const C& c ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
    -> std::enable_if_t<!span_internal::IsView<C>::value,
                        decltype(MakeSpan(c))> {
  return MakeSpan(c);
}

template <int&... ExplicitArgumentBarrier, typename T, size_t N>
constexpr Span<const T> MakeConstSpan(
    const T (&array ABSL_ATTRIBUTE_LIFETIME_BOUND)[N]) noexcept {
  return Span<const T>(array, N);
}
ABSL_NAMESPACE_END
}  // namespace absl
#endif  // ABSL_TYPES_SPAN_H_
