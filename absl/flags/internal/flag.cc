//
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

#include "absl/flags/internal/flag.h"

#include "absl/synchronization/mutex.h"

namespace absl {
namespace flags_internal {

// If the flag has a mutation callback this function invokes it. While the
// callback is being invoked the primary flag's mutex is unlocked and it is
// re-locked back after call to callback is completed. Callback invocation is
// guarded by flag's secondary mutex instead which prevents concurrent
// callback invocation. Note that it is possible for other thread to grab the
// primary lock and update flag's value at any time during the callback
// invocation. This is by design. Callback can get a value of the flag if
// necessary, but it might be different from the value initiated the callback
// and it also can be different by the time the callback invocation is
// completed. Requires that *primary_lock be held in exclusive mode; it may be
// released and reacquired by the implementation.
void InvokeCallback(absl::Mutex* primary_mu, absl::Mutex* callback_mu,
                    FlagCallback cb) ABSL_EXCLUSIVE_LOCKS_REQUIRED(primary_mu) {
  if (!cb) return;

  // When executing the callback we need the primary flag's mutex to be
  // unlocked so that callback can retrieve the flag's value.
  primary_mu->Unlock();

  {
    absl::MutexLock lock(callback_mu);
    cb();
  }

  primary_mu->Lock();
}

}  // namespace flags_internal
}  // namespace absl
