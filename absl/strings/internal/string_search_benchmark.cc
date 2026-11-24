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

// Benchmarks for substring search through the public APIs that use
// string_search.h. Typical inputs measure the fast path; adversarial inputs
// measure the worst case.

#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"

namespace {

constexpr size_t kHaystackSize = 64 << 10;

// Text made of random words, separated by spaces.
std::string TypicalText(size_t size, unsigned seed) {
  static const char* const kWords[] = {
      "the",    "of",     "and",    "to",     "in",       "is",    "that",
      "for",    "it",     "as",     "was",    "with",     "be",    "by",
      "on",     "not",    "he",     "this",   "are",      "or",    "his",
      "from",   "at",     "which",  "but",    "have",     "an",    "had",
      "they",   "you",    "were",   "their",  "one",      "all",   "we",
      "can",    "her",    "has",    "there",  "been",     "if",    "more",
      "when",   "will",   "would",  "who",    "so",       "no",    "string",
      "abseil", "search", "linear", "needle", "haystack", "period"};
  std::mt19937 rng(seed);
  std::uniform_int_distribution<size_t> dist(0, std::size(kWords) - 1);
  std::string s;
  while (s.size() < size) {
    s += kWords[dist(rng)];
    s += ' ';
  }
  s.resize(size);
  return s;
}

// A needle of `size` bytes of typical text that does not occur in the
// haystack (it ends with a byte that does not occur in TypicalText()).
std::string TypicalNeedle(size_t size) {
  std::string s = TypicalText(size - 1, 12345);
  s += '#';
  return s;
}

std::string AdversarialNeedle(size_t size) {
  return std::string(size - 1, 'a') + "b";
}

void BM_StrContainsTypical(benchmark::State& state) {
  const std::string haystack = TypicalText(kHaystackSize, 1);
  const std::string needle = TypicalNeedle(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(haystack);
    benchmark::DoNotOptimize(absl::StrContains(haystack, needle));
  }
  state.SetBytesProcessed(state.iterations() * haystack.size());
}
BENCHMARK(BM_StrContainsTypical)->Arg(4)->Arg(64)->Arg(65)->Arg(256)->Arg(4096);

void BM_StrContainsEarlyMatch(benchmark::State& state) {
  const std::string needle = TypicalNeedle(state.range(0));
  const std::string haystack =
      TypicalText(100, 1) + needle + TypicalText(kHaystackSize, 2);
  for (auto _ : state) {
    benchmark::DoNotOptimize(haystack);
    benchmark::DoNotOptimize(absl::StrContains(haystack, needle));
  }
}
BENCHMARK(BM_StrContainsEarlyMatch)->Arg(65)->Arg(256)->Arg(4096);

// Many short rows and a needle that is longer than most of them.
void BM_StrContainsShortRows(benchmark::State& state) {
  std::vector<std::string> rows;
  for (unsigned i = 0; i < 1000; ++i)
    rows.push_back(TypicalText(40 + i % 80, i));
  const std::string needle = TypicalNeedle(state.range(0));
  for (auto _ : state) {
    for (const std::string& row : rows) {
      benchmark::DoNotOptimize(absl::StrContains(row, needle));
    }
  }
  state.SetItemsProcessed(state.iterations() * rows.size());
}
BENCHMARK(BM_StrContainsShortRows)->Arg(65)->Arg(100)->Arg(1000);

void BM_StrContainsAdversarial(benchmark::State& state) {
  const std::string haystack(kHaystackSize, 'a');
  const std::string needle = AdversarialNeedle(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(haystack);
    benchmark::DoNotOptimize(absl::StrContains(haystack, needle));
  }
  state.SetBytesProcessed(state.iterations() * haystack.size());
}
BENCHMARK(BM_StrContainsAdversarial)->Arg(64)->Arg(256)->Arg(4096);

void BM_StrContainsIgnoreCaseTypical(benchmark::State& state) {
  const std::string haystack = TypicalText(kHaystackSize, 1);
  const std::string needle = TypicalNeedle(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(haystack);
    benchmark::DoNotOptimize(absl::StrContainsIgnoreCase(haystack, needle));
  }
  state.SetBytesProcessed(state.iterations() * haystack.size());
}
BENCHMARK(BM_StrContainsIgnoreCaseTypical)
    ->Arg(4)
    ->Arg(64)
    ->Arg(256)
    ->Arg(4096);

void BM_StrContainsIgnoreCaseAdversarial(benchmark::State& state) {
  const std::string haystack(kHaystackSize, 'a');
  const size_t k = state.range(0) / 2;
  const std::string needle = std::string(k, 'a') + "b" + std::string(k, 'a');
  for (auto _ : state) {
    benchmark::DoNotOptimize(haystack);
    benchmark::DoNotOptimize(absl::StrContainsIgnoreCase(haystack, needle));
  }
  state.SetBytesProcessed(state.iterations() * haystack.size());
}
BENCHMARK(BM_StrContainsIgnoreCaseAdversarial)->Arg(64)->Arg(256)->Arg(4096);

void BM_StrSplitTypical(benchmark::State& state) {
  const std::string delimiter = TypicalNeedle(state.range(0));
  std::string text;
  for (unsigned i = 0; text.size() < kHaystackSize; ++i) {
    text += TypicalText(500, i);
    text += delimiter;
  }
  for (auto _ : state) {
    size_t n = 0;
    for (absl::string_view piece : absl::StrSplit(text, delimiter)) {
      benchmark::DoNotOptimize(piece);
      ++n;
    }
    benchmark::DoNotOptimize(n);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}
BENCHMARK(BM_StrSplitTypical)->Arg(2)->Arg(64)->Arg(256);

void BM_StrReplaceAllTypical(benchmark::State& state) {
  const std::string old = TypicalNeedle(state.range(0));
  std::string text;
  for (unsigned i = 0; text.size() < kHaystackSize; ++i) {
    text += TypicalText(500, i);
    text += old;
  }
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        absl::StrReplaceAll(text, {{old, "x"}, {"the", "THE"}}));
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}
BENCHMARK(BM_StrReplaceAllTypical)->Arg(2)->Arg(64)->Arg(256);

absl::Cord MakeFragmentedCord(absl::string_view s, size_t chunk_size) {
  absl::Cord cord;
  for (size_t i = 0; i < s.size(); i += chunk_size) {
    cord.Append(absl::MakeCordFromExternal(s.substr(i, chunk_size),
                                           [](absl::string_view) {}));
  }
  return cord;
}

void BM_CordFindTypical(benchmark::State& state) {
  const std::string haystack = TypicalText(kHaystackSize, 1);
  const absl::Cord cord = MakeFragmentedCord(haystack, 4096);
  const std::string needle = TypicalNeedle(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(cord.Find(needle));
  }
  state.SetBytesProcessed(state.iterations() * haystack.size());
}
BENCHMARK(BM_CordFindTypical)->Arg(4)->Arg(64)->Arg(128)->Arg(256)->Arg(4096);

void BM_CordFindAdversarial(benchmark::State& state) {
  const std::string haystack(kHaystackSize, 'a');
  const absl::Cord cord = MakeFragmentedCord(haystack, 4096);
  const std::string needle = AdversarialNeedle(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(cord.Find(needle));
  }
  state.SetBytesProcessed(state.iterations() * haystack.size());
}
BENCHMARK(BM_CordFindAdversarial)->Arg(64)->Arg(128)->Arg(256)->Arg(4096);

}  // namespace
