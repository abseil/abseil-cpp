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

// A low-level allocator that can be used by other low-level
// modules without introducing dependency cycles.
// This allocator is slow and wasteful of memory;
// it should not be used when performance is key.

#include "absl/base/internal/low_level_alloc.h"

#include "absl/base/config.h"
#include "absl/base/internal/scheduling_mode.h"
#include "absl/base/macros.h"
#include "absl/base/thread_annotations.h"

// LowLevelAlloc requires that the platform support low-level
// allocation of virtual memory. Platforms lacking this cannot use
// LowLevelAlloc.
#ifndef ABSL_LOW_LEVEL_ALLOC_MISSING

#ifndef _WIN32
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <string.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <new>                   // for placement-new

#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/malloc_hook.h"
#include "absl/base/internal/malloc_hook_invoke.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock.h"

// MAP_ANONYMOUS
#if defined(__APPLE__)
// For mmap, Linux defines both MAP_ANONYMOUS and MAP_ANON and says MAP_ANON is
// deprecated. In Darwin, MAP_ANON is all there is.
#if !defined MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif  // !MAP_ANONYMOUS
#endif  // __APPLE__

namespace absl {
namespace base_internal {

// A first-fit allocator with amortized logarithmic free() time.

// ---------------------------------------------------------------------------
static const int kMaxLevel = 30;

namespace {
// This struct describes one allocated block, or one free block.
struct AllocList {
  struct Header {
    // Size of entire region, including this field. Must be
    // first. Valid in both allocated and unallocated blocks.
    uintptr_t size;

    // kMagicAllocated or kMagicUnallocated xor this.
    uintptr_t magic;

    // Pointer to parent arena.
    LowLevelAlloc::Arena *arena;

    // Aligns regions to 0 mod 2*sizeof(void*).
    void *dummy_for_alignment;
  } header;

  // Next two fields: in unallocated blocks: freelist skiplist data
  //                  in allocated blocks: overlaps with client data

  // Levels in skiplist used.
  int levels;

