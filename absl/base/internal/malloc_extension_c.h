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

 * C shims for the C++ malloc_extension.h.  See malloc_extension.h for
 * details.  Note these C shims always work on
 * MallocExtension::instance(); it is not possible to have more than
 * one MallocExtension object in C applications.
 */

#ifndef ABSL_BASE_INTERNAL_MALLOC_EXTENSION_C_H_
#define ABSL_BASE_INTERNAL_MALLOC_EXTENSION_C_H_

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define kMallocExtensionHistogramSize 64

int MallocExtension_VerifyAllMemory(void);
int MallocExtension_VerifyNewMemory(const void* p);
int MallocExtension_VerifyArrayNewMemory(const void* p);
int MallocExtension_VerifyMallocMemory(const void* p);
int MallocExtension_MallocMemoryStats(int* blocks, size_t* total,
                                      int histogram[kMallocExtensionHistogramSize]);

void MallocExtension_GetStats(char* buffer, int buffer_length);

/* TODO(csilvers): write a C version of these routines, that perhaps
 * takes a function ptr and a void *.
 */
/* void MallocExtension_GetHeapSample(MallocExtensionWriter* result); */
/* void MallocExtension_GetHeapGrowthStacks(MallocExtensionWriter* result); */

int MallocExtension_GetNumericProperty(const char* property, size_t* value);
int MallocExtension_SetNumericProperty(const char* property, size_t value);
void MallocExtension_MarkThreadIdle(void);
void MallocExtension_MarkThreadBusy(void);
void MallocExtension_ReleaseToSystem(size_t num_bytes);
void MallocExtension_ReleaseFreeMemory(void);
size_t MallocExtension_GetEstimatedAllocatedSize(size_t size);
size_t MallocExtension_GetAllocatedSize(const void* p);

/*
 * NOTE: These enum values MUST be kept in sync with the version in
 *       malloc_extension.h
 */
typedef enum {
  MallocExtension_kUnknownOwnership = 0,
  MallocExtension_kOwned,
  MallocExtension_kNotOwned
} MallocExtension_Ownership;

MallocExtension_Ownership MallocExtension_GetOwnership(const void* p);


#ifdef __cplusplus
}   // extern "C"
#endif

#endif  /* ABSL_BASE_INTERNAL_MALLOC_EXTENSION_C_H_ */
