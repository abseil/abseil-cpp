/*
 * Copyright 2017 The Abseil Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * C shims for the C++ malloc_hook.h.  See malloc_hook.h for details
 * on how to use these.
 */
#ifndef ABSL_BASE_INTERNAL_MALLOC_HOOK_C_H_
#define ABSL_BASE_INTERNAL_MALLOC_HOOK_C_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef int (*MallocHook_GetStackTraceFn)(void**, int, int);
typedef void (*MallocHook_NewHook)(const void* ptr, size_t size);
typedef void (*MallocHook_DeleteHook)(const void* ptr);
typedef int64_t MallocHook_AllocHandle;
typedef struct {
  /* See malloc_hook.h  for documentation for this struct. */
  MallocHook_AllocHandle handle;
  size_t allocated_size;
  int stack_depth;
  const void* stack;
} MallocHook_SampledAlloc;
typedef void (*MallocHook_SampledNewHook)(
    const MallocHook_SampledAlloc* sampled_alloc);
typedef void (*MallocHook_SampledDeleteHook)(MallocHook_AllocHandle handle);
typedef void (*MallocHook_PreMmapHook)(const void* start, size_t size,
                                       int protection, int flags, int fd,
                                       off_t offset);
typedef void (*MallocHook_MmapHook)(const void* result, const void* start,
                                    size_t size, int protection, int flags,
                                    int fd, off_t offset);
typedef int (*MallocHook_MmapReplacement)(const void* start, size_t size,
                                          int protection, int flags, int fd,
                                          off_t offset, void** result);
typedef void (*MallocHook_MunmapHook)(const void* start, size_t size);
typedef int (*MallocHook_MunmapReplacement)(const void* start, size_t size,
                                            int* result);
typedef void (*MallocHook_MremapHook)(const void* result, const void* old_addr,
                                      size_t old_size, size_t new_size,
                                      int flags, const void* new_addr);
typedef void (*MallocHook_PreSbrkHook)(ptrdiff_t increment);
typedef void (*MallocHook_SbrkHook)(const void* result, ptrdiff_t increment);

#ifdef __cplusplus
}  /* extern "C" */
#endif  /* __cplusplus */

#endif  /* ABSL_BASE_INTERNAL_MALLOC_HOOK_C_H_ */
