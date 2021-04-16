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

#include "absl/strings/internal/cordz_info.h"

#include "absl/base/config.h"
#include "absl/debugging/stacktrace.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cordz_handle.h"
#include "absl/strings/internal/cordz_statistics.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

constexpr int CordzInfo::kMaxStackDepth;

ABSL_CONST_INIT std::atomic<CordzInfo*> CordzInfo::ci_head_{nullptr};
ABSL_CONST_INIT absl::Mutex CordzInfo::ci_mutex_(absl::kConstInit);

CordzInfo* CordzInfo::Head(const CordzSnapshot& snapshot) {
  ABSL_ASSERT(snapshot.is_snapshot());
  ABSL_ASSERT(snapshot.DiagnosticsHandleIsSafeToInspect(ci_head_unsafe()));
  return ci_head_unsafe();
}

CordzInfo* CordzInfo::Next(const CordzSnapshot& snapshot) const {
  ABSL_ASSERT(snapshot.is_snapshot());
  ABSL_ASSERT(snapshot.DiagnosticsHandleIsSafeToInspect(this));
  ABSL_ASSERT(snapshot.DiagnosticsHandleIsSafeToInspect(ci_next_unsafe()));
  return ci_next_unsafe();
}

CordzInfo* CordzInfo::TrackCord(CordRep* rep, const CordzInfo* src) {
  CordzInfo* ci = new CordzInfo(rep);
  if (src) {
    ci->parent_stack_depth_ = src->stack_depth_;
    memcpy(ci->parent_stack_, src->stack_, sizeof(void*) * src->stack_depth_);
  }
  ci->Track();
  return ci;
}

CordzInfo* CordzInfo::TrackCord(CordRep* rep) {
  return TrackCord(rep, nullptr);
}

void CordzInfo::UntrackCord(CordzInfo* cordz_info) {
  assert(cordz_info);
  if (cordz_info) {
    cordz_info->Untrack();
    CordzHandle::Delete(cordz_info);
  }
}

CordzInfo::CordzInfo(CordRep* rep)
    : rep_(rep),
      stack_depth_(absl::GetStackTrace(stack_, /*max_depth=*/kMaxStackDepth,
                                       /*skip_count=*/1)),
      parent_stack_depth_(0),
      create_time_(absl::Now()) {}

CordzInfo::~CordzInfo() {
  // `rep_` is potentially kept alive if CordzInfo is included
  // in a collection snapshot (which should be rare).
  if (ABSL_PREDICT_FALSE(rep_)) {
    CordRep::Unref(rep_);
  }
}

void CordzInfo::Track() {
  absl::MutexLock l(&ci_mutex_);

  CordzInfo* const head = ci_head_.load(std::memory_order_acquire);
  if (head != nullptr) {
    head->ci_prev_.store(this, std::memory_order_release);
  }
  ci_next_.store(head, std::memory_order_release);
  ci_head_.store(this, std::memory_order_release);
}

void CordzInfo::Untrack() {
  {
    // TODO(b/117940323): change this to assuming ownership instead once all
    // Cord logic is properly keeping `rep_` in sync with the Cord root rep.
    absl::MutexLock lock(&mutex());
    rep_ = nullptr;
  }

  absl::MutexLock l(&ci_mutex_);

  CordzInfo* const head = ci_head_.load(std::memory_order_acquire);
  CordzInfo* const next = ci_next_.load(std::memory_order_acquire);
  CordzInfo* const prev = ci_prev_.load(std::memory_order_acquire);

  if (next) {
    ABSL_ASSERT(next->ci_prev_.load(std::memory_order_acquire) == this);
    next->ci_prev_.store(prev, std::memory_order_release);
  }
  if (prev) {
    ABSL_ASSERT(head != this);
    ABSL_ASSERT(prev->ci_next_.load(std::memory_order_acquire) == this);
    prev->ci_next_.store(next, std::memory_order_release);
  } else {
    ABSL_ASSERT(head == this);
    ci_head_.store(next, std::memory_order_release);
  }
}

void CordzInfo::SetCordRep(CordRep* rep) {
  mutex().AssertHeld();
  rep_ = rep;
}

absl::Span<void* const> CordzInfo::GetStack() const {
  return absl::MakeConstSpan(stack_, stack_depth_);
}

absl::Span<void* const> CordzInfo::GetParentStack() const {
  return absl::MakeConstSpan(parent_stack_, parent_stack_depth_);
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
