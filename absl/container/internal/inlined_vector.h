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
#include <cstring>
#include <iterator>
#include <memory>
#include <utility>

#include "absl/container/internal/compressed_tuple.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"

namespace absl {
namespace inlined_vector_internal {

template <typename Iterator>
using IsAtLeastForwardIterator = std::is_convertible<
    typename std::iterator_traits<Iterator>::iterator_category,
    std::forward_iterator_tag>;

template <typename AllocatorType, typename ValueType, typename SizeType>
void DestroyElements(AllocatorType* alloc_ptr, ValueType* destroy_first,
                     SizeType destroy_size) {
  using AllocatorTraits = absl::allocator_traits<AllocatorType>;
  for (SizeType i = 0; i < destroy_size; ++i) {
    AllocatorTraits::destroy(*alloc_ptr, destroy_first + i);
  }

#ifndef NDEBUG
  // Overwrite unused memory with `0xab` so we can catch uninitialized usage.
  //
  // Cast to `void*` to tell the compiler that we don't care that we might be
  // scribbling on a vtable pointer.
  void* memory = reinterpret_cast<void*>(destroy_first);
  size_t memory_size = sizeof(ValueType) * destroy_size;
  std::memset(memory, 0xab, memory_size);
#endif  // NDEBUG
}

template <typename AllocatorType>
struct StorageView {
  using pointer = typename AllocatorType::pointer;
  using size_type = typename AllocatorType::size_type;

  pointer data;
  size_type size;
  size_type capacity;
};

template <typename T, size_t N, typename A>
class Storage {
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
  using AllocatorTraits = absl::allocator_traits<allocator_type>;

  using StorageView = inlined_vector_internal::StorageView<allocator_type>;

  Storage() : metadata_() {}

  explicit Storage(const allocator_type& alloc)
      : metadata_(alloc, /* empty and inlined */ 0) {}

  ~Storage() { DestroyAndDeallocate(); }

  size_type GetSize() const { return GetSizeAndIsAllocated() >> 1; }

  bool GetIsAllocated() const { return GetSizeAndIsAllocated() & 1; }

  pointer GetInlinedData() {
    return reinterpret_cast<pointer>(
        std::addressof(data_.inlined.inlined_data[0]));
  }

  const_pointer GetInlinedData() const {
    return reinterpret_cast<const_pointer>(
        std::addressof(data_.inlined.inlined_data[0]));
  }

  pointer GetAllocatedData() { return data_.allocated.allocated_data; }

  const_pointer GetAllocatedData() const {
    return data_.allocated.allocated_data;
  }

  size_type GetAllocatedCapacity() const {
    return data_.allocated.allocated_capacity;
  }

  StorageView MakeStorageView() {
    return GetIsAllocated() ? StorageView{GetAllocatedData(), GetSize(),
                                          GetAllocatedCapacity()}
                            : StorageView{GetInlinedData(), GetSize(),
                                          static_cast<size_type>(N)};
  }

  allocator_type* GetAllocPtr() {
    return std::addressof(metadata_.template get<0>());
  }

  const allocator_type* GetAllocPtr() const {
    return std::addressof(metadata_.template get<0>());
  }

  void SetAllocatedSize(size_type size) {
    GetSizeAndIsAllocated() = (size << 1) | static_cast<size_type>(1);
  }

  void SetInlinedSize(size_type size) { GetSizeAndIsAllocated() = size << 1; }

  void AddSize(size_type count) { GetSizeAndIsAllocated() += count << 1; }

  void SetAllocatedData(pointer data, size_type capacity) {
    data_.allocated.allocated_data = data;
    data_.allocated.allocated_capacity = capacity;
  }

  void SwapSizeAndIsAllocated(Storage* other) {
    using std::swap;
    swap(GetSizeAndIsAllocated(), other->GetSizeAndIsAllocated());
  }

  void SwapAllocatedSizeAndCapacity(Storage* other) {
    using std::swap;
    swap(data_.allocated, other->data_.allocated);
  }

  void DestroyAndDeallocate();

 private:
  size_type& GetSizeAndIsAllocated() { return metadata_.template get<1>(); }

  const size_type& GetSizeAndIsAllocated() const {
    return metadata_.template get<1>();
  }

  using Metadata =
      container_internal::CompressedTuple<allocator_type, size_type>;

  struct Allocated {
    pointer allocated_data;
    size_type allocated_capacity;
  };

  struct Inlined {
    using InlinedDataElement =
        absl::aligned_storage_t<sizeof(value_type), alignof(value_type)>;
    InlinedDataElement inlined_data[N];
  };

  union Data {
    Allocated allocated;
    Inlined inlined;
  };

  Metadata metadata_;
  Data data_;
};

template <typename T, size_t N, typename A>
void Storage<T, N, A>::DestroyAndDeallocate() {
  namespace ivi = inlined_vector_internal;

  StorageView storage_view = MakeStorageView();

  ivi::DestroyElements(GetAllocPtr(), storage_view.data, storage_view.size);

  if (GetIsAllocated()) {
    AllocatorTraits::deallocate(*GetAllocPtr(), storage_view.data,
                                storage_view.capacity);
  }
}

}  // namespace inlined_vector_internal
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_INLINED_VECTOR_INTERNAL_H_
