#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace learning {

absl::flat_hash_map<std::string, int> CountWords(const std::vector<std::string>& words) {
  absl::flat_hash_map<std::string, int> counts;
  for (const auto& w : words) {
    ++counts[w];
  }
  return counts;
}

int MostFrequentCount(const absl::flat_hash_map<std::string, int>& counts) {
  int best = 0;
  for (const auto& kv : counts) {
    if (kv.second > best) best = kv.second;
  }
  return best;
}

}  // namespace learning

int main() {
  std::vector<std::string> words = {"absl", "status", "absl", "time", "absl", "map", "time"};
  auto counts = learning::CountWords(words);
  int best = learning::MostFrequentCount(counts);
  (void)best;
  return 0;
}
