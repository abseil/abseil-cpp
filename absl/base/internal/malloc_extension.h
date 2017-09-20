//
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

// Extra extensions exported by some malloc implementations.  These
// extensions are accessed through a virtual base class so an
// application can link against a malloc that does not implement these
// extensions, and it will get default versions that do nothing.
//
// NOTE FOR C USERS: If you wish to use this functionality from within
// a C program, see malloc_extension_c.h.

#ifndef ABSL_BASE_INTERNAL_MALLOC_EXTENSION_H_
#define ABSL_BASE_INTERNAL_MALLOC_EXTENSION_H_

#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/port.h"
namespace absl {
namespace base_internal {

class MallocExtensionWriter;

// Interface to a pluggable system allocator.
class SysAllocator {
 public:
  SysAllocator() {
  }
  virtual ~SysAllocator();

  // Allocates "size"-byte of memory from system aligned with "alignment".
  // Returns null if failed. Otherwise, the returned pointer p up to and
  // including (p + actual_size -1) have been allocated.
  virtual void* Alloc(size_t size, size_t *actual_size, size_t alignment) = 0;

  // Get a human-readable description of the current state of the
  // allocator.  The state is stored as a null-terminated std::string in
  // a prefix of buffer.
  virtual void GetStats(char* buffer, int length);
};

// The default implementations of the following routines do nothing.
// All implementations should be thread-safe; the current ones
// (DebugMallocImplementation and TCMallocImplementation) are.
class MallocExtension {
 public:
  virtual ~MallocExtension();

  // Verifies that all blocks are valid.  Returns true if all are; dumps
  // core otherwise.  A no-op except in debug mode.  Even in debug mode,
  // they may not do any checking except with certain malloc
  // implementations.  Thread-safe.
  virtual bool VerifyAllMemory();

  // Verifies that p was returned by new, has not been deleted, and is
  // valid.  Returns true if p is good; dumps core otherwise.  A no-op
  // except in debug mode.  Even in debug mode, may not do any checking
  // except with certain malloc implementations.  Thread-safe.
  virtual bool VerifyNewMemory(const void* p);

  // Verifies that p was returned by new[], has not been deleted, and is
  // valid.  Returns true if p is good; dumps core otherwise.  A no-op
  // except in debug mode.  Even in debug mode, may not do any checking
  // except with certain malloc implementations.  Thread-safe.
  virtual bool VerifyArrayNewMemory(const void* p);

  // Verifies that p was returned by malloc, has not been freed, and is
  // valid.  Returns true if p is good; dumps core otherwise.  A no-op
  // except in debug mode.  Even in debug mode, may not do any checking
  // except with certain malloc implementations.  Thread-safe.
  virtual bool VerifyMallocMemory(const void* p);

  // If statistics collection is enabled, sets *blocks to be the number of
  // currently allocated blocks, sets *total to be the total size allocated
  // over all blocks, sets histogram[n] to be the number of blocks with
  // size between 2^n-1 and 2^(n+1), and returns true.  Returns false, and
  // does not change *blocks, *total, or *histogram, if statistics
  // collection is disabled.
  //
  // Note that these statistics reflect memory allocated by new, new[],
  // malloc(), and realloc(), but not mmap().  They may be larger (if not
  // all pages have been written to) or smaller (if pages have been
  // allocated by mmap()) than the total RSS size.  They will always be
  // smaller than the total virtual memory size.
  static constexpr int kMallocHistogramSize = 64;
  virtual bool MallocMemoryStats(int* blocks, size_t* total,
                                 int histogram[kMallocHistogramSize]);

  // Get a human readable description of the current state of the malloc
  // data structures.  The state is stored as a null-terminated std::string
  // in a prefix of "buffer[0,buffer_length-1]".
  // REQUIRES: buffer_length > 0.
  virtual void GetStats(char* buffer, int buffer_length);

  // Outputs to "writer" a sample of live objects and the stack traces
  // that allocated these objects. The output can be passed to pprof.
  virtual void GetHeapSample(MallocExtensionWriter* writer);

  // Outputs to "writer" the stack traces that caused growth in the
  // address space size. The output can be passed to "pprof".
  virtual void GetHeapGrowthStacks(MallocExtensionWriter* writer);

