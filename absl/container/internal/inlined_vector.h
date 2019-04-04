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
#include <utility>

#include "absl/container/internal/compressed_tuple.h"
#include "absl/meta/type_traits.h"

namespace absl {
namespace inlined_vector_internal {

template <typename InlinedVector>
class Storage;

template <template <typename, size_t, typename> class InlinedVector, typename T,
          size_t N, typename A>
class Storage<InlinedVector<T, N, A>> {
 public:
  class Allocation;  // TODO(johnsoncj): Remove after migration

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

  explicit Storage(const allocator_type& alloc)
      : metadata_(alloc, /* empty and inlined */ 0) {}

  size_type GetSize() const { return GetSizeAndIsAllocated() >> 1; }

  bool GetIsAllocated() const { return GetSizeAndIsAllocated() & 1; }

  Allocation& GetAllocation() {
    return reinterpret_cast<Allocation&>(rep_.allocation_storage.allocation);
  }

  const Allocation& GetAllocation() const {
    return reinterpret_cast<const Allocation&>(
        rep_.allocation_storage.allocation);
  }

  pointer GetInlinedData() {
    return reinterpret_cast<pointer>(
        std::addressof(rep_.inlined_storage.inlined[0]));
  }

  const_pointer GetInlinedData() const {
    return reinterpret_cast<const_pointer>(
        std::addressof(rep_.inlined_storage.inlined[0]));
  }

  pointer GetAllocatedData() { return GetAllocation().buffer(); }

  const_pointer GetAllocatedData() const { return GetAllocation().buffer(); }

  size_type GetAllocatedCapacity() const { return GetAllocation().capacity(); }

  allocator_type& GetAllocator() { return metadata_.template get<0>(); }

  const allocator_type& GetAllocator() const {
    return metadata_.template get<0>();
  }

  void SetAllocatedSize(size_type size) {
    GetSizeAndIsAllocated() = (size << 1) | static_cast<size_type>(1);
  }

  void SetInlinedSize(size_type size) { GetSizeAndIsAllocated() = size << 1; }

  void AddSize(size_type count) { GetSizeAndIsAllocated() += count << 1; }

  void InitAllocation(const Allocation& allocation) {
    new (static_cast<void*>(std::addressof(rep_.allocation_storage.allocation)))
        Allocation(allocation);
  }

  void SwapSizeAndIsAllocated(Storage& other) {
    using std::swap;
    swap(GetSizeAndIsAllocated(), other.GetSizeAndIsAllocated());
  }

  // TODO(johnsoncj): Make the below types private after migration
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

 private:
  size_type& GetSizeAndIsAllocated() { return metadata_.template get<1>(); }

  const size_type& GetSizeAndIsAllocated() const {
    return metadata_.template get<1>();
  }

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

  container_internal::CompressedTuple<allocator_type, size_type> metadata_;
  Rep rep_;
};

}  // namespace inlined_vector_internal
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_INLINED_VECTOR_INTERNAL_H_
