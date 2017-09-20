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

#include "absl/base/internal/malloc_extension.h"

#include <assert.h>
#include <string.h>
#include <atomic>
#include <string>

#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/malloc_extension_c.h"

namespace absl {
namespace base_internal {

// SysAllocator implementation
SysAllocator::~SysAllocator() {}
void SysAllocator::GetStats(char* buffer, int) { buffer[0] = 0; }

// Default implementation -- does nothing
MallocExtension::~MallocExtension() { }
bool MallocExtension::VerifyAllMemory() { return true; }
bool MallocExtension::VerifyNewMemory(const void*) { return true; }
bool MallocExtension::VerifyArrayNewMemory(const void*) { return true; }
bool MallocExtension::VerifyMallocMemory(const void*) { return true; }

bool MallocExtension::GetNumericProperty(const char*, size_t*) {
  return false;
}

bool MallocExtension::SetNumericProperty(const char*, size_t) {
  return false;
}

void MallocExtension::GetStats(char* buffer, int length) {
  assert(length > 0);
  static_cast<void>(length);
  buffer[0] = '\0';
}

bool MallocExtension::MallocMemoryStats(int* blocks, size_t* total,
                                        int histogram[kMallocHistogramSize]) {
  *blocks = 0;
  *total = 0;
  memset(histogram, 0, sizeof(*histogram) * kMallocHistogramSize);
  return true;
}

void MallocExtension::MarkThreadIdle() {
  // Default implementation does nothing
}

void MallocExtension::MarkThreadBusy() {
  // Default implementation does nothing
}

SysAllocator* MallocExtension::GetSystemAllocator() {
  return nullptr;
}

void MallocExtension::SetSystemAllocator(SysAllocator*) {
  // Default implementation does nothing
}

void MallocExtension::ReleaseToSystem(size_t) {
  // Default implementation does nothing
}

void MallocExtension::ReleaseFreeMemory() {
  ReleaseToSystem(static_cast<size_t>(-1));   // SIZE_T_MAX
}

void MallocExtension::SetMemoryReleaseRate(double) {
  // Default implementation does nothing
}

double MallocExtension::GetMemoryReleaseRate() {
  return -1.0;
}

size_t MallocExtension::GetEstimatedAllocatedSize(size_t size) {
  return size;
}

size_t MallocExtension::GetAllocatedSize(const void* p) {
  assert(GetOwnership(p) != kNotOwned);
  static_cast<void>(p);
  return 0;
}

MallocExtension::Ownership MallocExtension::GetOwnership(const void*) {
  return kUnknownOwnership;
}

void MallocExtension::GetProperties(MallocExtension::StatLevel,
                                    std::map<std::string, Property>* result) {
  result->clear();
}

size_t MallocExtension::ReleaseCPUMemory(int) {
  return 0;
}

// The current malloc extension object.

std::atomic<MallocExtension*> MallocExtension::current_instance_;

MallocExtension* MallocExtension::InitModule() {
  MallocExtension* ext = new MallocExtension;
  current_instance_.store(ext, std::memory_order_release);
  return ext;
}

void MallocExtension::Register(MallocExtension* implementation) {
  InitModuleOnce();
  // When running under valgrind, our custom malloc is replaced with
  // valgrind's one and malloc extensions will not work.  (Note:
  // callers should be responsible for checking that they are the
  // malloc that is really being run, before calling Register.  This
  // is just here as an extra sanity check.)
  // Under compiler-based ThreadSanitizer RunningOnValgrind() returns true,
  // but we still want to use malloc extensions.
#ifndef THREAD_SANITIZER
  if (RunningOnValgrind()) {
    return;
  }
#endif  // #ifndef THREAD_SANITIZER
  current_instance_.store(implementation, std::memory_order_release);
}
void MallocExtension::GetHeapSample(MallocExtensionWriter*) {}

void MallocExtension::GetHeapGrowthStacks(MallocExtensionWriter*) {}

void MallocExtension::GetFragmentationProfile(MallocExtensionWriter*) {}

}  // namespace base_internal
}  // namespace absl

// These are C shims that work on the current instance.

#define C_SHIM(fn, retval, paramlist, arglist)                           \
  extern "C" retval MallocExtension_##fn paramlist {                     \
    return absl::base_internal::MallocExtension::instance()->fn arglist; \
  }

C_SHIM(VerifyAllMemory, int, (void), ());
C_SHIM(VerifyNewMemory, int, (const void* p), (p));
C_SHIM(VerifyArrayNewMemory, int, (const void* p), (p));
C_SHIM(VerifyMallocMemory, int, (const void* p), (p));
C_SHIM(
    MallocMemoryStats, int,
    (int* blocks, size_t* total,
     int histogram[absl::base_internal::MallocExtension::kMallocHistogramSize]),
    (blocks, total, histogram));

C_SHIM(GetStats, void,
       (char* buffer, int buffer_length), (buffer, buffer_length));
C_SHIM(GetNumericProperty, int,
       (const char* property, size_t* value), (property, value));
C_SHIM(SetNumericProperty, int,
       (const char* property, size_t value), (property, value));

C_SHIM(MarkThreadIdle, void, (void), ());
C_SHIM(MarkThreadBusy, void, (void), ());
C_SHIM(ReleaseFreeMemory, void, (void), ());
C_SHIM(ReleaseToSystem, void, (size_t num_bytes), (num_bytes));
C_SHIM(GetEstimatedAllocatedSize, size_t, (size_t size), (size));
C_SHIM(GetAllocatedSize, size_t, (const void* p), (p));

// Can't use the shim here because of the need to translate the enums.
extern "C"
MallocExtension_Ownership MallocExtension_GetOwnership(const void* p) {
  return static_cast<MallocExtension_Ownership>(
      absl::base_internal::MallocExtension::instance()->GetOwnership(p));
}

// Default implementation just returns size. The expectation is that
// the linked-in malloc implementation might provide an override of
// this weak function with a better implementation.
ABSL_ATTRIBUTE_WEAK ABSL_ATTRIBUTE_NOINLINE size_t nallocx(size_t size, int) {
  return size;
}