  // Outputs to "writer" a fragmentation profile. The output can be
  // passed to "pprof".  In particular, the result is a list of
  // <n,total,stacktrace> tuples that says that "total" bytes in "n"
  // objects are currently unusable because of fragmentation caused by
  // an allocation with the specified "stacktrace".
  virtual void GetFragmentationProfile(MallocExtensionWriter* writer);

  // -------------------------------------------------------------------
  // Control operations for getting and setting malloc implementation
  // specific parameters.  Some currently useful properties:
  //
  // generic
  // -------
  // "generic.current_allocated_bytes"
  //      Number of bytes currently allocated by application
  //      This property is not writable.
  //
  // "generic.heap_size"
  //      Number of bytes in the heap ==
  //            current_allocated_bytes +
  //            fragmentation +
  //            freed memory regions
  //      This property is not writable.
  //
  // tcmalloc
  // --------
  // "tcmalloc.max_total_thread_cache_bytes"
  //      Upper limit on total number of bytes stored across all
  //      per-thread caches.  Default: 16MB.
  //
  // "tcmalloc.current_total_thread_cache_bytes"
  //      Number of bytes used across all thread caches.
  //      This property is not writable.
  //
  // "tcmalloc.pageheap_free_bytes"
  //      Number of bytes in free, mapped pages in page heap.  These
  //      bytes can be used to fulfill allocation requests.  They
  //      always count towards virtual memory usage, and unless the
  //      underlying memory is swapped out by the OS, they also count
  //      towards physical memory usage.  This property is not writable.
  //
  // "tcmalloc.pageheap_unmapped_bytes"
  //      Number of bytes in free, unmapped pages in page heap.
  //      These are bytes that have been released back to the OS,
  //      possibly by one of the MallocExtension "Release" calls.
  //      They can be used to fulfill allocation requests, but
  //      typically incur a page fault.  They always count towards
  //      virtual memory usage, and depending on the OS, typically
  //      do not count towards physical memory usage.  This property
  //      is not writable.
  //
  //  "tcmalloc.per_cpu_caches_active"
  //      Whether tcmalloc is using per-CPU caches (1 or 0 respectively).
  //      This property is not writable.
  // -------------------------------------------------------------------

  // Get the named "property"'s value.  Returns true if the property
  // is known.  Returns false if the property is not a valid property
  // name for the current malloc implementation.
  // REQUIRES: property != null; value != null
  virtual bool GetNumericProperty(const char* property, size_t* value);

  // Set the named "property"'s value.  Returns true if the property
  // is known and writable.  Returns false if the property is not a
  // valid property name for the current malloc implementation, or
  // is not writable.
  // REQUIRES: property != null
  virtual bool SetNumericProperty(const char* property, size_t value);

  // Mark the current thread as "idle".  This routine may optionally
  // be called by threads as a hint to the malloc implementation that
  // any thread-specific resources should be released.  Note: this may
  // be an expensive routine, so it should not be called too often.
  //
  // Also, if the code that calls this routine will go to sleep for
  // a while, it should take care to not allocate anything between
  // the call to this routine and the beginning of the sleep.
  //
  // Most malloc implementations ignore this routine.
  virtual void MarkThreadIdle();

  // Mark the current thread as "busy".  This routine should be
  // called after MarkThreadIdle() if the thread will now do more
  // work.  If this method is not called, performance may suffer.
  //
  // Most malloc implementations ignore this routine.
  virtual void MarkThreadBusy();

  // Attempt to free any resources associated with cpu <cpu> (in the sense
  // of only being usable from that CPU.)  Returns the number of bytes
  // previously assigned to "cpu" that were freed.  Safe to call from
  // any processor, not just <cpu>.
  //
  // Most malloc implementations ignore this routine (known exceptions:
  // tcmalloc with --tcmalloc_per_cpu_caches=true.)
  virtual size_t ReleaseCPUMemory(int cpu);

  // Gets the system allocator used by the malloc extension instance. Returns
  // null for malloc implementations that do not support pluggable system
  // allocators.
  virtual SysAllocator* GetSystemAllocator();

