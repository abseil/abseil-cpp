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

#include "absl/container/internal/btree.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/container/internal/container_memory.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

ABSL_CONST_INIT ABSL_DLL const BtreeEmptyNode<uint8_t> kBtreeEmptyNode8{};
ABSL_CONST_INIT ABSL_DLL const BtreeEmptyNode<uint16_t> kBtreeEmptyNode16{};

namespace {

// Returns the aligned byte size of a leaf node for the given slot count.
inline size_t LeafSize(const BtreeDtorPolicy& policy, size_t slot_count) {
  return policy.LeafSize(slot_count);
}

// Returns the byte size of an internal node.
inline size_t InternalSize(size_t children_offset, size_t node_slots) {
  return children_offset + (node_slots + 1) * sizeof(void*);
}

// Returns a pointer to the slot at the given index within node.
inline void* NodeSlot(void* node, size_t slots_offset, size_t slot_size,
                      size_t i) {
  return static_cast<char*>(node) + slots_offset + i * slot_size;
}

// Returns the slot capacity of a leaf or internal node.
template <typename FieldType>
FieldType NodeMaxCount(const btree_node_base<FieldType>* node,
                       uint16_t node_slots) {
  return node->max_count(static_cast<FieldType>(node_slots));
}

// Null callback selects default pointer-aligned node deletion.
inline void DeallocateNodeWithFn(void* node, size_t size,
                                 BtreeDeallocFn dealloc, void* alloc) {
  SanitizerUnpoisonMemoryRegion(node, size);
  if (ABSL_PREDICT_TRUE(dealloc == nullptr)) {
    std::allocator<char> default_alloc;
    Deallocate<alignof(void*)>(&default_alloc, node, size);
    return;
  }
  dealloc(alloc, node, size);
}

// Destroys all elements and frees every node in the subtree.
template <typename FieldType>
void ClearAndDeleteSubtree(btree_node_base<FieldType>* node,
                           const BtreeDtorPolicy& policy,
                           BtreeDeallocFn dealloc, void* alloc) {
  assert(node->count() > 0);
  const size_t slot_size = policy.slot_size;
  const size_t slots_offset = policy.slots_offset;
  const uint16_t node_slots = policy.node_slots;
  const BtreeDestroyNFn destroy_n = policy.destroy_n;

  const auto destroy_node_slots = [&](btree_node_base<FieldType>* n) {
    n->next_generation();
    const size_t count = n->count();
    if (count == 0) return;
    void* slots = NodeSlot(n, slots_offset, slot_size, n->start());
    if (destroy_n != nullptr) {
      destroy_n(alloc, slots, count);
    }
    SanitizerPoisonMemoryRegion(slots, count * slot_size);
  };

  if (node->is_leaf()) {
    destroy_node_slots(node);
    DeallocateNodeWithFn(node, LeafSize(policy, NodeMaxCount(node, node_slots)),
                         dealloc, alloc);
    return;
  }

  const size_t coff = policy.ComputeChildrenOffset();
  const size_t internal_size = InternalSize(coff, node_slots);

  btree_node_base<FieldType>* delete_root_parent = node->parent();

  // Navigate to the leftmost leaf under node, and then delete upwards.
  while (node->is_internal()) node = node->start_child(coff);
  // When generations are enabled, we delete the leftmost leaf last in case it's
  // the parent of the root and we need to check whether it's a leaf before we
  // can update the root's generation.
  btree_node_base<FieldType>* leftmost_leaf = node;

  // Use `size_t` because `pos` needs to be able to hold `kNodeSlots+1`,
  // which isn't guaranteed to be a valid `FieldType`.
  size_t pos = node->position();
  btree_node_base<FieldType>* parent = node->parent();
  for (;;) {
    // In each iteration of the next loop, we delete one leaf node and go right.
    assert(pos <= parent->finish());
    do {
      node = parent->child(coff, pos);
      if (node->is_internal()) {
        // Navigate to the leftmost leaf under node.
        while (node->is_internal()) node = node->start_child(coff);
        pos = node->position();
        parent = node->parent();
      }
      destroy_node_slots(node);
      if (!BtreeGenerationsEnabled() || leftmost_leaf != node) {
        DeallocateNodeWithFn(node,
                             LeafSize(policy, NodeMaxCount(node, node_slots)),
                             dealloc, alloc);
      }
      ++pos;
    } while (pos <= parent->finish());

    // Once we've deleted all children of parent, delete parent and go up/right.
    assert(pos > parent->finish());
    do {
      node = parent;
      pos = node->position();
      parent = node->parent();
      destroy_node_slots(node);
      DeallocateNodeWithFn(node, internal_size, dealloc, alloc);
      if (parent == delete_root_parent) {
        if constexpr (BtreeGenerationsEnabled()) {
          DeallocateNodeWithFn(
              leftmost_leaf,
              LeafSize(policy, NodeMaxCount(leftmost_leaf, node_slots)),
              dealloc, alloc);
        }
        return;
      }
      ++pos;
    } while (pos > parent->finish());
  }
}

}  // namespace

// Destroys all elements and frees every node using the default deallocator.
template <typename FieldType>
void BtreeDestroy(btree_node_base<FieldType>* root,
                  const BtreeDtorPolicy& policy) {
  ClearAndDeleteSubtree(root, policy, nullptr, nullptr);
}

// Destroys all elements and frees every node using a custom deallocator.
template <typename FieldType>
void BtreeDestroy(btree_node_base<FieldType>* root,
                  const BtreeDtorPolicy& policy, BtreeDeallocFn dealloc,
                  void* alloc) {
  ClearAndDeleteSubtree(root, policy, dealloc, alloc);
}

template void BtreeDestroy<uint8_t>(btree_node_base<uint8_t>*,
                                    const BtreeDtorPolicy&);
template void BtreeDestroy<uint16_t>(btree_node_base<uint16_t>*,
                                     const BtreeDtorPolicy&);
template void BtreeDestroy<uint8_t>(btree_node_base<uint8_t>*,
                                    const BtreeDtorPolicy&, BtreeDeallocFn,
                                    void*);
template void BtreeDestroy<uint16_t>(btree_node_base<uint16_t>*,
                                     const BtreeDtorPolicy&, BtreeDeallocFn,
                                     void*);

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
