// Copyright 2026 The Abseil Authors.
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

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"

namespace {

// Benchmark integer insert operations
void BM_Insert_Int(benchmark::State& state) {
  const int size = state.range(0);

  for (auto _ : state) {
    state.PauseTiming();
    absl::flat_hash_map<int, int> map;
    state.ResumeTiming();

    for (int i = 0; i < size; ++i) {
      benchmark::DoNotOptimize(map.insert({i, i * 2}));
    }
  }

  state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_Insert_Int)->Range(1, 1 << 16);

// Benchmark integer lookup operations (successful lookups)
void BM_Lookup_Int_Hit(benchmark::State& state) {
  const int size = state.range(0);
  absl::flat_hash_map<int, int> map;

  // Populate map
  for (int i = 0; i < size; ++i) {
    map.insert({i, i * 2});
  }

  for (auto _ : state) {
    for (int i = 0; i < size; ++i) {
      benchmark::DoNotOptimize(map.find(i));
    }
  }

  state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_Lookup_Int_Hit)->Range(1, 1 << 16);

// Benchmark integer lookup operations (failed lookups)
void BM_Lookup_Int_Miss(benchmark::State& state) {
  const int size = state.range(0);
  absl::flat_hash_map<int, int> map;

  // Populate map with even numbers
  for (int i = 0; i < size; ++i) {
    map.insert({i * 2, i});
  }

  for (auto _ : state) {
    // Look for odd numbers (which don't exist)
    for (int i = 0; i < size; ++i) {
      benchmark::DoNotOptimize(map.find(i * 2 + 1));
    }
  }

  state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_Lookup_Int_Miss)->Range(1, 1 << 16);

// Benchmark string insert operations
constexpr absl::string_view kFormatShort = "%10d";
constexpr absl::string_view kFormatLong =
    "a longer string that exceeds the SSO %10d";

void BM_Insert_String_Short(benchmark::State& state) {
  const int size = state.range(0);
  std::vector<std::string> keys;
  keys.reserve(size);

  for (int i = 0; i < size; ++i) {
    keys.push_back(absl::StrFormat(kFormatShort, i));
  }

  for (auto _ : state) {
    state.PauseTiming();
    absl::flat_hash_map<std::string, int> map;
    state.ResumeTiming();

    for (const auto& key : keys) {
      benchmark::DoNotOptimize(map.insert({key, 0}));
    }
  }

  state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_Insert_String_Short)->Range(1, 1 << 16);

void BM_Insert_String_Long(benchmark::State& state) {
  const int size = state.range(0);
  std::vector<std::string> keys;
  keys.reserve(size);

  for (int i = 0; i < size; ++i) {
    keys.push_back(absl::StrFormat(kFormatLong, i));
  }

  for (auto _ : state) {
    state.PauseTiming();
    absl::flat_hash_map<std::string, int> map;
    state.ResumeTiming();

    for (const auto& key : keys) {
      benchmark::DoNotOptimize(map.insert({key, 0}));
    }
  }

  state.SetItemsProcessed(state.iterations() * size);
}
BENCHMARK(BM_Insert_String_Long)->Range(1, 1 << 16);

// -----------------------------------------------------------------------------
// Load Factor Benchmarks
// -----------------------------------------------------------------------------
// These benchmarks measure how performance changes as the hash map fills up.
// Load factor = size / capacity. Higher load factor means more collisions.

// Benchmark lookup performance at different load factors
void BM_Lookup_Int_LoadFactor(benchmark::State& state) {
  const int target_size = 10000;
  const double target_load_factor =
      state.range(0) / 100.0;  // Convert from percent

  // Calculate capacity needed to achieve target load factor
  const int capacity = static_cast<int>(target_size / target_load_factor);

  absl::flat_hash_map<int, int> map;
  map.reserve(capacity);

  // Fill to target size
  for (int i = 0; i < target_size; ++i) {
    map.insert({i, i * 2});
  }

  const double actual_load_factor = map.load_factor();

  for (auto _ : state) {
    for (int i = 0; i < target_size; ++i) {
      benchmark::DoNotOptimize(map.find(i));
    }
  }

  state.SetItemsProcessed(state.iterations() * target_size);
  state.SetLabel(absl::StrFormat("load_factor=%.2f", actual_load_factor));
}
// Test load factors: 25%, 50%, 75%, 87%
BENCHMARK(BM_Lookup_Int_LoadFactor)->Arg(25)->Arg(50)->Arg(75)->Arg(87);

// Benchmark insert performance as map approaches different load factors
void BM_InsertToLoadFactor(benchmark::State& state) {
  const int target_size = 1000;
  const double target_load_factor = state.range(0) / 100.0;
  const int capacity = static_cast<int>(target_size / target_load_factor);

  for (auto _ : state) {
    state.PauseTiming();
    absl::flat_hash_map<int, int> map;
    map.reserve(capacity);
    state.ResumeTiming();

    for (int i = 0; i < target_size; ++i) {
      benchmark::DoNotOptimize(map.insert({i, i * 2}));
    }

    state.PauseTiming();
    benchmark::DoNotOptimize(map.load_factor());
    state.ResumeTiming();
  }

  state.SetItemsProcessed(state.iterations() * target_size);
}
BENCHMARK(BM_InsertToLoadFactor)->Arg(25)->Arg(50)->Arg(75)->Arg(87);

// Benchmark iteration performance at different load factors
void BM_Iteration_LoadFactor(benchmark::State& state) {
  const int target_size = 10000;
  const double target_load_factor = state.range(0) / 100.0;
  const int capacity = static_cast<int>(target_size / target_load_factor);

  absl::flat_hash_map<int, int> map;
  map.reserve(capacity);

  for (int i = 0; i < target_size; ++i) {
    map.insert({i, i * 2});
  }

  const double actual_load_factor = map.load_factor();

  for (auto _ : state) {
    int sum = 0;
    for (const auto& kv : map) {
      benchmark::DoNotOptimize(sum += kv.second);
    }
    benchmark::DoNotOptimize(sum);
  }

  state.SetItemsProcessed(state.iterations() * target_size);
  state.SetLabel(absl::StrFormat("load_factor=%.2f", actual_load_factor));
}
BENCHMARK(BM_Iteration_LoadFactor)->Arg(25)->Arg(50)->Arg(75)->Arg(87);

// Benchmark the cost of growing the table (with vs without reserve)
void BM_InsertWithReserve(benchmark::State& state) {
  const int size = state.range(0);

  for (auto _ : state) {
    state.PauseTiming();
    absl::flat_hash_map<int, int> map;
    map.reserve(size);  // Pre-allocate to avoid rehashing
    state.ResumeTiming();

    for (int i = 0; i < size; ++i) {
      benchmark::DoNotOptimize(map.insert({i, i * 2}));
    }
  }

  state.SetItemsProcessed(state.iterations() * size);
  state.SetLabel("with_reserve");
}
BENCHMARK(BM_InsertWithReserve)->Range(1, 1 << 16);

void BM_InsertWithoutReserve(benchmark::State& state) {
  const int size = state.range(0);

  for (auto _ : state) {
    state.PauseTiming();
    absl::flat_hash_map<int, int> map;
    // No reserve - let it grow naturally
    state.ResumeTiming();

    for (int i = 0; i < size; ++i) {
      benchmark::DoNotOptimize(map.insert({i, i * 2}));
    }
  }

  state.SetItemsProcessed(state.iterations() * size);
  state.SetLabel("no_reserve");
}
BENCHMARK(BM_InsertWithoutReserve)->Range(1, 1 << 16);

}  // namespace
