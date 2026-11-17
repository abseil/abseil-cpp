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

#include "absl/container/internal/raw_linked_hash_set.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;

std::vector<int> CollectValues(const LinkedListSentinel<int>& sentinel) {
  std::vector<int> out;
  const void* end = sentinel.EndPtr();
  for (const void* cur = sentinel.head.next; cur != end;
       cur = static_cast<const LinkedListNode<int>*>(cur)->link.next) {
    out.push_back(static_cast<const LinkedListNode<int>*>(cur)->storage.value);
  }
  return out;
}

TEST(RawLinkedHashSetTest, IntrusiveListLinkAndSentinel) {
  using Node = LinkedListNode<int>;
  std::allocator<int> alloc;
  LinkedListSentinel<int> a;
  LinkedListSentinel<int> b;
  EXPECT_TRUE(a.empty());

  auto* n1 = Node::New(alloc, 10);
  auto* n2 = Node::New(alloc, 20);
  auto* n3 = Node::New(alloc, 30);
  Node::LinkBefore(a.EndPtr(), n1);
  Node::LinkBefore(a.EndPtr(), n2);
  Node::LinkBefore(n2, n3);
  EXPECT_THAT(CollectValues(a), ElementsAre(10, 30, 20));

  b.TakeFrom(a);
  EXPECT_TRUE(a.empty());
  EXPECT_THAT(CollectValues(b), ElementsAre(10, 30, 20));
  EXPECT_EQ(Node::LinkOf(b.head.next)->prev, b.EndPtr());
  EXPECT_EQ(Node::LinkOf(b.head.prev)->next, b.EndPtr());

  auto* n4 = Node::New(alloc, 40);
  Node::LinkBefore(a.EndPtr(), n4);
  a.Swap(b);
  EXPECT_THAT(CollectValues(a), ElementsAre(10, 30, 20));
  EXPECT_THAT(CollectValues(b), ElementsAre(40));
  EXPECT_EQ(Node::LinkOf(a.head.prev)->next, a.EndPtr());
  EXPECT_EQ(Node::LinkOf(b.head.prev)->next, b.EndPtr());

  for (LinkedListSentinel<int>* list : {&a, &b}) {
    while (!list->empty()) {
      auto* node = static_cast<Node*>(list->head.next);
      Node::Unlink(node);
      Node::Delete(alloc, node);
    }
  }
}

TEST(RawLinkedHashSetTest, RawLinkedHashSetAndMap) {
  using RawSet = raw_linked_hash_set<LinkedHashSetPolicy<int>>;
  using RawMap = raw_linked_hash_set<LinkedHashMapPolicy<int, std::string>>;

  RawSet s = {3, 1, 2};
  EXPECT_THAT(s, ElementsAre(3, 1, 2));
  EXPECT_EQ(s.front(), 3);
  EXPECT_EQ(s.back(), 2);

  auto n1 = s.extract(1);
  auto n2 = s.extract(2);
  using std::swap;
  swap(n1, n2);
  EXPECT_EQ(n1.value(), 2);
  EXPECT_EQ(n2.value(), 1);
  n2.value() = 4;
  EXPECT_TRUE(s.insert(std::move(n1)).inserted);
  EXPECT_TRUE(s.insert(std::move(n2)).inserted);
  EXPECT_THAT(s, ElementsAre(3, 2, 4));

  RawMap m = {{3, "c"}, {1, "a"}, {2, "b"}};
  EXPECT_THAT(m, ElementsAre(Pair(3, "c"), Pair(1, "a"), Pair(2, "b")));
  auto mn = m.extract(1);
  ASSERT_TRUE(mn);
  EXPECT_EQ(mn.key(), 1);
  mn.mapped() = "aa";
  EXPECT_TRUE(m.insert(std::move(mn)).inserted);
  EXPECT_THAT(m, ElementsAre(Pair(3, "c"), Pair(2, "b"), Pair(1, "aa")));

  struct alignas(64) AlignedVal {
    int x;
  };
  raw_linked_hash_set<LinkedHashMapPolicy<int, AlignedVal>> aligned_map;
  aligned_map.emplace(1, AlignedVal{7});
  aligned_map.emplace(2, AlignedVal{9});
  EXPECT_EQ(aligned_map.rbegin()->second.x, 9);
  aligned_map.pop_back();
  EXPECT_EQ(aligned_map.back().second.x, 7);
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
