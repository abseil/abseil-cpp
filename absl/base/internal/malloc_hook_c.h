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

/* Get the current stack trace.  Try to skip all routines up to and
 * including the caller of MallocHook::Invoke*.
 * Use "skip_count" (similarly to absl::GetStackTrace from stacktrace.h)
 * as a hint about how many routines to skip if better information
 * is not available.
 */
typedef int (*MallocHook_GetStackTraceFn)(void**, int, int);
int MallocHook_GetCallerStackTrace(void** result, int max_depth, int skip_count,
                                   MallocHook_GetStackTraceFn fn);

/* All the MallocHook_{Add,Remove}*Hook functions below return 1 on success
 * and 0 on failure.
 */

typedef void (*MallocHook_NewHook)(const void* ptr, size_t size);
int MallocHook_AddNewHook(MallocHook_NewHook hook);
int MallocHook_RemoveNewHook(MallocHook_NewHook hook);

typedef void (*MallocHook_DeleteHook)(const void* ptr);
int MallocHook_AddDeleteHook(MallocHook_DeleteHook hook);
int MallocHook_RemoveDeleteHook(MallocHook_DeleteHook hook);

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
int MallocHook_AddSampledNewHook(MallocHook_SampledNewHook hook);
int MallocHook_RemoveSampledNewHook(MallocHook_SampledNewHook hook);

typedef void (*MallocHook_SampledDeleteHook)(MallocHook_AllocHandle handle);
int MallocHook_AddSampledDeleteHook(MallocHook_SampledDeleteHook hook);
int MallocHook_RemoveSampledDeleteHook(MallocHook_SampledDeleteHook hook);

typedef void (*MallocHook_PreMmapHook)(const void *start,
                                       size_t size,
                                       int protection,
                                       int flags,
                                       int fd,
                                       off_t offset);
int MallocHook_AddPreMmapHook(MallocHook_PreMmapHook hook);
int MallocHook_RemovePreMmapHook(MallocHook_PreMmapHook hook);

typedef void (*MallocHook_MmapHook)(const void* result,
                                    const void* start,
                                    size_t size,
                                    int protection,
                                    int flags,
                                    int fd,
                                    off_t offset);
int MallocHook_AddMmapHook(MallocHook_MmapHook hook);
int MallocHook_RemoveMmapHook(MallocHook_MmapHook hook);

typedef int (*MallocHook_MmapReplacement)(const void* start,
                                          size_t size,
                                          int protection,
                                          int flags,
                                          int fd,
                                          off_t offset,
                                          void** result);
int MallocHook_SetMmapReplacement(MallocHook_MmapReplacement hook);
int MallocHook_RemoveMmapReplacement(MallocHook_MmapReplacement hook);

typedef void (*MallocHook_MunmapHook)(const void* start, size_t size);
int MallocHook_AddMunmapHook(MallocHook_MunmapHook hook);
int MallocHook_RemoveMunmapHook(MallocHook_MunmapHook hook);

typedef int (*MallocHook_MunmapReplacement)(const void* start,
                                            size_t size,
                                            int* result);
int MallocHook_SetMunmapReplacement(MallocHook_MunmapReplacement hook);
int MallocHook_RemoveMunmapReplacement(MallocHook_MunmapReplacement hook);

typedef void (*MallocHook_MremapHook)(const void* result,
                                      const void* old_addr,
                                      size_t old_size,
                                      size_t new_size,
                                      int flags,
                                      const void* new_addr);
int MallocHook_AddMremapHook(MallocHook_MremapHook hook);
int MallocHook_RemoveMremapHook(MallocHook_MremapHook hook);

typedef void (*MallocHook_PreSbrkHook)(ptrdiff_t increment);
int MallocHook_AddPreSbrkHook(MallocHook_PreSbrkHook hook);
int MallocHook_RemovePreSbrkHook(MallocHook_PreSbrkHook hook);

typedef void (*MallocHook_SbrkHook)(const void* result, ptrdiff_t increment);
int MallocHook_AddSbrkHook(MallocHook_SbrkHook hook);
int MallocHook_RemoveSbrkHook(MallocHook_SbrkHook hook);

#ifdef __cplusplus
}  /* extern "C" */
#endif  /* __cplusplus */

#endif  /* ABSL_BASE_INTERNAL_MALLOC_HOOK_C_H_ */
