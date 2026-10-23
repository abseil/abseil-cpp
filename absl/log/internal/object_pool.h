// Copyright 2026 The Abseil Authors.
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
// File: log/internal/object_pool.h
// -----------------------------------------------------------------------------
//
// This file declares `class absl::log_internal::ObjectPool<T, kCapacity>`, a
// fixed-capacity, lock-free object pool for reusable objects with static
// storage duration.

#ifndef ABSL_LOG_INTERNAL_OBJECT_POOL_H_
#define ABSL_LOG_INTERNAL_OBJECT_POOL_H_

#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/sysinfo.h"  // IWYU pragma: keep
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

// A fixed-capacity, lock-free object pool of up to `kCapacity` reusable `T`
// instances.
//
// `ObjectPool` is `constexpr`-constructible (supporting `ABSL_CONST_INIT`
// static storage duration) and trivially destructible; pooled instances are
// intentionally never destroyed at process exit so that usage during static
// destruction remains safe.
//
// `T` must be constructible from the arguments passed to `Acquire(args...)` and
// must provide a `void Reset(args...)` member function with a matching
// signature to reinitialize a previously pooled instance.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable \
                : 4324)  // structure was padded due to alignment specifier
#endif
template <typename T, size_t kCapacity>
class ObjectPool final {
  static_assert(kCapacity > 0, "ObjectPool capacity must be positive.");

 public:
  constexpr ObjectPool() = default;
  ObjectPool(const ObjectPool&) = delete;
  ObjectPool& operator=(const ObjectPool&) = delete;

  // Acquires a `T` instance initialized with `args...`.
  //
  // If a pool slot is available, either lazily allocates a new `T` in that
  // slot or calls `ptr->Reset(args...)` on the existing instance. If all
  // `kCapacity` slots are concurrently in use, allocates a temporary overflow
  // `T` on the heap.
  template <typename... Args>
  absl_nonnull std::unique_ptr<T> Acquire(Args&&... args) {
    const size_t start = GetSlotHint();
    for (size_t i = 0; i < kCapacity; ++i) {
      const size_t idx = (start + i) % kCapacity;
      Slot& slot = slots_[idx];
      if (!slot.in_use.load(std::memory_order_relaxed) &&
          !slot.in_use.exchange(true, std::memory_order_acquire)) {
        SetSlotHint(idx);
        T* ptr = slot.ptr.load(std::memory_order_relaxed);
        if (ptr == nullptr) {
          ptr = new T(std::forward<Args>(args)...);
          slot.ptr.store(ptr, std::memory_order_relaxed);
        } else {
          ptr->Reset(std::forward<Args>(args)...);
        }
        return std::unique_ptr<T>(ptr);
      }
    }
    return std::make_unique<T>(std::forward<Args>(args)...);
  }

  // Returns `item` to the pool if it belongs to one of the pool's slots, or
  // deletes it if it was an overflow allocation.
  void Release(std::unique_ptr<T> item) {
    if (item == nullptr) return;
    const size_t start = GetSlotHint();
    for (size_t i = 0; i < kCapacity; ++i) {
      Slot& slot = slots_[(start + i) % kCapacity];
      if (slot.ptr.load(std::memory_order_relaxed) == item.get()) {
        item.release();
        slot.in_use.store(false, std::memory_order_release);
        return;
      }
    }
  }

 private:
  struct alignas(ABSL_CACHELINE_SIZE) Slot {
    constexpr Slot() : in_use(false), ptr(nullptr) {}
    std::atomic<bool> in_use;
    std::atomic<T*> ptr;
  };

#ifdef ABSL_HAVE_THREAD_LOCAL
  static size_t& SlotHint() {
    ABSL_CONST_INIT thread_local size_t slot_hint = 0;
    return slot_hint;
  }
  static size_t GetSlotHint() { return SlotHint(); }
  static void SetSlotHint(size_t idx) { SlotHint() = idx; }
#else
  static size_t GetSlotHint() {
    return static_cast<size_t>(absl::base_internal::GetCachedTID()) % kCapacity;
  }
  static void SetSlotHint(size_t) {}
#endif

  Slot slots_[kCapacity];
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_OBJECT_POOL_H_
