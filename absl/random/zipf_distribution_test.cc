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

#include "absl/random/zipf_distribution.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/generate_real.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

namespace {

using ::absl::random_internal::kChiSquared;
using ::absl::random_internal::MaxIntegralDoubleLe;
using ::absl::random_internal::ZipfParam;
using ::testing::AllOf;
using ::testing::DoubleNear;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::Le;
using ::testing::Lt;
using ::testing::Pair;

// Fixed nothing-up-my-sleeve seed for deterministic sampling tests (fractional
// hex digits of Euler's number e - 2, used across Abseil random tests).
constexpr uint64_t kTestSeed = 0x2B7E151628AED2A6ULL;
constexpr uint64_t kHalfBits = uint64_t{1} << 63;     // w = 0.5
constexpr uint64_t kSmallestPosBits = 1ull;           // w = 2^-64
constexpr uint64_t kLargeK = 1000000000000000000ull;  // 10^18
constexpr uint64_t kTwoToThe53 = uint64_t{1}
                                 << std::numeric_limits<double>::digits;
constexpr double kDblMax = (std::numeric_limits<double>::max)();
constexpr double kDenormMin = std::numeric_limits<double>::denorm_min();

template <typename IntType>
class ZipfDistributionTypedTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int, int8_t, int16_t, int32_t, int64_t,
                                  uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(ZipfDistributionTypedTest, IntTypes);

TYPED_TEST(ZipfDistributionTypedTest, SerializeTest) {
  using param_type = typename absl::zipf_distribution<TypeParam>::param_type;

  constexpr int kCount = 1000;
  absl::InsecureBitGen gen;
  for (const auto& param : {
           param_type(),
           param_type(0),
           param_type(32),
           param_type(100, 3, 2),
           param_type(std::numeric_limits<TypeParam>::max(), 4, 3),
           param_type(std::numeric_limits<TypeParam>::max() / 2),
       }) {
    // Validate parameters.
    const auto k = param.k();
    const auto q = param.q();
    const auto v = param.v();

    absl::zipf_distribution<TypeParam> before(k, q, v);
    EXPECT_EQ(before.k(), param.k());
    EXPECT_EQ(before.q(), param.q());
    EXPECT_EQ(before.v(), param.v());

    {
      absl::zipf_distribution<TypeParam> via_param(param);
      EXPECT_EQ(via_param, before);
    }

    // Validate stream serialization.
    std::stringstream ss;
    ss << before;
    absl::zipf_distribution<TypeParam> after(4, 5.5, 4.4);

    EXPECT_NE(before.k(), after.k());
    EXPECT_NE(before.q(), after.q());
    EXPECT_NE(before.v(), after.v());
    EXPECT_NE(before.param(), after.param());
    EXPECT_NE(before, after);

    ss >> after;

    EXPECT_FALSE(ss.fail());
    EXPECT_EQ(before.k(), after.k());
    EXPECT_EQ(before.q(), after.q());
    EXPECT_EQ(before.v(), after.v());
    EXPECT_EQ(before.param(), after.param());
    EXPECT_EQ(before, after);

    // Smoke test.
    auto sample_min = after.max();
    auto sample_max = after.min();
    for (int i = 0; i < kCount; i++) {
      auto sample = after(gen);
      EXPECT_GE(sample, after.min());
      EXPECT_LE(sample, after.max());
      if (sample > sample_max) sample_max = sample;
      if (sample < sample_min) sample_min = sample;
    }
    LOG(INFO) << "Range: " << +sample_min << ", " << +sample_max;
  }
}

class ZipfModel {
 public:
  ZipfModel(uint64_t k, double q, double v) : k_(k), q_(q), v_(v) {}

  double mean() const { return mean_; }

  // For the other moments of the Zipf distribution, see, for example,
  // http://mathworld.wolfram.com/ZipfDistribution.html

  // PMF(i) = (1 + i / v)^(-q) / Z
  // Returns the probability that any single invocation returns i.
  double PMF(uint64_t i) {
    return i >= weights_.size()
               ? 0.0
               : weights_[static_cast<size_t>(i)] / sum_weights_;
  }

  // CDF(i) = P(X < i) = sum_{j=0}^{i-1} PMF(j).
  double CDF(uint64_t i) {
    if (i == 0) {
      return 0.0;
    }
    if (i > cdf_.size()) {
      return 1.0;
    }
    if (i == cdf_.size() && !truncated_) {
      return 1.0;
    }
    return cdf_[static_cast<size_t>(i - 1)] / sum_weights_;
  }

  // The InverseCDF returns the k values which bound p on the upper and lower
  // bound. Since there is no closed-form solution, this is implemented as a
  // bisection of the cdf.
  std::pair<uint64_t, uint64_t> InverseCDF(double p) {
    uint64_t min = 0;
    uint64_t max = static_cast<uint64_t>(cdf_.size()) + (truncated_ ? 1 : 0);
    while (max > min + 1) {
      uint64_t target = (max + min) >> 1;
      double x = CDF(target);
      if (x > p) {
        max = target;
      } else {
        min = target;
      }
    }
    return {min, max};
  }

  // Compute the unnormalized probability totals based on (1 + i / v)^(-q).
  void Init() {
    if (!weights_.empty()) {
      return;
    }
    const size_t reserve_n =
        (k_ < uint64_t{1000}) ? static_cast<size_t>(k_ + 1) : size_t{1001};
    weights_.reserve(reserve_n);
    cdf_.reserve(reserve_n);

    sum_weights_ = 0.0;
    double weighted_i_sum = 0.0;
    truncated_ = false;
    uint64_t i = 0;
    for (;; ++i) {
      const double lp = std::log1p(static_cast<double>(i) / v_);
      const double weight = std::exp(-q_ * lp);
      sum_weights_ += weight;
      weighted_i_sum += static_cast<double>(i) * weight;
      weights_.push_back(weight);
      cdf_.push_back(sum_weights_);
      if (i == k_) {
        break;
      }
      if (i >= 10000 && weight <= 1e-12 * sum_weights_) {
        truncated_ = true;
        break;
      }
    }
    if (truncated_) {
      const double kd = MaxIntegralDoubleLe(k_);
      const double z_lo =
          -(q_ - 1.0) * std::log1p((static_cast<double>(i) + 0.5) / v_);
      const double z_hi = -(q_ - 1.0) * std::log1p((kd + 0.5) / v_);
      const double tail =
          v_ * std::exp(z_lo) * (-std::expm1(z_hi - z_lo)) / (q_ - 1.0);
      sum_weights_ += tail;
    }
    assert(sum_weights_ > 0);
    mean_ = weighted_i_sum / sum_weights_;
  }

