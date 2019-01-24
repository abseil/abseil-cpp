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

#include <cstddef>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/internal/hashtablez_sampler.h"

namespace absl {
namespace container_internal {

class HashtablezInfoHandlePeer {
 public:
  static bool IsSampled(const HashtablezInfoHandle& h) {
    return h.info_ != nullptr;
  }
};

namespace {

bool samples[3]{true, true, true};

// We do this test in a global object to test that this works even before main.
struct Global {
  Global() {
    // By default it is sampled.
    samples[0] = HashtablezInfoHandlePeer::IsSampled(Sample());

    // Even with a large parameter, it is sampled.
    SetHashtablezSampleParameter(100);
    samples[1] = HashtablezInfoHandlePeer::IsSampled(Sample());

    // Even if we turn it off, it is still sampled.
    SetHashtablezEnabled(false);
    samples[2] = HashtablezInfoHandlePeer::IsSampled(Sample());
  }
} global;

TEST(kAbslContainerInternalSampleEverything, Works) {
  EXPECT_THAT(samples, testing::Each(true));
  EXPECT_TRUE(kAbslContainerInternalSampleEverything);
  // One more after main()
  EXPECT_TRUE(HashtablezInfoHandlePeer::IsSampled(Sample()));
}

}  // namespace
}  // namespace container_internal
}  // namespace absl
