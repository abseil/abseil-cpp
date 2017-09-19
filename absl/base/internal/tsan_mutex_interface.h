// Copyright 2017 The Abseil Authors.
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
//
// This file is intended solely for spinlock.h.
// It provides ThreadSanitizer annotations for custom mutexes.
// See <sanitizer/tsan_interface.h> for meaning of these annotations.

#ifndef ABSL_BASE_INTERNAL_TSAN_MUTEX_INTERFACE_H_
#define ABSL_BASE_INTERNAL_TSAN_MUTEX_INTERFACE_H_

#ifdef THREAD_SANITIZER
#include <sanitizer/tsan_interface.h>

#define ABSL_TSAN_MUTEX_CREATE __tsan_mutex_create
#define ABSL_TSAN_MUTEX_DESTROY __tsan_mutex_destroy
#define ABSL_TSAN_MUTEX_PRE_LOCK __tsan_mutex_pre_lock
#define ABSL_TSAN_MUTEX_POST_LOCK __tsan_mutex_post_lock
#define ABSL_TSAN_MUTEX_PRE_UNLOCK __tsan_mutex_pre_unlock
#define ABSL_TSAN_MUTEX_POST_UNLOCK __tsan_mutex_post_unlock
#define ABSL_TSAN_MUTEX_PRE_SIGNAL __tsan_mutex_pre_signal
#define ABSL_TSAN_MUTEX_POST_SIGNAL __tsan_mutex_post_signal
#define ABSL_TSAN_MUTEX_PRE_DIVERT __tsan_mutex_pre_divert
#define ABSL_TSAN_MUTEX_POST_DIVERT __tsan_mutex_post_divert

#else

#define ABSL_TSAN_MUTEX_CREATE(...)
#define ABSL_TSAN_MUTEX_DESTROY(...)
#define ABSL_TSAN_MUTEX_PRE_LOCK(...)
#define ABSL_TSAN_MUTEX_POST_LOCK(...)
#define ABSL_TSAN_MUTEX_PRE_UNLOCK(...)
#define ABSL_TSAN_MUTEX_POST_UNLOCK(...)
#define ABSL_TSAN_MUTEX_PRE_SIGNAL(...)
#define ABSL_TSAN_MUTEX_POST_SIGNAL(...)
#define ABSL_TSAN_MUTEX_PRE_DIVERT(...)
#define ABSL_TSAN_MUTEX_POST_DIVERT(...)

#endif

#endif  // ABSL_BASE_INTERNAL_TSAN_MUTEX_INTERFACE_H_