  // Actually has levels elements. The AllocList node may not have room
  // for all kMaxLevel entries. See max_fit in LLA_SkiplistLevels().
  AllocList *next[kMaxLevel];
};
}  // namespace

// ---------------------------------------------------------------------------
// A trivial skiplist implementation.  This is used to keep the freelist
// in address order while taking only logarithmic time per insert and delete.

// An integer approximation of log2(size/base)
// Requires size >= base.
static int IntLog2(size_t size, size_t base) {
  int result = 0;
  for (size_t i = size; i > base; i >>= 1) {  // i == floor(size/2**result)
    result++;
  }
  //    floor(size / 2**result) <= base < floor(size / 2**(result-1))
  // =>     log2(size/(base+1)) <= result < 1+log2(size/base)
  // => result ~= log2(size/base)
  return result;
}

// Return a random integer n:  p(n)=1/(2**n) if 1 <= n; p(n)=0 if n < 1.
static int Random(uint32_t *state) {
  uint32_t r = *state;
  int result = 1;
  while ((((r = r*1103515245 + 12345) >> 30) & 1) == 0) {
    result++;
  }
  *state = r;
  return result;
}

// Return a number of skiplist levels for a node of size bytes, where
// base is the minimum node size.  Compute level=log2(size / base)+n
// where n is 1 if random is false and otherwise a random number generated with
// the standard distribution for a skiplist:  See Random() above.
// Bigger nodes tend to have more skiplist levels due to the log2(size / base)
// term, so first-fit searches touch fewer nodes.  "level" is clipped so
// level<kMaxLevel and next[level-1] will fit in the node.
// 0 < LLA_SkiplistLevels(x,y,false) <= LLA_SkiplistLevels(x,y,true) < kMaxLevel
static int LLA_SkiplistLevels(size_t size, size_t base, uint32_t *random) {
  // max_fit is the maximum number of levels that will fit in a node for the
  // given size.   We can't return more than max_fit, no matter what the
  // random number generator says.
  size_t max_fit = (size - offsetof(AllocList, next)) / sizeof(AllocList *);
  int level = IntLog2(size, base) + (random != nullptr ? Random(random) : 1);
  if (static_cast<size_t>(level) > max_fit) level = static_cast<int>(max_fit);
  if (level > kMaxLevel-1) level = kMaxLevel - 1;
  ABSL_RAW_CHECK(level >= 1, "block not big enough for even one level");
  return level;
}

// Return "atleast", the first element of AllocList *head s.t. *atleast >= *e.
// For 0 <= i < head->levels, set prev[i] to "no_greater", where no_greater
// points to the last element at level i in the AllocList less than *e, or is
// head if no such element exists.
static AllocList *LLA_SkiplistSearch(AllocList *head,
                                     AllocList *e, AllocList **prev) {
  AllocList *p = head;
  for (int level = head->levels - 1; level >= 0; level--) {
    for (AllocList *n; (n = p->next[level]) != nullptr && n < e; p = n) {
    }
    prev[level] = p;
  }
  return (head->levels == 0) ? nullptr : prev[0]->next[0];
}

// Insert element *e into AllocList *head.  Set prev[] as LLA_SkiplistSearch.
// Requires that e->levels be previously set by the caller (using
// LLA_SkiplistLevels())
static void LLA_SkiplistInsert(AllocList *head, AllocList *e,
                               AllocList **prev) {
  LLA_SkiplistSearch(head, e, prev);
  for (; head->levels < e->levels; head->levels++) {  // extend prev pointers
    prev[head->levels] = head;                        // to all *e's levels
  }
  for (int i = 0; i != e->levels; i++) {  // add element to list
    e->next[i] = prev[i]->next[i];
    prev[i]->next[i] = e;
  }
}

// Remove element *e from AllocList *head.  Set prev[] as LLA_SkiplistSearch().
// Requires that e->levels be previous set by the caller (using
// LLA_SkiplistLevels())
static void LLA_SkiplistDelete(AllocList *head, AllocList *e,
                               AllocList **prev) {
  AllocList *found = LLA_SkiplistSearch(head, e, prev);
  ABSL_RAW_CHECK(e == found, "element not in freelist");
  for (int i = 0; i != e->levels && prev[i]->next[i] == e; i++) {
    prev[i]->next[i] = e->next[i];
  }
  while (head->levels > 0 && head->next[head->levels - 1] == nullptr) {
    head->levels--;   // reduce head->levels if level unused
  }
}

// ---------------------------------------------------------------------------
// Arena implementation

struct LowLevelAlloc::Arena {
  // This constructor does nothing, and relies on zero-initialization to get
  // the proper initial state.
  Arena() : mu(base_internal::kLinkerInitialized) {}  // NOLINT
  explicit Arena(int)  // NOLINT(readability/casting)
      :  // Avoid recursive cooperative scheduling w/ kernel scheduling.
        mu(base_internal::SCHEDULE_KERNEL_ONLY),
        // Set pagesize to zero explicitly for non-static init.
        pagesize(0),
        random(0) {}

