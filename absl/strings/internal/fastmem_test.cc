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

#include "absl/strings/internal/fastmem.h"

#include <memory>
#include <random>
#include <string>

#include "base/init_google.h"
#include "base/logging.h"
#include "testing/base/public/benchmark.h"
#include "gtest/gtest.h"

namespace {

using RandomEngine = std::minstd_rand0;

void VerifyResults(const int r1, const int r2, const std::string& a,
                   const std::string& b) {
  CHECK_EQ(a.size(), b.size());
  if (r1 == 0) {
    EXPECT_EQ(r2, 0) << a << " " << b;
  } else if (r1 > 0) {
    EXPECT_GT(r2, 0) << a << " " << b;
  } else {
    EXPECT_LT(r2, 0) << a << " " << b;
  }
  if ((r1 == 0) == (r2 == 0)) {
    EXPECT_EQ(r1 == 0,
              absl::strings_internal::memeq(a.data(), b.data(), a.size()))
        << r1 << " " << a << " " << b;
  }
}

// Check correctness against glibc's memcmp implementation
void CheckSingle(const std::string& a, const std::string& b) {
  CHECK_EQ(a.size(), b.size());
  const int r1 = memcmp(a.data(), b.data(), a.size());
  const int r2 =
      absl::strings_internal::fastmemcmp_inlined(a.data(), b.data(), a.size());
  VerifyResults(r1, r2, a, b);
}

void GenerateString(size_t len, std::string* s) {
  s->clear();
  for (int i = 0; i < len; i++) {
    *s += ('a' + (i % 26));
  }
}

void CheckCompare(const std::string& a, const std::string& b) {
  CheckSingle(a, b);
  for (int common = 0; common <= 32; common++) {
    std::string extra;
    GenerateString(common, &extra);
    CheckSingle(extra + a, extra + b);
    CheckSingle(a + extra, b + extra);
    for (char c1 = 'a'; c1 <= 'c'; c1++) {
      for (char c2 = 'a'; c2 <= 'c'; c2++) {
        CheckSingle(extra + c1 + a, extra + c2 + b);
      }
    }
  }
}

TEST(FastCompare, Misc) {
  CheckCompare("", "");

  CheckCompare("a", "a");
  CheckCompare("ab", "ab");
  CheckCompare("abc", "abc");
  CheckCompare("abcd", "abcd");
  CheckCompare("abcde", "abcde");

  CheckCompare("a", "x");
  CheckCompare("ab", "xb");
  CheckCompare("abc", "xbc");
  CheckCompare("abcd", "xbcd");
  CheckCompare("abcde", "xbcde");

  CheckCompare("x", "a");
  CheckCompare("xb", "ab");
  CheckCompare("xbc", "abc");
  CheckCompare("xbcd", "abcd");
  CheckCompare("xbcde", "abcde");

  CheckCompare("a", "x");
  CheckCompare("ab", "ax");
  CheckCompare("abc", "abx");
  CheckCompare("abcd", "abcx");
  CheckCompare("abcde", "abcdx");

  CheckCompare("x", "a");
  CheckCompare("ax", "ab");
  CheckCompare("abx", "abc");
  CheckCompare("abcx", "abcd");
  CheckCompare("abcdx", "abcde");

  for (int len = 0; len < 1000; len++) {
    std::string p(len, 'z');
    CheckCompare(p + "x", p + "a");
    CheckCompare(p + "ax", p + "ab");
    CheckCompare(p + "abx", p + "abc");
    CheckCompare(p + "abcx", p + "abcd");
    CheckCompare(p + "abcdx", p + "abcde");
  }
}

TEST(FastCompare, TrailingByte) {
  for (int i = 0; i < 256; i++) {
    for (int j = 0; j < 256; j++) {
      std::string a(1, i);
      std::string b(1, j);
      CheckSingle(a, b);
    }
  }
}

// Check correctness of memcpy_inlined.
void CheckSingleMemcpyInlined(const std::string& a) {
  std::unique_ptr<char[]> destination(new char[a.size() + 2]);
  destination[0] = 'x';
  destination[a.size() + 1] = 'x';
  absl::strings_internal::memcpy_inlined(destination.get() + 1, a.data(),
                                         a.size());
  CHECK_EQ('x', destination[0]);
  CHECK_EQ('x', destination[a.size() + 1]);
  CHECK_EQ(0, memcmp(a.data(), destination.get() + 1, a.size()));
}

TEST(MemCpyInlined, Misc) {
  CheckSingleMemcpyInlined("");
  CheckSingleMemcpyInlined("0");
  CheckSingleMemcpyInlined("012");
  CheckSingleMemcpyInlined("0123");
  CheckSingleMemcpyInlined("01234");
  CheckSingleMemcpyInlined("012345");
  CheckSingleMemcpyInlined("0123456");
  CheckSingleMemcpyInlined("01234567");
  CheckSingleMemcpyInlined("012345678");
  CheckSingleMemcpyInlined("0123456789");
  CheckSingleMemcpyInlined("0123456789a");
  CheckSingleMemcpyInlined("0123456789ab");
  CheckSingleMemcpyInlined("0123456789abc");
  CheckSingleMemcpyInlined("0123456789abcd");
  CheckSingleMemcpyInlined("0123456789abcde");
  CheckSingleMemcpyInlined("0123456789abcdef");
  CheckSingleMemcpyInlined("0123456789abcdefg");
}

template <typename Function>
inline void CopyLoop(benchmark::State& state, int size, Function func) {
  char* src = new char[size];
  char* dst = new char[size];
  memset(src, 'x', size);
  memset(dst, 'y', size);
  for (auto _ : state) {
    func(dst, src, size);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * size);
  CHECK_EQ(dst[0], 'x');
  delete[] src;
  delete[] dst;
}

void BM_memcpy(benchmark::State& state) {
  CopyLoop(state, state.range(0), memcpy);
}
BENCHMARK(BM_memcpy)->DenseRange(1, 18)->Range(32, 8 << 20);

void BM_memcpy_inlined(benchmark::State& state) {
  CopyLoop(state, state.range(0), absl::strings_internal::memcpy_inlined);
}
BENCHMARK(BM_memcpy_inlined)->DenseRange(1, 18)->Range(32, 8 << 20);

// unaligned memcpy
void BM_unaligned_memcpy(benchmark::State& state) {
  const int n = state.range(0);
  const int kMaxOffset = 32;
  char* src = new char[n + kMaxOffset];
  char* dst = new char[n + kMaxOffset];
  memset(src, 'x', n + kMaxOffset);
  int r = 0, i = 0;
  for (auto _ : state) {
    memcpy(dst + (i % kMaxOffset), src + ((i + 5) % kMaxOffset), n);
    r += dst[0];
    ++i;
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
  delete[] src;
  delete[] dst;
  benchmark::DoNotOptimize(r);
}
BENCHMARK(BM_unaligned_memcpy)->DenseRange(1, 18)->Range(32, 8 << 20);

// memmove worst case: heavy overlap, but not always by the same amount.
// Also, the source and destination will often be unaligned.
void BM_memmove_worst_case(benchmark::State& state) {
  const int n = state.range(0);
  const int32_t kDeterministicSeed = 301;
  const int kMaxOffset = 32;
  char* src = new char[n + kMaxOffset];
  memset(src, 'x', n + kMaxOffset);
  size_t offsets[64];
  RandomEngine rng(kDeterministicSeed);
  std::uniform_int_distribution<size_t> random_to_max_offset(0, kMaxOffset);
  for (size_t& offset : offsets) {
    offset = random_to_max_offset(rng);
  }
  int r = 0, i = 0;
  for (auto _ : state) {
    memmove(src + offsets[i], src + offsets[i + 1], n);
    r += src[0];
    i = (i + 2) % arraysize(offsets);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
  delete[] src;
  benchmark::DoNotOptimize(r);
}
BENCHMARK(BM_memmove_worst_case)->DenseRange(1, 18)->Range(32, 8 << 20);

// memmove cache-friendly: aligned and overlapping with 4k
// between the source and destination addresses.
void BM_memmove_cache_friendly(benchmark::State& state) {
  const int n = state.range(0);
  char* src = new char[n + 4096];
  memset(src, 'x', n);
  int r = 0;
  while (state.KeepRunningBatch(2)) {  // count each memmove as an iteration
    memmove(src + 4096, src, n);
    memmove(src, src + 4096, n);
    r += src[0];
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
  delete[] src;
  benchmark::DoNotOptimize(r);
}
BENCHMARK(BM_memmove_cache_friendly)
    ->Arg(5 * 1024)
    ->Arg(10 * 1024)
    ->Range(16 << 10, 8 << 20);

// memmove best(?) case: aligned and non-overlapping.
void BM_memmove_aligned_non_overlapping(benchmark::State& state) {
  CopyLoop(state, state.range(0), memmove);
}
BENCHMARK(BM_memmove_aligned_non_overlapping)
    ->DenseRange(1, 18)
    ->Range(32, 8 << 20);

// memset speed
void BM_memset(benchmark::State& state) {
  const int n = state.range(0);
  char* dst = new char[n];
  int r = 0;
  for (auto _ : state) {
    memset(dst, 'x', n);
    r += dst[0];
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
  delete[] dst;
  benchmark::DoNotOptimize(r);
}
BENCHMARK(BM_memset)->Range(8, 4096 << 10);

// Bandwidth (vectorization?) test: the ideal generated code will be limited
// by memory bandwidth.  Even so-so generated code will max out memory bandwidth
// on some machines.
void BM_membandwidth(benchmark::State& state) {
  const int n = state.range(0);
  CHECK_EQ(n % 32, 0);  // We will read 32 bytes per iter.
  char* dst = new char[n];
  int r = 0;
  for (auto _ : state) {
    const uint32_t* p = reinterpret_cast<uint32_t*>(dst);
    const uint32_t* limit = reinterpret_cast<uint32_t*>(dst + n);
    uint32_t x = 0;
    while (p < limit) {
      x += p[0];
      x += p[1];
      x += p[2];
      x += p[3];
      x += p[4];
      x += p[5];
      x += p[6];
      x += p[7];
      p += 8;
    }
    r += x;
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
  delete[] dst;
  benchmark::DoNotOptimize(r);
}
BENCHMARK(BM_membandwidth)->Range(32, 16384 << 10);

// Helper for benchmarks.  Repeatedly compares two strings that are
// either equal or different only in one character.  If test_equal_strings
// is false then position_to_modify determines where the difference will be.
template <typename Function>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline void StringCompareLoop(
    benchmark::State& state, bool test_equal_strings,
    std::string::size_type position_to_modify, int size, Function func) {
  const int kIterMult = 4;  // Iteration multiplier for better timing resolution
  CHECK_GT(size, 0);
  const bool position_to_modify_is_valid =
      position_to_modify != std::string::npos && position_to_modify < size;
  CHECK_NE(position_to_modify_is_valid, test_equal_strings);
  if (!position_to_modify_is_valid) {
    position_to_modify = 0;
  }
  std::string sa(size, 'a');
  std::string sb = sa;
  char last = sa[size - 1];
  int num = 0;
  for (auto _ : state) {
    for (int i = 0; i < kIterMult; ++i) {
      sb[position_to_modify] = test_equal_strings ? last : last ^ 1;
      num += func(sa, sb);
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * size);
  benchmark::DoNotOptimize(num);
}

// Helper for benchmarks.  Repeatedly compares two memory regions that are
// either equal or different only in their final character.
template <typename Function>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline void CompareLoop(benchmark::State& state,
                                                     bool test_equal_strings,
                                                     int size, Function func) {
  const int kIterMult = 4;  // Iteration multiplier for better timing resolution
  CHECK_GT(size, 0);
  char* data = static_cast<char*>(malloc(size * 2));
  memset(data, 'a', size * 2);
  char* a = data;
  char* b = data + size;
  char last = a[size - 1];
  int num = 0;
  for (auto _ : state) {
    for (int i = 0; i < kIterMult; ++i) {
      b[size - 1] = test_equal_strings ? last : last ^ 1;
      num += func(a, b, size);
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * size);
  benchmark::DoNotOptimize(num);
  free(data);
}

void BM_memcmp(benchmark::State& state) {
  CompareLoop(state, false, state.range(0), memcmp);
}
BENCHMARK(BM_memcmp)->DenseRange(1, 9)->Range(32, 8 << 20);

void BM_fastmemcmp_inlined(benchmark::State& state) {
  CompareLoop(state, false, state.range(0),
              absl::strings_internal::fastmemcmp_inlined);
}
BENCHMARK(BM_fastmemcmp_inlined)->DenseRange(1, 9)->Range(32, 8 << 20);

void BM_memeq(benchmark::State& state) {
  CompareLoop(state, false, state.range(0), absl::strings_internal::memeq);
}
BENCHMARK(BM_memeq)->DenseRange(1, 9)->Range(32, 8 << 20);

void BM_memeq_equal(benchmark::State& state) {
  CompareLoop(state, true, state.range(0), absl::strings_internal::memeq);
}
BENCHMARK(BM_memeq_equal)->DenseRange(1, 9)->Range(32, 8 << 20);

bool StringLess(const std::string& x, const std::string& y) { return x < y; }
bool StringEqual(const std::string& x, const std::string& y) { return x == y; }
bool StdEqual(const std::string& x, const std::string& y) {
  return x.size() == y.size() &&
         std::equal(x.data(), x.data() + x.size(), y.data());
}

// Benchmark for x < y, where x and y are strings that differ in only their
// final char.  That should be more-or-less the worst case for <.
void BM_string_less(benchmark::State& state) {
  StringCompareLoop(state, false, state.range(0) - 1, state.range(0),
                    StringLess);
}
BENCHMARK(BM_string_less)->DenseRange(1, 9)->Range(32, 1 << 20);

// Benchmark for x < y, where x and y are strings that differ in only their
// first char.  That should be more-or-less the best case for <.
void BM_string_less_easy(benchmark::State& state) {
  StringCompareLoop(state, false, 0, state.range(0), StringLess);
}
BENCHMARK(BM_string_less_easy)->DenseRange(1, 9)->Range(32, 1 << 20);

void BM_string_equal(benchmark::State& state) {
  StringCompareLoop(state, false, state.range(0) - 1, state.range(0),
                    StringEqual);
}
BENCHMARK(BM_string_equal)->DenseRange(1, 9)->Range(32, 1 << 20);

void BM_string_equal_equal(benchmark::State& state) {
  StringCompareLoop(state, true, std::string::npos, state.range(0), StringEqual);
}
BENCHMARK(BM_string_equal_equal)->DenseRange(1, 9)->Range(32, 1 << 20);

void BM_std_equal(benchmark::State& state) {
  StringCompareLoop(state, false, state.range(0) - 1, state.range(0), StdEqual);
}
BENCHMARK(BM_std_equal)->DenseRange(1, 9)->Range(32, 1 << 20);

void BM_std_equal_equal(benchmark::State& state) {
  StringCompareLoop(state, true, std::string::npos, state.range(0), StdEqual);
}
BENCHMARK(BM_std_equal_equal)->DenseRange(1, 9)->Range(32, 1 << 20);

void BM_string_equal_unequal_lengths(benchmark::State& state) {
  const int size = state.range(0);
  std::string a(size, 'a');
  std::string b(size + 1, 'a');
  int count = 0;
  for (auto _ : state) {
    b[size - 1] = 'a';
    count += (a == b);
  }
  benchmark::DoNotOptimize(count);
}
BENCHMARK(BM_string_equal_unequal_lengths)->Arg(1)->Arg(1 << 20);

void BM_stdstring_equal_unequal_lengths(benchmark::State& state) {
  const int size = state.range(0);
  std::string a(size, 'a');
  std::string b(size + 1, 'a');
  int count = 0;
  for (auto _ : state) {
    b[size - 1] = 'a';
    count += (a == b);
  }
  benchmark::DoNotOptimize(count);
}
BENCHMARK(BM_stdstring_equal_unequal_lengths)->Arg(1)->Arg(1 << 20);

}  // namespace
