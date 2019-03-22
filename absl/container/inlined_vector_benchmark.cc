// Copyright 2017 The Abseil Authors.
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

#include "absl/container/inlined_vector.h"

#include <string>
#include <vector>

#include "benchmark/benchmark.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/strings/str_cat.h"

namespace {

void BM_InlinedVectorFill(benchmark::State& state) {
  absl::InlinedVector<int, 8> v;
  int val = 10;
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    v.push_back(val);
  }
}
BENCHMARK(BM_InlinedVectorFill)->Range(0, 1024);

void BM_InlinedVectorFillRange(benchmark::State& state) {
  const int len = state.range(0);
  std::unique_ptr<int[]> ia(new int[len]);
  for (int i = 0; i < len; i++) {
    ia[i] = i;
  }
  auto* from = ia.get();
  auto* to = from + len;
  for (auto _ : state) {
    benchmark::DoNotOptimize(from);
    benchmark::DoNotOptimize(to);
    absl::InlinedVector<int, 8> v(from, to);
    benchmark::DoNotOptimize(v);
  }
}
BENCHMARK(BM_InlinedVectorFillRange)->Range(0, 1024);

void BM_StdVectorFill(benchmark::State& state) {
  std::vector<int> v;
  int val = 10;
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(val);
    v.push_back(val);
  }
}
BENCHMARK(BM_StdVectorFill)->Range(0, 1024);

// The purpose of the next two benchmarks is to verify that
// absl::InlinedVector is efficient when moving is more efficent than
// copying. To do so, we use strings that are larger than the short
// string optimization.
bool StringRepresentedInline(std::string s) {
  const char* chars = s.data();
  std::string s1 = std::move(s);
  return s1.data() != chars;
}

int GetNonShortStringOptimizationSize() {
  for (int i = 24; i <= 192; i *= 2) {
    if (!StringRepresentedInline(std::string(i, 'A'))) {
      return i;
    }
  }
  ABSL_RAW_LOG(
      FATAL,
      "Failed to find a std::string larger than the short std::string optimization");
  return -1;
}