 private:
  const uint64_t k_;
  const double q_;
  const double v_;

  double mean_ = 0.0;
  std::vector<double> weights_;
  std::vector<double> cdf_;
  double sum_weights_ = 0.0;
  bool truncated_ = false;
};

using zipf_u64 = absl::zipf_distribution<uint64_t>;

class ZipfTest : public testing::TestWithParam<zipf_u64::param_type>,
                 public ZipfModel {
 public:
  ZipfTest() : ZipfModel(GetParam().k(), GetParam().q(), GetParam().v()) {}

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the quality of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{kTestSeed};
};

TEST_P(ZipfTest, ChiSquaredTest) {
  const auto& param = GetParam();
  Init();

  size_t trials = 10000;

  // Find the split-points for the buckets.
  std::vector<uint64_t> points;
  std::vector<double> expected;
  {
    double last_cdf = 0.0;
    double min_p = 1.0;
    for (double p = 0.01; p < 1.0; p += 0.01) {
      auto x = InverseCDF(p);
      if (points.empty() || points.back() < x.second) {
        const double cdf_at_split = CDF(x.second);
        if (cdf_at_split <= last_cdf) {
          continue;
        }
        points.push_back(x.second);
        double q = cdf_at_split - last_cdf;
        expected.push_back(q);
        last_cdf = cdf_at_split;
        if (q < min_p) {
          min_p = q;
        }
      }
    }
    if (last_cdf < 0.999 || (points.size() == 1 && last_cdf < 1.0)) {
      points.push_back(std::numeric_limits<uint64_t>::max());
      double q = 1.0 - last_cdf;
      expected.push_back(q);
      if (q < min_p) {
        min_p = q;
      }
    } else {
      points.back() = std::numeric_limits<uint64_t>::max();
      expected.back() += (1.0 - last_cdf);
    }
    // The Chi-Squared score is not completely scale-invariant; it works best
    // when the small values are in the small digits.
    trials = static_cast<size_t>(8.0 / min_p);
  }
  ASSERT_GT(points.size(), 1);

  // Generate n variates and fill the counts vector with the count of their
  // occurrences.
  std::vector<int64_t> buckets(points.size(), 0);
  double avg = 0;
  {
    zipf_u64 dis(param);
    for (size_t i = 0; i < trials; i++) {
      uint64_t x = dis(rng_);
      ASSERT_LE(x, dis.max());
      ASSERT_GE(x, dis.min());
      avg += static_cast<double>(x);
      auto it = std::upper_bound(points.begin(), points.end() - 1, x);
      buckets[static_cast<size_t>(std::distance(points.begin(), it))]++;
    }
    avg = avg / static_cast<double>(trials);
  }

  // Validate the output using the Chi-Squared test.
  for (auto& e : expected) {
    e *= static_cast<double>(trials);
  }

  // The null-hypothesis is that the distribution is a Zipf distribution with
  // the provided k, q, and v (not estimated from the data).
  const int dof = static_cast<int>(expected.size()) - 1;
  ASSERT_GT(dof, 0);

  // NOTE: This test runs ~30x per invocation, so a value of 0.9999 gives a low
  // suite-wide false positive rate.
  const double threshold = absl::random_internal::ChiSquareValue(dof, 0.9999);

  const double chi_square = absl::random_internal::ChiSquare(
      std::begin(buckets), std::end(buckets), std::begin(expected),
      std::end(expected));

  const double p_actual =
      absl::random_internal::ChiSquarePValue(chi_square, dof);

  // Log if the chi_squared value is above the threshold.
  if (chi_square > threshold) {
    LOG(INFO) << "values";
    for (size_t i = 0; i < expected.size(); i++) {
      LOG(INFO) << points[i] << ": " << buckets[i] << " vs. E=" << expected[i];
    }
    LOG(INFO) << "trials " << trials;
    LOG(INFO) << "mean " << avg << " vs. expected " << mean();
    LOG(INFO) << kChiSquared << "(data, " << dof << ") = " << chi_square << " ("
              << p_actual << ")";
    LOG(INFO) << kChiSquared << " @ 0.9999 = " << threshold;
    FAIL() << kChiSquared << " value of " << chi_square
           << " is above the threshold.";
  }
}

std::vector<zipf_u64::param_type> GenParams() {
  using param = zipf_u64::param_type;
  constexpr double kDblMax = (std::numeric_limits<double>::max)();
  const double kQMin = std::nextafter(1.0, 2.0);
  const auto k = param().k();
  const auto q = param().q();
  const auto v = param().v();
  const uint64_t k2 = 1 << 10;
  return std::vector<zipf_u64::param_type>{
      // Default
      param(k, q, v),
      // Vary K (including k = 1 and odd k where rint(k + 0.5) == k + 1).
      param(1, q, v), param(4, q, v), param(5, q, v), param(5, 1.5, 0.5),
      param(1 << 4, q, v), param(k2, q, v),
      // Vary V (including v << 1 and quadratic v > 1e300).
      param(k2, q, 0.5), param(k2, q, 1.5), param(k2, q, 2.5), param(k2, q, 10),
      param(k2, 1.2, 0.01), param(100, q, kDblMax),
      // Vary Q (including q immediately below and above the q == 2 fast path).
      param(k2, 1.5, v), param(k2, std::nextafter(2.0, 1.0), v),
      param(k2, std::nextafter(2.0, 3.0), v), param(k2, 3, v), param(k2, 5, v),
      param(k2, 10, v),
      // Vary V & Q: general, curved and flat zm >= -0.5, and linear limit.
      param(k2, 1.5, 0.5), param(k2, 3, 1.5), param(k, 10, 10),
      param(100, 1.1, 10.0), param(100, 1.0 + 1e-15, 10.0),
      param(100, 2.5, 1e16), param(100, kQMin, kDblMax),
      // LogAddExp inversion (zm < -708) with subnormal and zero exp(zm).
      param(8000, 200.0, 200.0), param(10000, 200.0, 200.0),
      // Huge (q, v) with finite q / v (truncated geometric; y >= 1e100).
      param(100, 1e100, 1e100), param(100, kDblMax, kDblMax),
      param(200, 0.1 * kDblMax, kDblMax)};
}

std::string ParamName(
    const ::testing::TestParamInfo<zipf_u64::param_type>& info) {
  const auto& p = info.param;
  std::string q_str = (p.q() == std::nextafter(2.0, 1.0)) ? "2_minus_eps"
                      : (p.q() == std::nextafter(2.0, 3.0))
                          ? "2_plus_eps"
                          : absl::StrCat(absl::SixDigits(p.q()));
  std::string name =
      absl::StrCat("k_", p.k(), "__q_", q_str, "__v_", absl::SixDigits(p.v()));
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(All, ZipfTest, ::testing::ValuesIn(GenParams()),
                         ParamName);

// NOTE: absl::zipf_distribution is not guaranteed to be stable.
TEST(ZipfDistributionTest, StabilityTest) {
  // absl::zipf_distribution stability depends on
  // absl::random_internal::GenerateRealFromBits, std::exp, std::expm1,
  // std::log, std::log1p, and std::rint.
  absl::random_internal::sequence_urbg urbg(
      {0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
       0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
       0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
       0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull});

  const auto sample_sequence = [&urbg](int32_t k, double q, double v) {
    urbg.reset();
    absl::zipf_distribution<int32_t> dist(k, q, v);
    std::vector<int32_t> output(10);
    std::generate(output.begin(), output.end(), [&] { return dist(urbg); });
    return std::make_pair(std::move(output), urbg.invocations());
  };

  constexpr int32_t kI32Max = (std::numeric_limits<int32_t>::max)();
  constexpr double kDblMax = (std::numeric_limits<double>::max)();

  // Quadratic inversion (q == 2, v == 1).
  EXPECT_THAT(sample_sequence(kI32Max, 2.0, 1.0),
              Pair(ElementsAre(10031, 0, 0, 3, 6, 0, 7, 47, 0, 0), 11u));
  // General inversion with zm in [-708, -0.5).
  EXPECT_THAT(sample_sequence(kI32Max, 3.3, 1.0),
              Pair(ElementsAre(44, 0, 0, 0, 0, 1, 0, 1, 3, 0), 10u));
  // General inversion with zm >= -0.5 (expm1/log1p path) and v != 1.
  EXPECT_THAT(
      sample_sequence(1000, 1.05, 10.0),
      Pair(ElementsAre(1000, 0, 16, 130, 416, 648, 3, 666, 936, 23), 10u));
  // General inversion with zm < -708 (LogAddExp path).
  EXPECT_THAT(sample_sequence(10000, 200.0, 200.0),
              Pair(ElementsAre(0, 0, 0, 1, 2, 0, 2, 4, 0, 0), 11u));
  // Inline linear-limit inversion (q * log1p(xm / v) <= 1e-40).
  EXPECT_THAT(
      sample_sequence(1000, std::nextafter(1.0, 2.0), kDblMax),
      Pair(ElementsAre(1000, 0, 237, 604, 833, 918, 74, 923, 988, 291), 10u));
}

TEST(ZipfDistributionTest, AlgorithmBounds) {
  absl::zipf_distribution<int32_t> dist;

  // Small values from GenerateRealFromBits map to larger Zipf variates.
  const std::pair<uint64_t, int32_t> kInputs[] = {
      {0xffffffffffffffff, 0x0}, {0x7fffffffffffffff, 0x0},
      {0x3ffffffffffffffb, 0x1}, {0x1ffffffffffffffd, 0x4},
      {0xffffffffffffffe, 0x9},  {0x7ffffffffffffff, 0x12},
      {0x3ffffffffffffff, 0x25}, {0x1ffffffffffffff, 0x4c},
      {0xffffffffffffff, 0x99},  {0x7fffffffffffff, 0x132},
      {0x3fffffffffffff, 0x265}, {0x1fffffffffffff, 0x4cc},
      {0xfffffffffffff, 0x999},  {0x7ffffffffffff, 0x1332},
      {0x3ffffffffffff, 0x2665}, {0x1ffffffffffff, 0x4ccc},
      {0xffffffffffff, 0x9998},  {0x7fffffffffff, 0x1332f},
      {0x3fffffffffff, 0x2665a}, {0x1fffffffffff, 0x4cc9e},
      {0xfffffffffff, 0x998e0},  {0x7ffffffffff, 0x133051},
      {0x3ffffffffff, 0x265ae4}, {0x1ffffffffff, 0x4c9ed3},
      {0xffffffffff, 0x98e223},  {0x7fffffffff, 0x13058c4},
      {0x3fffffffff, 0x25b178e}, {0x1fffffffff, 0x4a062b2},
      {0xfffffffff, 0x8ee23b8},  {0x7ffffffff, 0x10b21642},
      {0x3ffffffff, 0x1d89d89d}, {0x1ffffffff, 0x2fffffff},
      {0xffffffff, 0x45d1745d},  {0x7fffffff, 0x5a5a5a5a},
      {0x3fffffff, 0x69ee5846},  {0x1fffffff, 0x73ecade3},
      {0xfffffff, 0x79a9d260},   {0x7ffffff, 0x7cc0532b},
      {0x3ffffff, 0x7e5ad146},   {0x1ffffff, 0x7f2c0bec},
      {0xffffff, 0x7f95adef},    {0x7fffff, 0x7fcac0da},
      {0x3fffff, 0x7fe55ae2},    {0x1fffff, 0x7ff2ac0e},
      {0xfffff, 0x7ff955ae},     {0x7ffff, 0x7ffcaac1},
      {0x3ffff, 0x7ffe555b},     {0x1ffff, 0x7fff2aac},
      {0xffff, 0x7fff9556},      {0x7fff, 0x7fffcaab},
      {0x3fff, 0x7fffe555},      {0x1fff, 0x7ffff2ab},
      {0xfff, 0x7ffff955},       {0x7ff, 0x7ffffcab},
      {0x3ff, 0x7ffffe55},       {0x1ff, 0x7fffff2b},
      {0xff, 0x7fffff95},        {0x7f, 0x7fffffcb},
      {0x3f, 0x7fffffe5},        {0x1f, 0x7ffffff3},
      {0xf, 0x7ffffff9},         {0x7, 0x7ffffffd},
      {0x3, 0x7ffffffe},         {0x1, 0x7fffffff},
  };

  for (const auto& instance : kInputs) {
    absl::random_internal::sequence_urbg urbg({instance.first});
    EXPECT_EQ(instance.second, dist(urbg));
  }
}

template <typename IntType>
IntType SampleFromSeedBits(absl::zipf_distribution<IntType> dist,
                           uint64_t first_bits) {
  absl::random_internal::sequence_urbg urbg({first_bits, kHalfBits});
  return dist(urbg);
}

template <typename IntType>
std::vector<IntType> DrawSamples(absl::zipf_distribution<IntType> dist,
                                 size_t count) {
  absl::random_internal::pcg64_2018_engine rng(kTestSeed);
  std::vector<IntType> samples(count);
  std::generate(samples.begin(), samples.end(), [&] { return dist(rng); });
  return samples;
}

class CountingPcgEngine {
 public:
  using result_type = absl::random_internal::pcg64_2018_engine::result_type;

  explicit CountingPcgEngine(uint64_t seed) : rng_(seed) {}
  static constexpr result_type(min)() {
    return (absl::random_internal::pcg64_2018_engine::min)();
  }
  static constexpr result_type(max)() {
    return (absl::random_internal::pcg64_2018_engine::max)();
  }
  result_type operator()() {
    ++invocations_;
    return rng_();
  }
  size_t invocations() const { return invocations_; }

 private:
  absl::random_internal::pcg64_2018_engine rng_;
  size_t invocations_ = 0;
};

template <typename IntType>
size_t MeasureDrawCount(absl::zipf_distribution<IntType> dist, size_t count) {
  CountingPcgEngine rng(kTestSeed);
  for (size_t i = 0; i < count; ++i) {
    (void)dist(rng);
  }
  return rng.invocations();
}

template <typename IntType>
struct SampleSummary {
  IntType min_sample;
  IntType max_sample;
  double mean;
};

template <typename IntType>
void PrintTo(const SampleSummary<IntType>& s, std::ostream* os) {
  *os << "{min_sample=" << +s.min_sample << ", max_sample=" << +s.max_sample
      << ", mean=" << s.mean << "}";
}

template <typename IntType>
SampleSummary<IntType> SummarizeSamples(absl::zipf_distribution<IntType> dist,
                                        size_t count) {
  absl::random_internal::pcg64_2018_engine rng(kTestSeed);
  IntType min_s = dist.max();
  IntType max_s = dist.min();
  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const IntType s = dist(rng);
    min_s = (std::min)(min_s, s);
    max_s = (std::max)(max_s, s);
    sum += static_cast<double>(s);
  }
  return {min_s, max_s, sum / static_cast<double>(count)};
}

MATCHER_P3(IsWithinBoundsWithMean, max_val, expected_mean, tol, "") {
  return arg.min_sample >= 0 && arg.max_sample <= max_val &&
         std::abs(arg.mean - expected_mean) <= tol;
}

struct TailFractionSummary {
  bool all_non_negative;
  uint64_t max_sample;
  double tail_fraction;
};

void PrintTo(const TailFractionSummary& s, std::ostream* os) {
  *os << "{all_non_negative=" << (s.all_non_negative ? "true" : "false")
      << ", max_sample=" << s.max_sample
      << ", tail_fraction=" << s.tail_fraction << "}";
}

template <typename IntType>
TailFractionSummary SummarizeTailFraction(absl::zipf_distribution<IntType> dist,
                                          IntType threshold, size_t count) {
  absl::random_internal::pcg64_2018_engine rng(kTestSeed);
  bool all_non_negative = true;
  uint64_t max_s = 0;
  size_t hits = 0;
  for (size_t i = 0; i < count; ++i) {
    const IntType x = dist(rng);
    if (x < IntType{0}) {
      all_non_negative = false;
      continue;
    }
    max_s = (std::max)(max_s, static_cast<uint64_t>(x));
    if (x >= threshold) {
      ++hits;
    }
  }
  return {all_non_negative, max_s,
          static_cast<double>(hits) / static_cast<double>(count)};
}

MATCHER_P3(HasBoundedTailFraction, max_val, expected_fraction, tol, "") {
  return arg.all_non_negative && arg.max_sample <= max_val &&
         std::abs(arg.tail_fraction - expected_fraction) <= tol;
}

MATCHER_P2(IsSoundSqueezeNear, expected, tol, "") {
  return arg >= 0.0 && arg < 0.5 && std::abs(arg - expected) <= tol;
}

struct ZeroBoundarySummary {
  double zero_probability;
  uint64_t first_nonzero_sample;
};

void PrintTo(const ZeroBoundarySummary& s, std::ostream* os) {
  *os << "{zero_probability=" << s.zero_probability
      << ", first_nonzero_sample=" << s.first_nonzero_sample << "}";
}

ZeroBoundarySummary ScanTopUniformGrid(absl::zipf_distribution<uint64_t> dist,
                                       int grid_steps) {
  constexpr double kStep = 1.0 / static_cast<double>(kTwoToThe53);
  int total_zeros = 0;
  uint64_t first_nonzero = 0;
  for (int m = 1; m <= grid_steps; ++m) {
    // Top 52-bit mantissas in [0.5, 1.0): w = 1.0 - m * 2^-53.
    const uint64_t bits = 0xfffffffffffff800ull -
                          static_cast<uint64_t>(m - 1) * (uint64_t{1} << 11);
    absl::random_internal::sequence_urbg urbg({bits, kSmallestPosBits});
    const uint64_t sample = dist(urbg);
    if (urbg.invocations() != 1) continue;
    if (sample == 0) {
      ++total_zeros;
    } else if (first_nonzero == 0) {
      first_nonzero = sample;
    }
  }
  return {static_cast<double>(total_zeros) * kStep, first_nonzero};
}

MATCHER_P2(MatchesZeroMassWithoutDoubleCounting, expected_prob, tol, "") {
  return std::abs(arg.zero_probability - expected_prob) <= tol &&
         arg.first_nonzero_sample == 1;
}

TEST(ZipfDistributionTest, Int64AndUint64UpperBoundsAvoidFloatCastOverflow) {
  constexpr int64_t kMaxI64 = (std::numeric_limits<int64_t>::max)();
  constexpr uint64_t kMaxU64 = (std::numeric_limits<uint64_t>::max)();
  constexpr int64_t kRoundsUpInDouble = (int64_t{1} << 62) - 1;
  constexpr int64_t kNearMaxRoundsUp = kMaxI64 - 1025;
  constexpr uint64_t kAbove53Bits = kTwoToThe53 + 1;

  // 64-bit k values where static_cast<double>(k) or xm_ = max_kd_ + 0.5 rounds
  // above k (including INT64_MAX -> 2^63 and x87 excess precision): drawing at
  // uniform_double == 0.0 must stay <= k without [conv.fpint] overflow.
  EXPECT_EQ(SampleFromSeedBits(absl::zipf_distribution<int64_t>{}, 0ull),
            kMaxI64 - 1023);
  EXPECT_THAT(
      SampleFromSeedBits(absl::zipf_distribution<int64_t>{}, kSmallestPosBits),
      AllOf(Ge(int64_t{0}), Le(kMaxI64)));
  EXPECT_EQ(SampleFromSeedBits(
                absl::zipf_distribution<int64_t>(kRoundsUpInDouble), 0ull),
            (int64_t{1} << 62) - 512);
  EXPECT_EQ(SampleFromSeedBits(
                absl::zipf_distribution<int64_t>(kNearMaxRoundsUp), 0ull),
            kMaxI64 - 2047);
  EXPECT_EQ(
      SampleFromSeedBits(absl::zipf_distribution<uint64_t>(kAbove53Bits), 0ull),
      kTwoToThe53);
  EXPECT_EQ(SampleFromSeedBits(absl::zipf_distribution<uint64_t>{}, 0ull),
            kMaxU64 - 2047);
}

TEST(ZipfDistributionTest, NearUnitExponentRemainsInBounds) {
  // Near q -> 1+: avoids catastrophic cancellation and negative outputs.
  EXPECT_THAT(
      DrawSamples(absl::zipf_distribution<int>(100, 1.0 + 1e-15, 10.0), 1000),
      Each(AllOf(Ge(0), Le(100))));
}

TEST(ZipfDistributionTest, SteepExponentTerminatesAtZero) {
  // Steep q with both v < 1 and v > 1: P(X = 0) == 1 in IEEE-754 double
  // without pow overflow or infinite rejection loops.
  EXPECT_THAT(DrawSamples(absl::zipf_distribution<int>(100, 1400.0, 0.6), 500),
              Each(0));
  EXPECT_THAT(DrawSamples(absl::zipf_distribution<int>(100, 800.0, 3.0), 500),
              Each(0));
}

TEST(ZipfDistributionTest, LargeVApproachesUniformSupportMean) {
  // Large v = 1e16: previously cancelled exp(...) - v; weights (1 + k/v)^(-q)
  // are nearly uniform over [0, 100] with mean ~50.0.
  EXPECT_THAT(
      SummarizeSamples(absl::zipf_distribution<int>(100, 2.0, 1e16), 10000),
      IsWithinBoundsWithMean(100, 50.0, 1.5));
}

TEST(ZipfDistributionTest, TailProbabilityAccurateAboveTwoToTheFiftyThree) {
  constexpr int64_t kLargeKSigned = static_cast<int64_t>(kLargeK);
  constexpr uint64_t kThreshold = uint64_t{1} << 54;

  // For (1e18, 1.5, 1e14), exact P(X >= 2^54) is ~0.06495. Without accurate
  // squeeze/acceptance above 2^53, tail mass shifts to ~0.0720.
  EXPECT_THAT(
      SummarizeTailFraction(zipf_u64(kLargeK, 1.5, 1e14), kThreshold, 50000),
      HasBoundedTailFraction(kLargeK, 0.06495, 0.003));
  EXPECT_THAT(SummarizeTailFraction(
                  absl::zipf_distribution<int64_t>(kLargeKSigned, 1.5, 1e14),
                  static_cast<int64_t>(kThreshold), 50000),
              HasBoundedTailFraction(kLargeK, 0.06495, 0.003));
}

TEST(ZipfDistributionTest, LargeKMaintainsBoundedRejectionRate) {
  // When k >= 2^53, a broken squeeze threshold rejects ~50% of tail proposals
  // (~95k-97k URBG calls for 50k samples instead of ~50k).
  EXPECT_LT(MeasureDrawCount(zipf_u64(kLargeK, 1.5, 1e16), 50000), 60000u);
  EXPECT_LT(MeasureDrawCount(zipf_u64(kLargeK, 3.0, 1e16), 50000), 60000u);
}

TEST(ZipfDistributionTest, BoundaryRoundingDoesNotDoubleCountZero) {
  // Exact P(X == 0) ~ 1.500047e-15 (~13.51 ULPs of w below 1.0) for q = 2.5
  // and ~1.001e-15 (~9.02 ULPs) for q = 2.0. Double-counting k = 0 from
  // rounding x < 0.5 inflates zero hits above tolerance.
  EXPECT_THAT(ScanTopUniformGrid(zipf_u64(kLargeK, 2.5, 1e15), 64),
              MatchesZeroMassWithoutDoubleCounting(1.500047364598e-15, 7e-17));
  EXPECT_THAT(ScanTopUniformGrid(zipf_u64(kLargeK, 2.0, 1e15), 64),
              MatchesZeroMassWithoutDoubleCounting(1.000999999999e-15, 7e-17));
}

TEST(ZipfDistributionTest, BoundaryRoundingClampsBelowHalfToOne) {
  // Returns {draw(lo), draw(hi)} straddling the exact platform/libm k = 0
  // cutoff, where `lo` is the largest 64-bit URBG draw with
  // `w * total_hat < d0x5` and `hi = lo + 1` satisfies `w * total_hat >= d0x5`.
  // When inversion at `lo` yields `x <= 0.5`, clamping to k = 1 prevents
  // falsely returning 0.
  const auto draw_straddling_cutoff =
      [](uint64_t k, double q, double v) -> std::pair<uint64_t, uint64_t> {
    const ZipfParam zri(MaxIntegralDoubleLe(k), q, v);
    const auto is_below = [&zri](uint64_t bits) {
      const double w = absl::random_internal::GenerateRealFromBits<
          double, absl::random_internal::GeneratePositiveTag, true>(bits);
      return w * zri.total_hat() < zri.d0x5();
    };
    uint64_t lo = 0;
    uint64_t hi = (std::numeric_limits<uint64_t>::max)();
    while (hi - lo > 1) {
      const uint64_t mid = lo + (hi - lo) / 2;
      if (is_below(mid)) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    const auto draw = [&](uint64_t bits) {
      absl::random_internal::sequence_urbg urbg({bits, kSmallestPosBits});
      return zipf_u64(k, q, v)(urbg);
    };
    return {draw(lo), draw(hi)};
  };

  EXPECT_THAT(draw_straddling_cutoff(10000000000ull, 1.05, 1e10),
              Pair(Gt(0u), 0u));
  EXPECT_THAT(draw_straddling_cutoff(100, 2.0, 1e16), Pair(Gt(0u), 0u));
  EXPECT_THAT(draw_straddling_cutoff(100, 1.5, 0.5), Pair(Gt(0u), 0u));
}

TEST(ZipfDistributionTest, NearUnitExponentWithMaxVApproachesUniform) {
  const double q_min = std::nextafter(1.0, 2.0);

  // Near q -> 1+ with v == DBL_MAX, weights (1 + x/v)^(-q) are identically 1
  // in double, so the distribution must be uniform on [0, k] rather than
  // collapsing to 0 or distorting midpoint draws.
  EXPECT_THAT(
      SummarizeSamples(absl::zipf_distribution<int>(1, q_min, kDblMax), 5000),
      IsWithinBoundsWithMean(1, 0.5, 0.04));
  EXPECT_THAT(
      SummarizeSamples(absl::zipf_distribution<int>(100, q_min, kDblMax), 5000),
      IsWithinBoundsWithMean(100, 50.0, 2.0));
  EXPECT_EQ(SampleFromSeedBits(
                absl::zipf_distribution<int>(100, q_min, kDblMax), kHalfBits),
            50);
}

TEST(ZipfDistributionTest, QuadraticSamplingWithHugeVApproachesUniform) {
  // Near-uniform over [0, 127] on both sides of the v = 1e300 ratio split.
  EXPECT_THAT(
      SummarizeSamples(absl::zipf_distribution<int8_t>(127, 2.0, 1e300), 5000),
      IsWithinBoundsWithMean(int8_t{127}, 63.5, 2.5));
  EXPECT_THAT(SummarizeSamples(
                  absl::zipf_distribution<int8_t>(127, 2.0, kDblMax), 5000),
              IsWithinBoundsWithMean(int8_t{127}, 63.5, 2.5));
}

MATCHER_P2(IsRelativelyNear, expected, rel_tol, "") {
  return std::abs(arg - expected) <= rel_tol * std::abs(expected);
}

double ZipfParamRoundTrip(const ZipfParam& p, double x) {
  return p.TailIntegralInv(p.TailIntegral(x));
}

TEST(ZipfParamTest, SqueezeThresholdIsSoundAndBounded) {
  const double kLargeKd = MaxIntegralDoubleLe(kLargeK);

  // Previously cancelled against xm_ (s_ = +1.0, false accept) or disabled the
  // squeeze (s_ = -1.0, tail bias).
  EXPECT_THAT(ZipfParam(kLargeKd, 2.0, 1e16).s(),
              IsSoundSqueezeNear(0.5, 1e-12));
  EXPECT_THAT(ZipfParam(kLargeKd, 1.05, 1e10).s(),
              IsSoundSqueezeNear(0.5, 1e-12));
  EXPECT_THAT(ZipfParam(1000.0, 1.5, 1e-300).s(),
              IsSoundSqueezeNear(0.423020306144, 1e-11));
  EXPECT_THAT(ZipfParam(1000.0, 1.0 + 1e-12, 1e-300).s(),
              IsSoundSqueezeNear(0.448180838243, 1e-11));
  EXPECT_THAT(ZipfParam(1000.0, 1000.0, 1.0).s(),
              IsSoundSqueezeNear(0.012401048681, 1e-11));
  // q / (v + 1) <= 1e-8 fast path.
  EXPECT_THAT(ZipfParam(1000.0, 1e10, 1e100).s(),
              IsSoundSqueezeNear(0.5, 1e-12));
  // y >= 1e100 with finite qr = 1 (limit s = log(1 + exp(-0.5))).
  const double kGeomUnitS = std::log1p(std::exp(-0.5));
  EXPECT_THAT(ZipfParam(1000.0, 1e100, 1e100).s(),
              IsSoundSqueezeNear(kGeomUnitS, 1e-12));
  EXPECT_THAT(ZipfParam(1000.0, kDblMax, kDblMax).s(),
              IsSoundSqueezeNear(kGeomUnitS, 1e-12));
  // qr = (q - 1) / (v + 1) >= 1e300: s ~= (v + 1) * log(qr) / (q - 1) in
  // (0, 0.5).
  EXPECT_THAT(ZipfParam(1000.0, 1e301, 1.0).s(),
              IsSoundSqueezeNear(2.0 * std::log(0.5e301) / 1e301, 1e-310));
  EXPECT_THAT(
      ZipfParam(1000.0, kDblMax, 1.0).s(),
      AllOf(Gt(0.0), Lt(0.5),
            DoubleNear(2.0 * std::log(0.5 * kDblMax) / kDblMax, 1e-317)));
  EXPECT_EQ(ZipfParam(0.0, 2.0, 1.0).s(), 0.0);
}

TEST(ZipfParamTest, MaxIntegralDoubleLeMasksBitsBelowDoublePrecision) {
  constexpr int64_t kMaxI64 = (std::numeric_limits<int64_t>::max)();
  constexpr uint64_t kMaxU64 = (std::numeric_limits<uint64_t>::max)();

  // Types and values that fit the 53-bit double significand are exact.
  EXPECT_EQ(MaxIntegralDoubleLe((std::numeric_limits<int32_t>::max)()),
            static_cast<double>((std::numeric_limits<int32_t>::max)()));
  EXPECT_EQ(MaxIntegralDoubleLe(uint64_t{0}), 0.0);
  EXPECT_EQ(MaxIntegralDoubleLe(kTwoToThe53 - 1),
            static_cast<double>(kTwoToThe53 - 1));
  // Wider values truncate toward zero rather than rounding to nearest, which
  // would otherwise yield 2^63, 2^64, and 2^128.
  EXPECT_EQ(MaxIntegralDoubleLe(kTwoToThe53 + 1),
            static_cast<double>(kTwoToThe53));
  EXPECT_EQ(MaxIntegralDoubleLe(kMaxI64), static_cast<double>(kMaxI64 - 1023));
  EXPECT_EQ(MaxIntegralDoubleLe(kMaxU64), static_cast<double>(kMaxU64 - 2047));
  EXPECT_EQ(MaxIntegralDoubleLe(absl::Uint128Max()),
            std::ldexp(static_cast<double>(kTwoToThe53 - 1), 75));
}

TEST(ZipfParamTest, QuadraticTailIntegralMatchesClosedFormAtZero) {
  // q == 2: D(0) = v * xm / (v + xm).
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 1e-300).TailIntegral(0.0),
              IsRelativelyNear(1e-300, 1e-13));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 0.5).TailIntegral(0.0),
              IsRelativelyNear(0.49975024975024973, 1e-13));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 1.0).TailIntegral(0.0),
              IsRelativelyNear(0.99900149775337, 1e-13));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 1e16).TailIntegral(0.0),
              IsRelativelyNear(1000.4999999999, 1e-13));
}

