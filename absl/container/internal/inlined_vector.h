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

#ifndef ABSL_CONTAINER_INTERNAL_INLINED_VECTOR_INTERNAL_H_
#define ABSL_CONTAINER_INTERNAL_INLINED_VECTOR_INTERNAL_H_

#include <cstddef>
#include <iterator>
#include <memory>

#include "absl/meta/type_traits.h"

namespace absl {
namespace inlined_vector_internal {

template <typename InlinedVector>
class Storage;

template <template <typename, size_t, typename> class InlinedVector, typename T,
          size_t N, typename A>
class Storage<InlinedVector<T, N, A>> {
 public:
  using allocator_type = A;
  using value_type = typename allocator_type::value_type;
  using pointer = typename allocator_type::pointer;
  using const_pointer = typename allocator_type::const_pointer;
  using reference = typename allocator_type::reference;
  using const_reference = typename allocator_type::const_reference;
  using rvalue_reference = typename allocator_type::value_type&&;
  using size_type = typename allocator_type::size_type;
  using difference_type = typename allocator_type::difference_type;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  explicit Storage(const allocator_type& a) : allocator_and_tag_(a) {}

  // TODO(johnsoncj): Make the below types and members private after migration

  // Holds whether the vector is allocated or not in the lowest bit and the size
  // in the high bits:
  //   `size_ = (size << 1) | is_allocated;`
  class Tag {
    size_type size_;

   public:
    Tag() : size_(0) {}
    size_type size() const { return size_ / 2; }
    void add_size(size_type n) { size_ += n * 2; }
    void set_inline_size(size_type n) { size_ = n * 2; }
    void set_allocated_size(size_type n) { size_ = (n * 2) + 1; }
    bool allocated() const { return size_ % 2; }
  };

  // Derives from `allocator_type` to use the empty base class optimization.
  // If the `allocator_type` is stateless, we can store our instance for free.
  class AllocatorAndTag : private allocator_type {
    Tag tag_;

   public:
    explicit AllocatorAndTag(const allocator_type& a) : allocator_type(a) {}
    Tag& tag() { return tag_; }
    const Tag& tag() const { return tag_; }
    allocator_type& allocator() { return *this; }
    const allocator_type& allocator() const { return *this; }
  };

  class Allocation {
    size_type capacity_;
    pointer buffer_;

   public:
    Allocation(allocator_type& a, size_type capacity)
        : capacity_(capacity), buffer_(Create(a, capacity)) {}
    void Dealloc(allocator_type& a) {
      std::allocator_traits<allocator_type>::deallocate(a, buffer_, capacity_);
    }
    size_type capacity() const { return capacity_; }
    const_pointer buffer() const { return buffer_; }
    pointer buffer() { return buffer_; }
    static pointer Create(allocator_type& a, size_type n) {
      return std::allocator_traits<allocator_type>::allocate(a, n);
    }
  };

  // Stores either the inlined or allocated representation
  union Rep {
    using ValueTypeBuffer =
        absl::aligned_storage_t<sizeof(value_type), alignof(value_type)>;
    using AllocationBuffer =
        absl::aligned_storage_t<sizeof(Allocation), alignof(Allocation)>;

    // Structs wrap the buffers to perform indirection that solves a bizarre
    // compilation error on Visual Studio (all known versions).
    struct InlinedRep {
      ValueTypeBuffer inlined[N];
    };

    struct AllocatedRep {
      AllocationBuffer allocation;
    };

    InlinedRep inlined_storage;
    AllocatedRep allocation_storage;
  };

  AllocatorAndTag allocator_and_tag_;
  Rep rep_;
};

}  // namespace inlined_vector_internal
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_INLINED_VECTOR_INTERNAL_H_