  base_internal::SpinLock mu;   // protects freelist, allocation_count,
                                // pagesize, roundup, min_size
  AllocList freelist;           // head of free list; sorted by addr (under mu)
  int32_t allocation_count;     // count of allocated blocks (under mu)
  std::atomic<uint32_t> flags;  // flags passed to NewArena (ro after init)
  size_t pagesize;              // ==getpagesize()  (init under mu, then ro)
  size_t roundup;               // lowest 2^n >= max(16,sizeof (AllocList))
                                // (init under mu, then ro)
  size_t min_size;              // smallest allocation block size
                                // (init under mu, then ro)
  uint32_t random;              // PRNG state
};

// The default arena, which is used when 0 is passed instead of an Arena
// pointer.
static struct LowLevelAlloc::Arena default_arena;  // NOLINT

// Non-malloc-hooked arenas: used only to allocate metadata for arenas that
// do not want malloc hook reporting, so that for them there's no malloc hook
// reporting even during arena creation.
static struct LowLevelAlloc::Arena unhooked_arena;  // NOLINT

#ifndef ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING
static struct LowLevelAlloc::Arena unhooked_async_sig_safe_arena;  // NOLINT
#endif

// magic numbers to identify allocated and unallocated blocks
static const uintptr_t kMagicAllocated = 0x4c833e95U;
static const uintptr_t kMagicUnallocated = ~kMagicAllocated;

namespace {
class SCOPED_LOCKABLE ArenaLock {
 public:
  explicit ArenaLock(LowLevelAlloc::Arena *arena)
      EXCLUSIVE_LOCK_FUNCTION(arena->mu)
      : arena_(arena) {
#ifndef ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING
    if (arena == &unhooked_async_sig_safe_arena ||
        (arena->flags.load(std::memory_order_relaxed) &
         LowLevelAlloc::kAsyncSignalSafe) != 0) {
      sigset_t all;
      sigfillset(&all);
      mask_valid_ = pthread_sigmask(SIG_BLOCK, &all, &mask_) == 0;
    }
#endif
    arena_->mu.Lock();
  }
  ~ArenaLock() { ABSL_RAW_CHECK(left_, "haven't left Arena region"); }
  void Leave() UNLOCK_FUNCTION() {
    arena_->mu.Unlock();
#ifndef ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING
    if (mask_valid_) {
      pthread_sigmask(SIG_SETMASK, &mask_, nullptr);
    }
#endif
    left_ = true;
  }