TEST(ZipfParamTest, QuadraticInversionReturnsZeroExactlyFromDZero) {
  constexpr double kLargeKd = 1e18;

  // TailIntegralInv(d) == 0 iff d >= D(0). For v = 1e-300 and xm ~ 1e18,
  // v / (v + xm) is a 17-bit subnormal, so D(0) must not be derived from it.
  EXPECT_EQ(
      ZipfParam(kLargeKd, 2.0, 1e-300).TailIntegralInv(1e-300 * (1.0 + 1e-9)),
      0.0);
  EXPECT_GT(
      ZipfParam(kLargeKd, 2.0, 1e-300).TailIntegralInv(1e-300 * (1.0 - 1e-9)),
      0.0);
  EXPECT_EQ(ZipfParam(1000.0, 2.0, 0.5)
                .TailIntegralInv(0.49975024975024973 * (1.0 + 1e-9)),
            0.0);
  EXPECT_GT(ZipfParam(1000.0, 2.0, 0.5)
                .TailIntegralInv(0.49975024975024973 * (1.0 - 1e-9)),
            0.0);
  EXPECT_EQ(ZipfParam(1000.0, 2.0, 1e16)
                .TailIntegralInv(1000.4999999999 * (1.0 + 1e-9)),
            0.0);
  EXPECT_GT(ZipfParam(1000.0, 2.0, 1e16)
                .TailIntegralInv(1000.4999999999 * (1.0 - 1e-9)),
            0.0);
}