  // Sets the system allocator to the specified.
  //
  // Users could register their own system allocators for malloc implementation
  // that supports pluggable system allocators, such as TCMalloc, by doing:
  //   alloc = new MyOwnSysAllocator();
  //   MallocExtension::instance()->SetSystemAllocator(alloc);
  // It's up to users whether to fall back (recommended) to the default
  // system allocator (use GetSystemAllocator() above) or not. The caller is
  // responsible to any necessary locking.
  // See tcmalloc/system-alloc.h for the interface and
  //     tcmalloc/memfs_malloc.cc for the examples.
  //
  // It's a no-op for malloc implementations that do not support pluggable
  // system allocators.
  virtual void SetSystemAllocator(SysAllocator *a);

  // Try to release num_bytes of free memory back to the operating
  // system for reuse.  Use this extension with caution -- to get this
  // memory back may require faulting pages back in by the OS, and
  // that may be slow.  (Currently only implemented in tcmalloc.)
  virtual void ReleaseToSystem(size_t num_bytes);

  // Same as ReleaseToSystem() but release as much memory as possible.
  virtual void ReleaseFreeMemory();

  // Sets the rate at which we release unused memory to the system.
  // Zero means we never release memory back to the system.  Increase
  // this flag to return memory faster; decrease it to return memory
  // slower.  Reasonable rates are in the range [0,10].  (Currently
  // only implemented in tcmalloc).
  virtual void SetMemoryReleaseRate(double rate);

  // Gets the release rate.  Returns a value < 0 if unknown.
  virtual double GetMemoryReleaseRate();

  // Returns the estimated number of bytes that will be allocated for
  // a request of "size" bytes.  This is an estimate: an allocation of
  // SIZE bytes may reserve more bytes, but will never reserve less.
  // (Currently only implemented in tcmalloc, other implementations
  // always return SIZE.)
  // This is equivalent to malloc_good_size() in OS X.
  virtual size_t GetEstimatedAllocatedSize(size_t size);

  // Returns the actual number N of bytes reserved by tcmalloc for the
  // pointer p.  This number may be equal to or greater than the
  // number of bytes requested when p was allocated.
  //
  // This routine is just useful for statistics collection.  The
  // client must *not* read or write from the extra bytes that are
  // indicated by this call.
  //
  // Example, suppose the client gets memory by calling
  //    p = malloc(10)
  // and GetAllocatedSize(p) returns 16.  The client must only use the
  // first 10 bytes p[0..9], and not attempt to read or write p[10..15].
  //
  // p must have been allocated by this malloc implementation, must
  // not be an interior pointer -- that is, must be exactly the
  // pointer returned to by malloc() et al., not some offset from that
  // -- and should not have been freed yet.  p may be null.
  // (Currently only implemented in tcmalloc; other implementations
  // will return 0.)
  virtual size_t GetAllocatedSize(const void* p);

  // Returns kOwned if this malloc implementation allocated the memory
  // pointed to by p, or kNotOwned if some other malloc implementation
  // allocated it or p is null.  May also return kUnknownOwnership if
  // the malloc implementation does not keep track of ownership.
  // REQUIRES: p must be a value returned from a previous call to
  // malloc(), calloc(), realloc(), memalign(), posix_memalign(),
  // valloc(), pvalloc(), new, or new[], and must refer to memory that
  // is currently allocated (so, for instance, you should not pass in
  // a pointer after having called free() on it).
  enum Ownership {
    // NOTE: Enum values MUST be kept in sync with the version in
    // malloc_extension_c.h
    kUnknownOwnership = 0,
    kOwned,
    kNotOwned
  };
  virtual Ownership GetOwnership(const void* p);

  // The current malloc implementation.  Always non-null.
  static MallocExtension* instance() {
    InitModuleOnce();
    return current_instance_.load(std::memory_order_acquire);
  }

  // Change the malloc implementation.  Typically called by the
  // malloc implementation during initialization.
  static void Register(MallocExtension* implementation);

  // Type used by GetProperties.  See comment on GetProperties.
  struct Property {
    size_t value;
    // Stores breakdown of the property value bucketed by object size.
    struct Bucket {
      size_t min_object_size;
      size_t max_object_size;
      size_t size;
    };
    // Empty unless detailed info was asked for and this type has buckets
    std::vector<Bucket> buckets;
  };