 private:
  bool left_ = false;  // whether left region
#ifndef ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING
  bool mask_valid_ = false;
  sigset_t mask_;  // old mask of blocked signals
#endif
  LowLevelAlloc::Arena *arena_;
  ArenaLock(const ArenaLock &) = delete;
  ArenaLock &operator=(const ArenaLock &) = delete;
};
}  // namespace

// create an appropriate magic number for an object at "ptr"
// "magic" should be kMagicAllocated or kMagicUnallocated
inline static uintptr_t Magic(uintptr_t magic, AllocList::Header *ptr) {
  return magic ^ reinterpret_cast<uintptr_t>(ptr);
}

// Initialize the fields of an Arena
static void ArenaInit(LowLevelAlloc::Arena *arena) {
  if (arena->pagesize == 0) {
#ifdef _WIN32
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    arena->pagesize = std::max(system_info.dwPageSize,
                               system_info.dwAllocationGranularity);
#else
    arena->pagesize = getpagesize();
#endif
    // Round up block sizes to a power of two close to the header size.
    arena->roundup = 16;
    while (arena->roundup < sizeof (arena->freelist.header)) {
      arena->roundup += arena->roundup;
    }
    // Don't allocate blocks less than twice the roundup size to avoid tiny
    // free blocks.
    arena->min_size = 2 * arena->roundup;
    arena->freelist.header.size = 0;
    arena->freelist.header.magic =
        Magic(kMagicUnallocated, &arena->freelist.header);
    arena->freelist.header.arena = arena;
    arena->freelist.levels = 0;
    memset(arena->freelist.next, 0, sizeof (arena->freelist.next));
    arena->allocation_count = 0;
    if (arena == &default_arena) {
      // Default arena should be hooked, e.g. for heap-checker to trace
      // pointer chains through objects in the default arena.
      arena->flags.store(LowLevelAlloc::kCallMallocHook,
                         std::memory_order_relaxed);
    }
#ifndef ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING
    else if (arena ==  // NOLINT(readability/braces)
             &unhooked_async_sig_safe_arena) {
      arena->flags.store(LowLevelAlloc::kAsyncSignalSafe,
                         std::memory_order_relaxed);
    }
#endif
    else {  // NOLINT(readability/braces)
      // other arenas' flags may be overridden by client,
      // but unhooked_arena will have 0 in 'flags'.
      arena->flags.store(0, std::memory_order_relaxed);
    }
  }
}

// L < meta_data_arena->mu
LowLevelAlloc::Arena *LowLevelAlloc::NewArena(int32_t flags,
                                              Arena *meta_data_arena) {
  ABSL_RAW_CHECK(meta_data_arena != nullptr, "must pass a valid arena");
  if (meta_data_arena == &default_arena) {
#ifndef ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING
    if ((flags & LowLevelAlloc::kAsyncSignalSafe) != 0) {
      meta_data_arena = &unhooked_async_sig_safe_arena;
    } else  // NOLINT(readability/braces)
#endif
        if ((flags & LowLevelAlloc::kCallMallocHook) == 0) {
      meta_data_arena = &unhooked_arena;
    }
  }
  // Arena(0) uses the constructor for non-static contexts
  Arena *result =
    new (AllocWithArena(sizeof (*result), meta_data_arena)) Arena(0);
  ArenaInit(result);
  result->flags.store(flags, std::memory_order_relaxed);
  return result;
}

// L < arena->mu, L < arena->arena->mu
bool LowLevelAlloc::DeleteArena(Arena *arena) {
  ABSL_RAW_CHECK(
      arena != nullptr && arena != &default_arena && arena != &unhooked_arena,
      "may not delete default arena");
  ArenaLock section(arena);
  bool empty = (arena->allocation_count == 0);
  section.Leave();
  if (empty) {
    while (arena->freelist.next[0] != nullptr) {
      AllocList *region = arena->freelist.next[0];
      size_t size = region->header.size;
      arena->freelist.next[0] = region->next[0];
      ABSL_RAW_CHECK(
          region->header.magic == Magic(kMagicUnallocated, &region->header),
          "bad magic number in DeleteArena()");
      ABSL_RAW_CHECK(region->header.arena == arena,
                     "bad arena pointer in DeleteArena()");
      ABSL_RAW_CHECK(size % arena->pagesize == 0,
                     "empty arena has non-page-aligned block size");
      ABSL_RAW_CHECK(reinterpret_cast<uintptr_t>(region) % arena->pagesize == 0,
                     "empty arena has non-page-aligned block");
      int munmap_result;
#ifdef _WIN32
      munmap_result = VirtualFree(region, 0, MEM_RELEASE);
      ABSL_RAW_CHECK(munmap_result != 0,
                     "LowLevelAlloc::DeleteArena: VitualFree failed");
#else
      if ((arena->flags.load(std::memory_order_relaxed) &
           LowLevelAlloc::kAsyncSignalSafe) == 0) {
        munmap_result = munmap(region, size);
      } else {
        munmap_result = MallocHook::UnhookedMUnmap(region, size);
      }
      if (munmap_result != 0) {
        ABSL_RAW_LOG(FATAL, "LowLevelAlloc::DeleteArena: munmap failed: %d",
                     errno);
      }
#endif
    }
    Free(arena);
  }
  return empty;
}

// ---------------------------------------------------------------------------

// Addition, checking for overflow.  The intent is to die if an external client
// manages to push through a request that would cause arithmetic to fail.
static inline uintptr_t CheckedAdd(uintptr_t a, uintptr_t b) {
  uintptr_t sum = a + b;
  ABSL_RAW_CHECK(sum >= a, "LowLevelAlloc arithmetic overflow");
  return sum;
}

// Return value rounded up to next multiple of align.
// align must be a power of two.
static inline uintptr_t RoundUp(uintptr_t addr, uintptr_t align) {
  return CheckedAdd(addr, align - 1) & ~(align - 1);
}

// Equivalent to "return prev->next[i]" but with sanity checking
// that the freelist is in the correct order, that it
// consists of regions marked "unallocated", and that no two regions
// are adjacent in memory (they should have been coalesced).
// L < arena->mu
static AllocList *Next(int i, AllocList *prev, LowLevelAlloc::Arena *arena) {
  ABSL_RAW_CHECK(i < prev->levels, "too few levels in Next()");
  AllocList *next = prev->next[i];
  if (next != nullptr) {
    ABSL_RAW_CHECK(
        next->header.magic == Magic(kMagicUnallocated, &next->header),
        "bad magic number in Next()");
    ABSL_RAW_CHECK(next->header.arena == arena, "bad arena pointer in Next()");
    if (prev != &arena->freelist) {
      ABSL_RAW_CHECK(prev < next, "unordered freelist");
      ABSL_RAW_CHECK(reinterpret_cast<char *>(prev) + prev->header.size <
                         reinterpret_cast<char *>(next),
                     "malformed freelist");
    }
  }
  return next;
}

// Coalesce list item "a" with its successor if they are adjacent.
static void Coalesce(AllocList *a) {
  AllocList *n = a->next[0];
  if (n != nullptr && reinterpret_cast<char *>(a) + a->header.size ==
                          reinterpret_cast<char *>(n)) {
    LowLevelAlloc::Arena *arena = a->header.arena;
    a->header.size += n->header.size;
    n->header.magic = 0;
    n->header.arena = nullptr;
    AllocList *prev[kMaxLevel];
    LLA_SkiplistDelete(&arena->freelist, n, prev);
    LLA_SkiplistDelete(&arena->freelist, a, prev);
    a->levels = LLA_SkiplistLevels(a->header.size, arena->min_size,
                                   &arena->random);
    LLA_SkiplistInsert(&arena->freelist, a, prev);
  }
}

// Adds block at location "v" to the free list
// L >= arena->mu
static void AddToFreelist(void *v, LowLevelAlloc::Arena *arena) {
  AllocList *f = reinterpret_cast<AllocList *>(
                        reinterpret_cast<char *>(v) - sizeof (f->header));
  ABSL_RAW_CHECK(f->header.magic == Magic(kMagicAllocated, &f->header),
                 "bad magic number in AddToFreelist()");
  ABSL_RAW_CHECK(f->header.arena == arena,
                 "bad arena pointer in AddToFreelist()");
  f->levels = LLA_SkiplistLevels(f->header.size, arena->min_size,
                                 &arena->random);
  AllocList *prev[kMaxLevel];
  LLA_SkiplistInsert(&arena->freelist, f, prev);
  f->header.magic = Magic(kMagicUnallocated, &f->header);
  Coalesce(f);                  // maybe coalesce with successor
  Coalesce(prev[0]);            // maybe coalesce with predecessor
}

// Frees storage allocated by LowLevelAlloc::Alloc().
// L < arena->mu
void LowLevelAlloc::Free(void *v) {
  if (v != nullptr) {
    AllocList *f = reinterpret_cast<AllocList *>(
                        reinterpret_cast<char *>(v) - sizeof (f->header));
    ABSL_RAW_CHECK(f->header.magic == Magic(kMagicAllocated, &f->header),
                   "bad magic number in Free()");
    LowLevelAlloc::Arena *arena = f->header.arena;
    if ((arena->flags.load(std::memory_order_relaxed) & kCallMallocHook) != 0) {
      MallocHook::InvokeDeleteHook(v);
    }
    ArenaLock section(arena);
    AddToFreelist(v, arena);
    ABSL_RAW_CHECK(arena->allocation_count > 0, "nothing in arena to free");
    arena->allocation_count--;
    section.Leave();
  }
}

// allocates and returns a block of size bytes, to be freed with Free()
// L < arena->mu
static void *DoAllocWithArena(size_t request, LowLevelAlloc::Arena *arena) {
  void *result = nullptr;
  if (request != 0) {
    AllocList *s;       // will point to region that satisfies request
    ArenaLock section(arena);
    ArenaInit(arena);
    // round up with header
    size_t req_rnd = RoundUp(CheckedAdd(request, sizeof (s->header)),
                             arena->roundup);
    for (;;) {      // loop until we find a suitable region
      // find the minimum levels that a block of this size must have
      int i = LLA_SkiplistLevels(req_rnd, arena->min_size, nullptr) - 1;
      if (i < arena->freelist.levels) {   // potential blocks exist
        AllocList *before = &arena->freelist;  // predecessor of s
        while ((s = Next(i, before, arena)) != nullptr &&
               s->header.size < req_rnd) {
          before = s;
        }
        if (s != nullptr) {       // we found a region
          break;
        }
      }
      // we unlock before mmap() both because mmap() may call a callback hook,
      // and because it may be slow.
      arena->mu.Unlock();
      // mmap generous 64K chunks to decrease
      // the chances/impact of fragmentation:
      size_t new_pages_size = RoundUp(req_rnd, arena->pagesize * 16);
      void *new_pages;
#ifdef _WIN32
      new_pages = VirtualAlloc(0, new_pages_size,
                               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      ABSL_RAW_CHECK(new_pages != nullptr, "VirtualAlloc failed");
#else
      if ((arena->flags.load(std::memory_order_relaxed) &
           LowLevelAlloc::kAsyncSignalSafe) != 0) {
        new_pages = MallocHook::UnhookedMMap(nullptr, new_pages_size,
            PROT_WRITE|PROT_READ, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
      } else {
        new_pages = mmap(nullptr, new_pages_size, PROT_WRITE | PROT_READ,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
      }
      if (new_pages == MAP_FAILED) {
        ABSL_RAW_LOG(FATAL, "mmap error: %d", errno);
      }
#endif
      arena->mu.Lock();
      s = reinterpret_cast<AllocList *>(new_pages);
      s->header.size = new_pages_size;
      // Pretend the block is allocated; call AddToFreelist() to free it.
      s->header.magic = Magic(kMagicAllocated, &s->header);
      s->header.arena = arena;
      AddToFreelist(&s->levels, arena);  // insert new region into free list
    }
    AllocList *prev[kMaxLevel];
    LLA_SkiplistDelete(&arena->freelist, s, prev);    // remove from free list
    // s points to the first free region that's big enough
    if (CheckedAdd(req_rnd, arena->min_size) <= s->header.size) {
      // big enough to split
      AllocList *n = reinterpret_cast<AllocList *>
                        (req_rnd + reinterpret_cast<char *>(s));
      n->header.size = s->header.size - req_rnd;
      n->header.magic = Magic(kMagicAllocated, &n->header);
      n->header.arena = arena;
      s->header.size = req_rnd;
      AddToFreelist(&n->levels, arena);
    }
    s->header.magic = Magic(kMagicAllocated, &s->header);
    ABSL_RAW_CHECK(s->header.arena == arena, "");
    arena->allocation_count++;
    section.Leave();
    result = &s->levels;
  }
  ANNOTATE_MEMORY_IS_UNINITIALIZED(result, request);
  return result;
}

void *LowLevelAlloc::Alloc(size_t request) {
  void *result = DoAllocWithArena(request, &default_arena);
  if ((default_arena.flags.load(std::memory_order_relaxed) &
       kCallMallocHook) != 0) {
    // this call must be directly in the user-called allocator function
    // for MallocHook::GetCallerStackTrace to work properly
    MallocHook::InvokeNewHook(result, request);
  }
  return result;
}

void *LowLevelAlloc::AllocWithArena(size_t request, Arena *arena) {
  ABSL_RAW_CHECK(arena != nullptr, "must pass a valid arena");
  void *result = DoAllocWithArena(request, arena);
  if ((arena->flags.load(std::memory_order_relaxed) & kCallMallocHook) != 0) {
    // this call must be directly in the user-called allocator function
    // for MallocHook::GetCallerStackTrace to work properly
    MallocHook::InvokeNewHook(result, request);
  }
  return result;
}

LowLevelAlloc::Arena *LowLevelAlloc::DefaultArena() {
  return &default_arena;
}

}  // namespace base_internal
}  // namespace absl

#endif  // ABSL_LOW_LEVEL_ALLOC_MISSING