TEST(ZipfParamTest, QuadraticInversionMatchesClosedForm) {
  constexpr double kLargeKd = 1e18;

  // q == 2: x = 1 / (d / v^2 + 1 / (v + xm)) - v, evaluated to 700 digits.
  // Covers v whose reciprocal is inexact (3, 0.1, 7), huge (1e-300), and
  // subnormal (DBL_MAX).
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 3.0).TailIntegralInv(0.1),
              IsRelativelyNear(79.5925925925926, 1e-12));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 3.0).TailIntegralInv(1.0),
              IsRelativelyNear(5.92, 1e-12));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 3.0).TailIntegralInv(2.9),
              IsRelativelyNear(0.09388006782796371, 1e-12));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 0.1).TailIntegralInv(0.01),
              IsRelativelyNear(0.8990015974440896, 1e-12));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 0.1).TailIntegralInv(0.09),
              IsRelativelyNear(0.011098774205009782, 1e-12));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 1e-300).TailIntegralInv(5e-301),
              IsRelativelyNear(1e-300, 1e-12));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 1e300).TailIntegralInv(500.0),
              IsRelativelyNear(500.5, 1e-12));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, kDblMax).TailIntegralInv(250.25),
              IsRelativelyNear(750.25, 1e-12));
  EXPECT_THAT(ZipfParam(kLargeKd, 2.0, 7.0).TailIntegralInv(1e-12),
              IsRelativelyNear(48997599117636.234, 1e-12));
  EXPECT_THAT(ZipfParam(kLargeKd, 2.0, 7.0).TailIntegralInv(3.5),
              IsRelativelyNear(7.0, 1e-12));
}