void BM_InlinedVectorFillString(benchmark::State& state) {
  const int len = state.range(0);
  const int no_sso = GetNonShortStringOptimizationSize();
  std::string strings[4] = {std::string(no_sso, 'A'), std::string(no_sso, 'B'),
                            std::string(no_sso, 'C'), std::string(no_sso, 'D')};

  for (auto _ : state) {
    absl::InlinedVector<std::string, 8> v;
    for (int i = 0; i < len; i++) {
      v.push_back(strings[i & 3]);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * len);
}
BENCHMARK(BM_InlinedVectorFillString)->Range(0, 1024);

void BM_StdVectorFillString(benchmark::State& state) {
  const int len = state.range(0);
  const int no_sso = GetNonShortStringOptimizationSize();
  std::string strings[4] = {std::string(no_sso, 'A'), std::string(no_sso, 'B'),
                            std::string(no_sso, 'C'), std::string(no_sso, 'D')};

  for (auto _ : state) {
    std::vector<std::string> v;
    for (int i = 0; i < len; i++) {
      v.push_back(strings[i & 3]);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * len);
}
BENCHMARK(BM_StdVectorFillString)->Range(0, 1024);

struct Buffer {  // some arbitrary structure for benchmarking.
  char* base;
  int length;
  int capacity;
  void* user_data;
};

void BM_InlinedVectorAssignments(benchmark::State& state) {
  const int len = state.range(0);
  using BufferVec = absl::InlinedVector<Buffer, 2>;

  BufferVec src;
  src.resize(len);

  BufferVec dst;
  for (auto _ : state) {
    benchmark::DoNotOptimize(dst);
    benchmark::DoNotOptimize(src);
    dst = src;
  }
}
BENCHMARK(BM_InlinedVectorAssignments)
    ->Arg(0)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(20);

void BM_CreateFromContainer(benchmark::State& state) {
  for (auto _ : state) {
    absl::InlinedVector<int, 4> src{1, 2, 3};
    benchmark::DoNotOptimize(src);
    absl::InlinedVector<int, 4> dst(std::move(src));
    benchmark::DoNotOptimize(dst);
  }
}
BENCHMARK(BM_CreateFromContainer);

struct LargeCopyableOnly {
  LargeCopyableOnly() : d(1024, 17) {}
  LargeCopyableOnly(const LargeCopyableOnly& o) = default;
  LargeCopyableOnly& operator=(const LargeCopyableOnly& o) = default;

  std::vector<int> d;
};

struct LargeCopyableSwappable {
  LargeCopyableSwappable() : d(1024, 17) {}

  LargeCopyableSwappable(const LargeCopyableSwappable& o) = default;

  LargeCopyableSwappable& operator=(LargeCopyableSwappable o) {
    using std::swap;
    swap(*this, o);
    return *this;
  }

  friend void swap(LargeCopyableSwappable& a, LargeCopyableSwappable& b) {
    using std::swap;
    swap(a.d, b.d);
  }

  std::vector<int> d;
};

struct LargeCopyableMovable {
  LargeCopyableMovable() : d(1024, 17) {}
  // Use implicitly defined copy and move.

  std::vector<int> d;
};

struct LargeCopyableMovableSwappable {
  LargeCopyableMovableSwappable() : d(1024, 17) {}
  LargeCopyableMovableSwappable(const LargeCopyableMovableSwappable& o) =
      default;
  LargeCopyableMovableSwappable(LargeCopyableMovableSwappable&& o) = default;

  LargeCopyableMovableSwappable& operator=(LargeCopyableMovableSwappable o) {
    using std::swap;
    swap(*this, o);
    return *this;
  }
  LargeCopyableMovableSwappable& operator=(LargeCopyableMovableSwappable&& o) =
      default;

  friend void swap(LargeCopyableMovableSwappable& a,
                   LargeCopyableMovableSwappable& b) {
    using std::swap;
    swap(a.d, b.d);
  }

  std::vector<int> d;
};

template <typename ElementType>
void BM_SwapElements(benchmark::State& state) {
  const int len = state.range(0);
  using Vec = absl::InlinedVector<ElementType, 32>;
  Vec a(len);
  Vec b;
  for (auto _ : state) {
    using std::swap;
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    swap(a, b);
  }
}
BENCHMARK_TEMPLATE(BM_SwapElements, LargeCopyableOnly)->Range(0, 1024);
BENCHMARK_TEMPLATE(BM_SwapElements, LargeCopyableSwappable)->Range(0, 1024);
BENCHMARK_TEMPLATE(BM_SwapElements, LargeCopyableMovable)->Range(0, 1024);
BENCHMARK_TEMPLATE(BM_SwapElements, LargeCopyableMovableSwappable)
    ->Range(0, 1024);

// The following benchmark is meant to track the efficiency of the vector size
// as a function of stored type via the benchmark label. It is not meant to
// output useful sizeof operator performance. The loop is a dummy operation
// to fulfill the requirement of running the benchmark.
template <typename VecType>
void BM_Sizeof(benchmark::State& state) {
  int size = 0;
  for (auto _ : state) {
    VecType vec;
    size = sizeof(vec);
  }
  state.SetLabel(absl::StrCat("sz=", size));
}
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<char, 1>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<char, 4>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<char, 7>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<char, 8>);

BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<int, 1>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<int, 4>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<int, 7>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<int, 8>);

BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<void*, 1>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<void*, 4>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<void*, 7>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<void*, 8>);

BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<std::string, 1>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<std::string, 4>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<std::string, 7>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<std::string, 8>);

void BM_InlinedVectorIndexInlined(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v[4]);
  }
}
BENCHMARK(BM_InlinedVectorIndexInlined);

void BM_InlinedVectorIndexExternal(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v[4]);
  }
}
BENCHMARK(BM_InlinedVectorIndexExternal);

void BM_StdVectorIndex(benchmark::State& state) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v[4]);
  }
}
BENCHMARK(BM_StdVectorIndex);

void BM_InlinedVectorDataInlined(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.data());
  }
}
BENCHMARK(BM_InlinedVectorDataInlined);

void BM_InlinedVectorDataExternal(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.data());
  }
  state.SetItemsProcessed(16 * static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_InlinedVectorDataExternal);

void BM_StdVectorData(benchmark::State& state) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.data());
  }
  state.SetItemsProcessed(16 * static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_StdVectorData);

void BM_InlinedVectorSizeInlined(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.size());
  }
}
BENCHMARK(BM_InlinedVectorSizeInlined);

void BM_InlinedVectorSizeExternal(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.size());
  }
}
BENCHMARK(BM_InlinedVectorSizeExternal);

void BM_StdVectorSize(benchmark::State& state) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.size());
  }
}
BENCHMARK(BM_StdVectorSize);

void BM_InlinedVectorEmptyInlined(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.empty());
  }
}
BENCHMARK(BM_InlinedVectorEmptyInlined);

void BM_InlinedVectorEmptyExternal(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.empty());
  }
}
BENCHMARK(BM_InlinedVectorEmptyExternal);

void BM_StdVectorEmpty(benchmark::State& state) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.empty());
  }
}
BENCHMARK(BM_StdVectorEmpty);

}  // namespace