  // Type used by GetProperties.  See comment on GetProperties.
  enum StatLevel { kSummary, kDetailed };

  // Stores in *result detailed statistics about the malloc
  // implementation. *result will be a map keyed by the name of
  // the statistic. Each statistic has at least a "value" field.
  //
  // Some statistics may also contain an array of buckets if
  // level==kDetailed and the "value" can be subdivided
  // into different buckets for different object sizes.  If
  // such detailed statistics are not available, Property::buckets
  // will be empty.  Otherwise Property::buckets will contain
  // potentially many entries.  For each bucket b, b.value
  // will count the value contributed by objects in the range
  // [b.min_object_size, b.max_object_size].
  //
  // Common across malloc implementations:
  //  generic.bytes_in_use_by_app  -- Bytes currently in use by application
  //  generic.physical_memory_used -- Overall (including malloc internals)
  //  generic.virtual_memory_used  -- Overall (including malloc internals)
  //
  // Tcmalloc specific properties
  //  tcmalloc.cpu_free            -- Bytes in per-cpu free-lists
  //  tcmalloc.thread_cache_free   -- Bytes in per-thread free-lists
  //  tcmalloc.transfer_cache      -- Bytes in cross-thread transfer caches
  //  tcmalloc.central_cache_free  -- Bytes in central cache
  //  tcmalloc.page_heap_free      -- Bytes in page heap
  //  tcmalloc.page_heap_unmapped  -- Bytes in page heap (no backing phys. mem)
  //  tcmalloc.metadata_bytes      -- Used by internal data structures
  //  tcmalloc.thread_cache_count  -- Number of thread caches in use
  //
  // Debug allocator
  //  debug.free_queue             -- Recently freed objects
  virtual void GetProperties(StatLevel level,
                             std::map<std::string, Property>* result);
 private:
  static MallocExtension* InitModule();

  static void InitModuleOnce() {
    // Pointer stored here so heap leak checker will consider the default
    // instance reachable, even if current_instance_ is later overridden by
    // MallocExtension::Register().
    ABSL_ATTRIBUTE_UNUSED static MallocExtension* default_instance =
        InitModule();
  }

  static std::atomic<MallocExtension*> current_instance_;
};

// Base class than can handle output generated by GetHeapSample() and
// GetHeapGrowthStacks().  Use the available subclass or roll your
// own.  Useful if you want explicit control over the type of output
// buffer used (e.g. IOBuffer, Cord, etc.)
class MallocExtensionWriter {
 public:
  virtual ~MallocExtensionWriter() {}
  virtual void Write(const char* buf, int len) = 0;
 protected:
  MallocExtensionWriter() {}
  MallocExtensionWriter(const MallocExtensionWriter&) = delete;
  MallocExtensionWriter& operator=(const MallocExtensionWriter&) = delete;
};

// A subclass that writes to the std::string "out".  NOTE: The generated
// data is *appended* to "*out".  I.e., the old contents of "*out" are
// preserved.
class StringMallocExtensionWriter : public MallocExtensionWriter {
 public:
  explicit StringMallocExtensionWriter(std::string* out) : out_(out) {}
  virtual void Write(const char* buf, int len) {
    out_->append(buf, len);
  }

 private:
  std::string* const out_;
  StringMallocExtensionWriter(const StringMallocExtensionWriter&) = delete;
  StringMallocExtensionWriter& operator=(const StringMallocExtensionWriter&) =
      delete;
};

}  // namespace base_internal
}  // namespace absl

// The nallocx function allocates no memory, but it performs the same size
// computation as the malloc function, and returns the real size of the
// allocation that would result from the equivalent malloc function call.
// Default weak implementation returns size unchanged, but tcmalloc overrides it
// and returns rounded up size. See the following link for details:
// http://www.unix.com/man-page/freebsd/3/nallocx/
extern "C" size_t nallocx(size_t size, int flags);

#ifndef MALLOCX_LG_ALIGN
#define MALLOCX_LG_ALIGN(la) (la)
#endif

#endif  // ABSL_BASE_INTERNAL_MALLOC_EXTENSION_H_