TEST(ZipfParamTest, QuadraticInversionWithSubnormalV) {
  // v = 1e-310 is subnormal and 1 / v overflows to +inf, so the inversion
  // must divide by v rather than scale by a precomputed reciprocal, which
  // would return 0 (or NaN at d == 0).
  const ZipfParam subnormal_v(1000.0, 2.0, 1e-310);
  EXPECT_THAT(subnormal_v.TailIntegralInv(5e-311),
              IsRelativelyNear(1e-310, 1e-9));
  EXPECT_THAT(subnormal_v.TailIntegralInv(kDenormMin),
              IsRelativelyNear(2.0240225330729937e-297, 1e-9));
  EXPECT_EQ(subnormal_v.TailIntegralInv(0.0), 1000.5);
}

TEST(ZipfParamTest, QuadraticTailIntegralRoundTrips) {
  const ZipfParam tiny_v(1000.0, 2.0, 1e-300);
  const ZipfParam small_v(1000.0, 2.0, 0.5);
  const ZipfParam unit_v(1000.0, 2.0, 1.0);
  const ZipfParam large_v(1000.0, 2.0, 1e16);

  EXPECT_THAT(ZipfParamRoundTrip(tiny_v, 1e-300),
              IsRelativelyNear(1e-300, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(tiny_v, 1e-295),
              IsRelativelyNear(1e-295, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(small_v, 0.5), IsRelativelyNear(0.5, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(small_v, 10.0), IsRelativelyNear(10.0, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(small_v, 500.0),
              IsRelativelyNear(500.0, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(unit_v, 0.5), IsRelativelyNear(0.5, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(unit_v, 10.0), IsRelativelyNear(10.0, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(unit_v, 500.0), IsRelativelyNear(500.0, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(large_v, 0.5), IsRelativelyNear(0.5, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(large_v, 10.0), IsRelativelyNear(10.0, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(large_v, 500.0),
              IsRelativelyNear(500.0, 1e-9));
}

TEST(ZipfParamTest, GeneralTailIntegralMatchesClosedFormForSmallV) {
  // D(x) = v / (q - 1) * ((1 + x / v)^(1 - q) - (1 + xm / v)^(1 - q)),
  // evaluated to 80 digits.
  const ZipfParam small_v(1000.0, 3.5, 1e-3);
  EXPECT_THAT(small_v.TailIntegral(0.0),
              IsRelativelyNear(0.0003999999999999996, 1e-10));
  EXPECT_THAT(small_v.TailIntegral(0.5),
              IsRelativelyNear(7.11976524558709e-11, 1e-10));
  EXPECT_THAT(small_v.TailIntegral(2.0),
              IsRelativelyNear(2.2332749368948122e-12, 1e-10));
  EXPECT_THAT(small_v.TailIntegral(999.0),
              IsRelativelyNear(1.5013102006029483e-21, 1e-10));
  EXPECT_EQ(small_v.TailIntegral(1000.5), 0.0);

  // v * (1 + x / v)^(1 - q) ~ 8.7e-309 is subnormal while D(x) is not.
  EXPECT_THAT(ZipfParam(1e18, 1.01, 1e-305).TailIntegral(10.0),
              IsRelativelyNear(2.8211993460048987e-307, 1e-10));
}

TEST(ZipfParamTest, GeneralInversionRoundTripsWhenExpZmUnderflows) {
  // zm ~ -1823 < -708 and d < D(0) / 2: log(exp(zm) + b) is evaluated in log
  // space.
  const ZipfParam steep(1e18, 100.0, 1e10);
  EXPECT_THAT(ZipfParamRoundTrip(steep, 1e9), IsRelativelyNear(1e9, 1e-9));
  EXPECT_THAT(ZipfParamRoundTrip(steep, 1e12), IsRelativelyNear(1e12, 1e-9));
  // x / v ~ 1e315 overflows double (t = -z / (q - 1) ~ 725 > 709): VExpm1
  // combines log(v) + t to invert without premature expm1 overflow.
  const ZipfParam tiny_v_flat(1e18, 1.0 + 1e-15, 1e-305);
  EXPECT_THAT(ZipfParamRoundTrip(tiny_v_flat, 1e10),
              IsRelativelyNear(1e10, 1e-11));
}

TEST(ZipfParamTest, AcceptsHatSqueezeMissMatchesExactThreshold) {
  // Accepts iff d <= D(k + 0.5) + f(k), with f(k) = (1 + k / v)^(-q).
  // q == 2, v == 1, k == 1: (1 / 2.5 - 1 / 11.5) + 1 / 4.
  const ZipfParam quadratic(10.0, 2.0, 1.0);
  EXPECT_TRUE(
      quadratic.AcceptsHatSqueezeMiss(1.0, 0.5630434782608695 * (1.0 - 1e-9)));
  EXPECT_FALSE(
      quadratic.AcceptsHatSqueezeMiss(1.0, 0.5630434782608695 * (1.0 + 1e-9)));
  // k == max_kd: D(k + 0.5) == 0, leaving f(10) = 1 / 121.
  EXPECT_TRUE(quadratic.AcceptsHatSqueezeMiss(
      10.0, 0.008264462809917356 * (1.0 - 1e-9)));
  EXPECT_FALSE(quadratic.AcceptsHatSqueezeMiss(
      10.0, 0.008264462809917356 * (1.0 + 1e-9)));
  // q == 3, v == 0.5, k == 3: 0.25 * (1 / 64 - 1 / 484) + 1 / 343.
  const ZipfParam cubic(10.0, 3.0, 0.5);
  EXPECT_TRUE(
      cubic.AcceptsHatSqueezeMiss(3.0, 0.0063051729694238974 * (1.0 - 1e-9)));
  EXPECT_FALSE(
      cubic.AcceptsHatSqueezeMiss(3.0, 0.0063051729694238974 * (1.0 + 1e-9)));
}

TEST(ZipfParamTest, TailIntegralInvStaysWithinDomainForTinyD) {
  // Exercises all TailIntegralInv paths where rounding at d -> 0+ can
  // otherwise exceed xm = max_kd + 0.5 by a few ULPs.
  EXPECT_THAT(ZipfParam(10.0, 10.0, 100.0).TailIntegralInv(kDenormMin),
              AllOf(Ge(0.0), Le(10.5)));
  EXPECT_THAT(ZipfParam(100.0, 1.5, 0.1).TailIntegralInv(kDenormMin),
              AllOf(Ge(0.0), Le(100.5)));
  EXPECT_THAT(ZipfParam(1000.0, 2.0, 200.0).TailIntegralInv(kDenormMin),
              AllOf(Ge(0.0), Le(1000.5)));
  EXPECT_EQ(ZipfParam(1e12, 100.0, 1e4).TailIntegralInv(kDenormMin),
            1e12 + 0.5);
}

MATCHER(FailsLeavingUnchanged,
        "fails stream extraction and leaves target and stream flags "
        "unchanged") {
  return arg.failed && arg.flags_preserved && arg.value == arg.initial;
}

MATCHER_P(ParsesEqualTo, expected,
          "succeeds, updates target, and preserves stream flags") {
  return !arg.failed && arg.flags_preserved && arg.value == expected;
}

TEST(ZipfDistributionTest, StreamRejectsInvalidParameters) {
  using absl::random_internal::ParseFromStream;
  const absl::zipf_distribution<int> kIntSentinel(42, 3.0, 2.0);
  const absl::zipf_distribution<int8_t> kI8Sentinel(42, 3.0, 2.0);
  const absl::zipf_distribution<uint8_t> kU8Sentinel(42, 3.0, 2.0);
  const absl::zipf_distribution<uint32_t> kU32Sentinel(42, 3.0, 2.0);

  // Valid boundary values for 8-bit integer types and subnormal v.
  EXPECT_THAT(ParseFromStream(kI8Sentinel, "0 2.0 1.0"),
              ParsesEqualTo(absl::zipf_distribution<int8_t>(0, 2.0, 1.0)));
  EXPECT_THAT(ParseFromStream(kI8Sentinel, "127 2.0 1.0"),
              ParsesEqualTo(absl::zipf_distribution<int8_t>(127, 2.0, 1.0)));
  EXPECT_THAT(ParseFromStream(kU8Sentinel, "255 2.0 1.0"),
              ParsesEqualTo(absl::zipf_distribution<uint8_t>(255, 2.0, 1.0)));
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.5 1e-320"),
              ParsesEqualTo(absl::zipf_distribution<int>(10, 2.5, 1e-320)));
  // Extreme valid q and v (nextafter(1, 2) and DBL_MAX) round-trip cleanly.
  constexpr double kDblMax = (std::numeric_limits<double>::max)();
  const absl::zipf_distribution<int> kExtremeValid(10, std::nextafter(1.0, 2.0),
                                                   kDblMax);
  const absl::zipf_distribution<int> kMaxQValid(10, kDblMax, kDblMax);
  std::ostringstream extreme_os, max_q_os;
  extreme_os << kExtremeValid;
  max_q_os << kMaxQValid;
  EXPECT_THAT(ParseFromStream(kIntSentinel, extreme_os.str()),
              ParsesEqualTo(kExtremeValid));
  EXPECT_THAT(ParseFromStream(kIntSentinel, max_q_os.str()),
              ParsesEqualTo(kMaxQValid));

  // Out-of-range, negative, or overflowing k.
  EXPECT_THAT(ParseFromStream(kIntSentinel, "-5 2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI8Sentinel, "-5 2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Sentinel, "-5 2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU32Sentinel, "-5 2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Sentinel, "-0 2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU32Sentinel, "-0 2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI8Sentinel, "128 2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI8Sentinel, "300 2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Sentinel, "300 2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU32Sentinel, "5000000000 2.0 1.0"),
              FailsLeavingUnchanged());

  // Invalid q (must be finite and > 1.0).
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 1.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 0.5 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 -2.0 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 nan 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 inf 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 -inf 1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 1e500 1.0"),
              FailsLeavingUnchanged());

  // Invalid v (must be finite and > 0.0).
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0 0.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0 -1.0"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0 nan"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0 inf"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0 -inf"),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0 1e500"),
              FailsLeavingUnchanged());

  // Non-finite q and v delivered with goodbit by a custom num_get facet.
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 nan 1.0",
                              /*use_non_finite_facet=*/true),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 inf 1.0",
                              /*use_non_finite_facet=*/true),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 -inf 1.0",
                              /*use_non_finite_facet=*/true),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0 nan",
                              /*use_non_finite_facet=*/true),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0 inf",
                              /*use_non_finite_facet=*/true),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0 -inf",
                              /*use_non_finite_facet=*/true),
              FailsLeavingUnchanged());

  // Truncated and malformed streams.
  EXPECT_THAT(ParseFromStream(kIntSentinel, ""), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "10 2.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kIntSentinel, "abc 2.0 1.0"),
              FailsLeavingUnchanged());
}

}  // namespace
