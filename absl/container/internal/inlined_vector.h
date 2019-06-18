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

#include "absl/base/macros.h"
#include "absl/container/internal/compressed_tuple.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"

namespace absl {
namespace inlined_vector_internal {

template <typename Iterator>
using IsAtLeastForwardIterator = std::is_convertible<
    typename std::iterator_traits<Iterator>::iterator_category,
    std::forward_iterator_tag>;

template <typename AllocatorType>
using IsMemcpyOk = absl::conjunction<
    std::is_same<std::allocator<typename AllocatorType::value_type>,
                 AllocatorType>,
    absl::is_trivially_copy_constructible<typename AllocatorType::value_type>,
    absl::is_trivially_copy_assignable<typename AllocatorType::value_type>,
    absl::is_trivially_destructible<typename AllocatorType::value_type>>;

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

template <typename AllocatorType, typename ValueType, typename ValueAdapter,
          typename SizeType>
void ConstructElements(AllocatorType* alloc_ptr, ValueType* construct_first,
                       ValueAdapter* values_ptr, SizeType construct_size) {
  // If any construction fails, all completed constructions are rolled back.
  for (SizeType i = 0; i < construct_size; ++i) {
    ABSL_INTERNAL_TRY {
      values_ptr->ConstructNext(alloc_ptr, construct_first + i);
    }
    ABSL_INTERNAL_CATCH_ANY {
      inlined_vector_internal::DestroyElements(alloc_ptr, construct_first, i);

      ABSL_INTERNAL_RETHROW;
    }
  }
}

template <typename AllocatorType>
struct StorageView {
  using pointer = typename AllocatorType::pointer;
  using size_type = typename AllocatorType::size_type;

  pointer data;
  size_type size;
  size_type capacity;
};

template <typename AllocatorType, typename Iterator>
class IteratorValueAdapter {
  using pointer = typename AllocatorType::pointer;
  using AllocatorTraits = absl::allocator_traits<AllocatorType>;

 public:
  explicit IteratorValueAdapter(const Iterator& it) : it_(it) {}

  void ConstructNext(AllocatorType* alloc_ptr, pointer construct_at) {
    AllocatorTraits::construct(*alloc_ptr, construct_at, *it_);
    ++it_;
  }

 private:
  Iterator it_;
};

template <typename AllocatorType>
class CopyValueAdapter {
  using pointer = typename AllocatorType::pointer;
  using const_pointer = typename AllocatorType::const_pointer;
  using const_reference = typename AllocatorType::const_reference;
  using AllocatorTraits = absl::allocator_traits<AllocatorType>;

 public:
  explicit CopyValueAdapter(const_reference v) : ptr_(std::addressof(v)) {}

  void ConstructNext(AllocatorType* alloc_ptr, pointer construct_at) {
    AllocatorTraits::construct(*alloc_ptr, construct_at, *ptr_);
  }

 private:
  const_pointer ptr_;
};

template <typename AllocatorType>
class DefaultValueAdapter {
  using pointer = typename AllocatorType::pointer;
  using value_type = typename AllocatorType::value_type;
  using AllocatorTraits = absl::allocator_traits<AllocatorType>;

 public:
  explicit DefaultValueAdapter() {}

  void ConstructNext(AllocatorType* alloc_ptr, pointer construct_at) {
    AllocatorTraits::construct(*alloc_ptr, construct_at);
  }
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
  using MoveIterator = std::move_iterator<iterator>;
  using AllocatorTraits = absl::allocator_traits<allocator_type>;
  using IsMemcpyOk = inlined_vector_internal::IsMemcpyOk<allocator_type>;

  using StorageView = inlined_vector_internal::StorageView<allocator_type>;

  template <typename Iterator>
  using IteratorValueAdapter =
      inlined_vector_internal::IteratorValueAdapter<allocator_type, Iterator>;
  using CopyValueAdapter =
      inlined_vector_internal::CopyValueAdapter<allocator_type>;
  using DefaultValueAdapter =
      inlined_vector_internal::DefaultValueAdapter<allocator_type>;

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

  void SetIsAllocated() { GetSizeAndIsAllocated() |= 1; }

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

  void MemcpyContents(const Storage& other) {
    assert(IsMemcpyOk::value);

    GetSizeAndIsAllocated() = other.GetSizeAndIsAllocated();
    data_ = other.data_;
  }

  void DestroyAndDeallocate();

  template <typename ValueAdapter>
  void Initialize(ValueAdapter values, size_type new_size);

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
  StorageView storage_view = MakeStorageView();

  inlined_vector_internal::DestroyElements(GetAllocPtr(), storage_view.data,
                                           storage_view.size);

  if (GetIsAllocated()) {
    AllocatorTraits::deallocate(*GetAllocPtr(), storage_view.data,
                                storage_view.capacity);
  }
}

template <typename T, size_t N, typename A>
template <typename ValueAdapter>
auto Storage<T, N, A>::Initialize(ValueAdapter values, size_type new_size)
    -> void {
  // Only callable from constructors!
  assert(!GetIsAllocated());
  assert(GetSize() == 0);

  pointer construct_data;

  if (new_size > static_cast<size_type>(N)) {
    // Because this is only called from the `InlinedVector` constructors, it's
    // safe to take on the allocation with size `0`. If `ConstructElements(...)`
    // throws, deallocation will be automatically handled by `~Storage()`.
    construct_data = AllocatorTraits::allocate(*GetAllocPtr(), new_size);
    SetAllocatedData(construct_data, new_size);
    SetIsAllocated();
  } else {
    construct_data = GetInlinedData();
  }

  inlined_vector_internal::ConstructElements(GetAllocPtr(), construct_data,
                                             &values, new_size);

  // Since the initial size was guaranteed to be `0` and the allocated bit is
  // already correct for either case, *adding* `new_size` gives us the correct
  // result faster than setting it directly.
  AddSize(new_size);
}

}  // namespace inlined_vector_internal
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_INLINED_VECTOR_INTERNAL_H_
