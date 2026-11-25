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

#include "absl/random/discrete_distribution.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

// The maximum value of `fabs(sum - 1.0)` that can be tolerated before rescaling
// the probabilities.
constexpr double kRoundingError = 16.0 * std::numeric_limits<double>::epsilon();

// A stateful `double` adder using Neumaier compensated summation that
// saturates at infinity.
class NeumaierSum {
 public:
  NeumaierSum& operator+=(double value) {
    const double t = sum_ + value;
    if (std::isfinite(t)) {
      compensation_ +=
          (sum_ >= value) ? (sum_ - t) + value : (value - t) + sum_;
    } else {
      compensation_ = 0.0;
    }
    sum_ = t;
    return *this;
  }

  friend NeumaierSum operator+(NeumaierSum adder, double value) {
    adder += value;
    return adder;
  }

  double value() const { return sum_ + compensation_; }

 private:
  double sum_;
  double compensation_;
};

}  // namespace

// Initializes the distribution table for Walker's Aliasing algorithm, described
// in Knuth, Vol 2. as well as in https://en.wikipedia.org/wiki/Alias_method
std::vector<std::pair<double, size_t>> InitDiscreteDistribution(
    std::vector<double>& probabilities) {
  // The empty-case should already be handled by the constructor.
  assert(!probabilities.empty());
  for (double& item : probabilities) {
    const bool is_valid_probability_mass = std::isfinite(item) && item >= 0.0;
    assert(is_valid_probability_mass);
    // Gracefully handle non-positive or non-finite weights (and canonicalize
    // -0.0 to +0.0 without signaling FE_INVALID on quiet NaN).
    if (!is_valid_probability_mass || item == 0.0) {
      item = 0.0;
    }
  }

  // Step 1. Normalize the input probabilities to 1.0.
  const double max_weight = *absl::c_max_element(probabilities);
  if (max_weight == 0.0) {
    // Computed non-negative weights can underflow or clamp to all zeros; fall
    // back to std::discrete_distribution's empty-range default ({1.0}) rather
    // than dividing 0.0 / 0.0 into NaN.
    probabilities.assign(1, 1.0);
    return {{1.0, 0}};
  }

  double sum = absl::c_accumulate(probabilities, NeumaierSum{}).value();
  const bool overflowed = !std::isfinite(sum);
  if (overflowed) {
    for (double& item : probabilities) {
      item /= max_weight;
    }
    sum = absl::c_accumulate(probabilities, NeumaierSum{}).value();
  }
  if (overflowed || sum < 1.0 - kRoundingError || sum > 1.0 + kRoundingError) {
    // Rescale on overflow or if the sum exceeds the rounding error threshold.
    for (double& item : probabilities) {
      item = item / sum;
    }
  }

  // Step 2. At this point `probabilities` is set to the conditional
  // probabilities of each element which sum to 1.0, to within reasonable error.
  // These values are used to construct the proportional probability tables for
  // the selection phases of Walker's Aliasing algorithm.
  //
  // To construct the table, pick an element which is under-full (i.e., an
  // element for which `probabilities[i] < 1.0/n`), and pair it with an
  // element which is over-full (i.e., an element for which
  // `probabilities[i] > 1.0/n`). The smaller value can always be retired.
  // The larger may still be greater than 1.0/n, or may now be less than 1.0/n,
  // and put back onto the appropriate collection.
  const size_t n = probabilities.size();
  std::vector<std::pair<double, size_t>> q;
  q.reserve(n);

  std::vector<size_t> over;
  std::vector<size_t> under;
  size_t idx = 0;
  for (const double item : probabilities) {
    assert(item >= 0.0);
    const double v = item * n;
    q.emplace_back(v, 0);
    if (v < 1.0) {
      under.push_back(idx++);
    } else {
      over.push_back(idx++);
    }
  }
  while (!over.empty() && !under.empty()) {
    auto lo = under.back();
    under.pop_back();
    auto hi = over.back();
    over.pop_back();

    q[lo].second = hi;
    const double r = q[hi].first - (1.0 - q[lo].first);
    q[hi].first = r;
    if (r < 1.0) {
      under.push_back(hi);
    } else {
      over.push_back(hi);
    }
  }

  // Due to rounding errors, there may be un-paired elements in either
  // collection; these should all be values near 1.0.  For these values, set `q`
  // to 1.0 and set the alternate to the identity.
  for (auto i : over) {
    q[i] = {1.0, i};
  }
  for (auto i : under) {
    q[i] = {1.0, i};
  }
  return q;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
