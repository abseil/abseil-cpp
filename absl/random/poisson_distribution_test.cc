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

#include "absl/random/poisson_distribution.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"

// Notes about generating poisson variates:
//
// It is unlikely that any implementation of std::poisson_distribution
// will be stable over time and across library implementations. For instance
// the three different poisson variate generators listed below all differ:
//
// https://github.com/ampl/gsl/tree/master/randist/poisson.c
// * GSL uses a gamma + binomial + knuth method to compute poisson variates.
//
// https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/include/bits/random.tcc
// * GCC uses the Devroye rejection algorithm, based on
// Devroye, L. Non-Uniform Random Variates Generation. Springer-Verlag,
// New York, 1986, Ch. X, Sects. 3.3 & 3.4 (+ Errata!), ~p.511
//   http://www.nrbook.com/devroye/
//
// https://github.com/llvm-mirror/libcxx/blob/master/include/random
// * CLANG uses a different rejection method, which appears to include a
// normal-distribution approximation and an exponential distribution to
// compute the threshold, including a similar factorial approximation to this
// one, but it is unclear where the algorithm comes from, exactly.
//

namespace {

using ::absl::random_internal::kChiSquared;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::DoubleNear;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Ge;
using ::testing::Le;

// The PoissonDistributionInterfaceTest provides a basic test that
// absl::poisson_distribution conforms to the interface and serialization
// requirements imposed by [rand.req.dist] for the common integer types.

template <typename IntType>
class PoissonDistributionInterfaceTest : public ::testing::Test {};

using IntTypes =
    ::testing::Types<int, int8_t, int16_t, int32_t, int64_t, absl::int128,
                     uint8_t, uint16_t, uint32_t, uint64_t, absl::uint128>;
TYPED_TEST_SUITE(PoissonDistributionInterfaceTest, IntTypes);

TYPED_TEST(PoissonDistributionInterfaceTest, SerializeTest) {
  using param_type = typename absl::poisson_distribution<TypeParam>::param_type;
  const double kMax =
      std::min(1e15 /* assertion limit */,
               static_cast<double>(std::numeric_limits<TypeParam>::max()));

  const double kParams[] = {
      // Cases around 1.
      1,                         //
      std::nextafter(1.0, 0.0),  // 1 - epsilon
      std::nextafter(1.0, 2.0),  // 1 + epsilon
      // Arbitrary values.
      1e-8, 1e-4,
      0.0000005,  // ~7.2e-7
      0.2,        // ~0.2x
      0.5,        // 0.72
      2,          // ~2.8
      20,         // 3x ~9.6
      100, 1e4, 1e8, 1.5e9, 1e20,
      // Boundary cases.
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::epsilon(),
      std::nextafter(std::numeric_limits<double>::min(),
                     1.0),                        // min + epsilon
      std::numeric_limits<double>::min(),         // smallest normal
      std::numeric_limits<double>::denorm_min(),  // smallest denorm
      std::numeric_limits<double>::min() / 2,     // denorm
      std::nextafter(std::numeric_limits<double>::min(),
                     0.0),  // denorm_max
  };

  constexpr int kCount = 1000;
  absl::InsecureBitGen gen;
  for (const double m : kParams) {
    const double mean = std::min(kMax, m);
    const param_type param(mean);

    // Validate parameters.
    absl::poisson_distribution<TypeParam> before(mean);
    EXPECT_EQ(before.mean(), param.mean());

    {
      absl::poisson_distribution<TypeParam> via_param(param);
      EXPECT_EQ(via_param, before);
      EXPECT_EQ(via_param.param(), before.param());
    }

    // Smoke test.
    auto sample_min = before.max();
    auto sample_max = before.min();
    for (int i = 0; i < kCount; i++) {
      auto sample = before(gen);
      EXPECT_GE(sample, before.min());
      EXPECT_LE(sample, before.max());
      if (sample > sample_max) sample_max = sample;
      if (sample < sample_min) sample_min = sample;
    }

    LOG(INFO) << "Range {" << param.mean() << "}: " << sample_min << ", "
              << sample_max;

    // Validate stream serialization.
    std::stringstream ss;
    ss << before;

    absl::poisson_distribution<TypeParam> after(3.8);

    EXPECT_NE(before.mean(), after.mean());
    EXPECT_NE(before.param(), after.param());
    EXPECT_NE(before, after);

    ss >> after;

    EXPECT_EQ(before.mean(), after.mean())  //
        << ss.str() << " "                  //
        << (ss.good() ? "good " : "")       //
        << (ss.bad() ? "bad " : "")         //
        << (ss.eof() ? "eof " : "")         //
        << (ss.fail() ? "fail " : "");
  }
}

// See http://www.itl.nist.gov/div898/handbook/eda/section3/eda366j.htm

class PoissonModel {
 public:
  explicit PoissonModel(double mean) : mean_(mean) {}

  double mean() const { return mean_; }
  double variance() const { return mean_; }
  double stddev() const { return std::sqrt(variance()); }
  double skew() const { return 1.0 / mean_; }
  double kurtosis() const { return 3.0 + 1.0 / mean_; }

  // InitCDF() initializes the CDF for the distribution parameters.
  void InitCDF();

  // The InverseCDF, or the Percent-point function returns x, P(x) < v.
  struct CDF {
    size_t index;
    double pmf;
    double cdf;
  };
  CDF InverseCDF(double p) {
    CDF target{0, 0, p};
    auto it = std::upper_bound(
        std::begin(cdf_), std::end(cdf_), target,
        [](const CDF& a, const CDF& b) { return a.cdf < b.cdf; });
    return *it;
  }

  void LogCDF() {
    LOG(INFO) << "CDF (mean = " << mean_ << ")";
    for (const auto c : cdf_) {
      LOG(INFO) << c.index << ": pmf=" << c.pmf << " cdf=" << c.cdf;
    }
  }

 private:
  const double mean_;

  std::vector<CDF> cdf_;
};

// The goal is to compute an InverseCDF function, or percent point function for
// the poisson distribution, and use that to partition our output into equal
// range buckets.  However there is no closed form solution for the inverse cdf
// for poisson distributions (the closest is the incomplete gamma function).
// Instead, `InitCDF` iteratively computes the PMF and the CDF. This enables
// searching for the bucket points.
void PoissonModel::InitCDF() {
  if (!cdf_.empty()) {
    // State already initialized.
    return;
  }
  ABSL_ASSERT(mean_ < 201.0);

  const size_t max_i = 50 * stddev() + mean();
  const double e_neg_mean = std::exp(-mean());
  ABSL_ASSERT(e_neg_mean > 0);

  double d = 1;
  double last_result = e_neg_mean;
  double cumulative = e_neg_mean;
  if (e_neg_mean > 1e-10) {
    cdf_.push_back({0, e_neg_mean, cumulative});
  }
  for (size_t i = 1; i < max_i; i++) {
    d *= (mean() / i);
    double result = e_neg_mean * d;
    cumulative += result;
    if (result < 1e-10 && result < last_result && cumulative > 0.999999) {
      break;
    }
    if (result > 1e-7) {
      cdf_.push_back({i, result, cumulative});
    }
    last_result = result;
  }
  ABSL_ASSERT(!cdf_.empty());
}

// PoissonDistributionZTest implements a z-test for the poisson distribution.

struct ZParam {
  double mean;
  double p_fail;   // Z-Test probability of failure.
  int trials;      // Z-Test trials.
  size_t samples;  // Z-Test samples.
};

class PoissonDistributionZTest : public testing::TestWithParam<ZParam>,
                                 public PoissonModel {
 public:
  PoissonDistributionZTest() : PoissonModel(GetParam().mean) {}

  // ZTestImpl provides a basic z-squared test of the mean vs. expected
  // mean for data generated by the poisson distribution.
  template <typename D>
  bool SingleZTest(const double p, const size_t samples);

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{0x2B7E151628AED2A6};
};

template <typename D>
bool PoissonDistributionZTest::SingleZTest(const double p,
                                           const size_t samples) {
  D dis(mean());

  absl::flat_hash_map<int32_t, int> buckets;
  std::vector<double> data;
  data.reserve(samples);
  for (int j = 0; j < samples; j++) {
    const auto x = dis(rng_);
    buckets[x]++;
    data.push_back(x);
  }

  // The null-hypothesis is that the distribution is a poisson distribution with
  // the provided mean (not estimated from the data).
  const auto m = absl::random_internal::ComputeDistributionMoments(data);
  const double max_err = absl::random_internal::MaxErrorTolerance(p);
  const double z = absl::random_internal::ZScore(mean(), m);
  const bool pass = absl::random_internal::Near("z", z, 0.0, max_err);

  if (!pass) {
    // clang-format off
    LOG(INFO)
        << "p=" << p << " max_err=" << max_err << "\n"
           " mean=" << m.mean << " vs. " << mean() << "\n"
           " stddev=" << std::sqrt(m.variance) << " vs. " << stddev() << "\n"
           " skewness=" << m.skewness << " vs. " << skew() << "\n"
           " kurtosis=" << m.kurtosis << " vs. " << kurtosis() << "\n"
           " z=" << z;
    // clang-format on
  }
  return pass;
}

TEST_P(PoissonDistributionZTest, AbslPoissonDistribution) {
  const auto& param = GetParam();
  const int expected_failures =
      std::max(1, static_cast<int>(std::ceil(param.trials * param.p_fail)));
  const double p = absl::random_internal::RequiredSuccessProbability(
      param.p_fail, param.trials);

  int failures = 0;
  for (int i = 0; i < param.trials; i++) {
    failures +=
        SingleZTest<absl::poisson_distribution<int32_t>>(p, param.samples) ? 0
                                                                           : 1;
  }
  EXPECT_LE(failures, expected_failures);
}

std::vector<ZParam> GetZParams() {
  // These values have been adjusted from the "exact" computed values to reduce
  // failure rates.
  //
  // It turns out that the actual values are not as close to the expected values
  // as would be ideal.
  return std::vector<ZParam>({
      // Knuth method.
      ZParam{0.5, 0.01, 100, 1000},
      ZParam{1.0, 0.01, 100, 1000},
      ZParam{10.0, 0.01, 100, 5000},
      // Split-knuth method.
      ZParam{20.0, 0.01, 100, 10000},
      ZParam{50.0, 0.01, 100, 10000},
      // Ratio of gaussians method.
      ZParam{51.0, 0.01, 100, 10000},
      ZParam{200.0, 0.05, 10, 100000},
      ZParam{100000.0, 0.05, 10, 1000000},
  });
}

std::string ZParamName(const ::testing::TestParamInfo<ZParam>& info) {
  const auto& p = info.param;
  std::string name = absl::StrCat("mean_", absl::SixDigits(p.mean));
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(All, PoissonDistributionZTest,
                         ::testing::ValuesIn(GetZParams()), ZParamName);

// The PoissonDistributionChiSquaredTest class provides a basic test framework
// for variates generated by a conforming poisson_distribution.
class PoissonDistributionChiSquaredTest : public testing::TestWithParam<double>,
                                          public PoissonModel {
 public:
  PoissonDistributionChiSquaredTest() : PoissonModel(GetParam()) {}

  // The ChiSquaredTestImpl provides a chi-squared goodness of fit test for data
  // generated by the poisson distribution.
  template <typename D>
  double ChiSquaredTestImpl();

 private:
  void InitChiSquaredTest(const double buckets);

  std::vector<size_t> cutoffs_;
  std::vector<double> expected_;

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{0x2B7E151628AED2A6};
};

void PoissonDistributionChiSquaredTest::InitChiSquaredTest(
    const double buckets) {
  if (!cutoffs_.empty() && !expected_.empty()) {
    return;
  }
  InitCDF();

  // The code below finds cuttoffs that yield approximately equally-sized
  // buckets to the extent that it is possible. However for poisson
  // distributions this is particularly challenging for small mean parameters.
  // Track the expected proportion of items in each bucket.
  double last_cdf = 0;
  const double inc = 1.0 / buckets;
  for (double p = inc; p <= 1.0; p += inc) {
    auto result = InverseCDF(p);
    if (!cutoffs_.empty() && cutoffs_.back() == result.index) {
      continue;
    }
    double d = result.cdf - last_cdf;
    cutoffs_.push_back(result.index);
    expected_.push_back(d);
    last_cdf = result.cdf;
  }
  cutoffs_.push_back(std::numeric_limits<size_t>::max());
  expected_.push_back(std::max(0.0, 1.0 - last_cdf));
}

template <typename D>
double PoissonDistributionChiSquaredTest::ChiSquaredTestImpl() {
  const int kSamples = 2000;
  const int kBuckets = 50;

  // The poisson CDF fails for large mean values, since e^-mean exceeds the
  // machine precision. For these cases, using a normal approximation would be
  // appropriate.
  ABSL_ASSERT(mean() <= 200);
  InitChiSquaredTest(kBuckets);

  D dis(mean());

  std::vector<int32_t> counts(cutoffs_.size(), 0);
  for (int j = 0; j < kSamples; j++) {
    const size_t x = dis(rng_);
    auto it = std::lower_bound(std::begin(cutoffs_), std::end(cutoffs_), x);
    counts[std::distance(cutoffs_.begin(), it)]++;
  }

  // Normalize the counts.
  std::vector<int32_t> e(expected_.size(), 0);
  for (int i = 0; i < e.size(); i++) {
    e[i] = kSamples * expected_[i];
  }

  // The null-hypothesis is that the distribution is a poisson distribution with
  // the provided mean (not estimated from the data).
  const int dof = static_cast<int>(counts.size()) - 1;

  // The threshold for logging is 1-in-50.
  const double threshold = absl::random_internal::ChiSquareValue(dof, 0.98);

  const double chi_square = absl::random_internal::ChiSquare(
      std::begin(counts), std::end(counts), std::begin(e), std::end(e));

  const double p = absl::random_internal::ChiSquarePValue(chi_square, dof);

  // Log if the chi_squared value is above the threshold.
  if (chi_square > threshold) {
    LogCDF();

    LOG(INFO) << "VALUES  buckets=" << counts.size()
              << "  samples=" << kSamples;
    for (size_t i = 0; i < counts.size(); i++) {
      LOG(INFO) << cutoffs_[i] << ": " << counts[i] << " vs. E=" << e[i];
    }

    LOG(INFO) << kChiSquared << "(data, dof=" << dof << ") = " << chi_square
              << " (" << p << ")\n"
              << " vs.\n"
              << kChiSquared << " @ 0.98 = " << threshold;
  }
  return p;
}

TEST_P(PoissonDistributionChiSquaredTest, AbslPoissonDistribution) {
  const int kTrials = 20;

  // Large values are not yet supported -- this requires estimating the cdf
  // using the normal distribution instead of the poisson in this case.
  ASSERT_LE(mean(), 200.0);
  if (mean() > 200.0) {
    return;
  }

  int failures = 0;
  for (int i = 0; i < kTrials; i++) {
    double p_value = ChiSquaredTestImpl<absl::poisson_distribution<int32_t>>();
    if (p_value < 0.005) {
      failures++;
    }
  }
  // There is a 0.10% chance of producing at least one failure, so raise the
  // failure threshold high enough to allow for a flake rate < 10,000.
  EXPECT_LE(failures, 4);
}

INSTANTIATE_TEST_SUITE_P(All, PoissonDistributionChiSquaredTest,
                         ::testing::Values(0.5, 1.0, 2.0, 10.0, 50.0, 51.0,
                                           200.0));

// NOTE: absl::poisson_distribution is not guaranteed to be stable.
TEST(PoissonDistributionTest, StabilityTest) {
  // absl::poisson_distribution stability relies on stability of
  // std::exp, std::expm1, std::log, std::log1p, std::sqrt, std::ceil,
  // std::floor, FastUniformBits, GenerateRealFromBits, and
  // StirlingLogFactorialRemainder.
  absl::random_internal::sequence_urbg urbg({
      0x035b0dc7e0a18acfull, 0x06cebe0d2653682eull, 0x0061e9b23861596bull,
      0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
      0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
      0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
      0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull,
      0x4864f22c059bf29eull, 0x247856d8b862665cull, 0xe46e86e9a1337e10ull,
      0xd8c8541f3519b133ull, 0xe75b5162c567b9e4ull, 0xf732e5ded7009c5bull,
      0xb170b98353121eacull, 0x1ec2e8986d2362caull, 0x814c8e35fe9a961aull,
      0x0c3cd59c9b638a02ull, 0xcb3bb6478a07715cull, 0x1224e62c978bbc7full,
      0x671ef2cb04e81f6eull, 0x3c1cbd811eaf1808ull, 0x1bbc23cfa8fac721ull,
      0xa4c2cda65e596a51ull, 0xb77216fad37adf91ull, 0x836d794457c08849ull,
      0xe083df03475f49d7ull, 0xbc9feb512e6b0d6cull, 0xb12d74fdd718c8c5ull,
      0x12ff09653bfbe4caull, 0x8dd03a105bc4ee7eull, 0x5738341045ba0d85ull,
      0xf3fd722dc65ad09eull, 0xfa14fd21ea2a5705ull, 0xffe6ea4d6edb0c73ull,
      0xD07E9EFE2BF11FB4ull, 0x95DBDA4DAE909198ull, 0xEAAD8E716B93D5A0ull,
      0xD08ED1D0AFC725E0ull, 0x8E3C5B2F8E7594B7ull, 0x8FF6E2FBF2122B64ull,
      0x8888B812900DF01Cull, 0x4FAD5EA0688FC31Cull, 0xD1CFF191B3A8C1ADull,
      0x2F2F2218BE0E1777ull, 0xEA752DFE8B021FA1ull, 0xE5A0CC0FB56F74E8ull,
      0x18ACF3D6CE89E299ull, 0xB4A84FE0FD13E0B7ull, 0x7CC43B81D2ADA8D9ull,
      0x165FA26680957705ull, 0x93CC7314211A1477ull, 0xE6AD206577B5FA86ull,
      0xC75442F5FB9D35CFull, 0xEBCDAF0C7B3E89A0ull, 0xD6411BD3AE1E7E49ull,
      0x00250E2D2071B35Eull, 0x226800BB57B8E0AFull, 0x2464369BF009B91Eull,
      0x5563911D59DFA6AAull, 0x78C14389D95A537Full, 0x207D5BA202E5B9C5ull,
      0x832603766295CFA9ull, 0x11C819684E734A41ull, 0xB3472DCA7B14A94Aull,
  });

  std::vector<int> output(10);

  // Method 1.
  {
    absl::poisson_distribution<int> dist(5);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
  }
  EXPECT_THAT(output,  // mean = 4.2
              ElementsAre(1, 0, 0, 4, 2, 10, 3, 3, 7, 12));

  // Method 2.
  {
    urbg.reset();
    absl::poisson_distribution<int> dist(25);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
  }
  EXPECT_THAT(output,  // mean = 19.8
              ElementsAre(9, 35, 18, 10, 35, 18, 10, 35, 18, 10));

  // Method 3.
  {
    urbg.reset();
    absl::poisson_distribution<int> dist(121);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
  }
  EXPECT_THAT(output,  // mean = 124.1
              ElementsAre(161, 122, 129, 124, 112, 112, 117, 120, 130, 114));
}

TEST(PoissonDistributionTest, AlgorithmExpectedValue_1) {
  // This tests small values of the Knuth method.
  // The underlying uniform distribution will generate exactly 0.5.
  absl::random_internal::sequence_urbg urbg({0x8000000000000001ull});
  absl::poisson_distribution<int> dist(5);
  EXPECT_EQ(7, dist(urbg));
}

TEST(PoissonDistributionTest, AlgorithmExpectedValue_2) {
  // This tests larger values of the Knuth method.
  // The underlying uniform distribution will generate exactly 0.5.
  absl::random_internal::sequence_urbg urbg({0x8000000000000001ull});
  absl::poisson_distribution<int> dist(25);
  EXPECT_EQ(36, dist(urbg));
}

TEST(PoissonDistributionTest, AlgorithmExpectedValue_3) {
  // This variant uses the ratio of uniforms method.
  absl::random_internal::sequence_urbg urbg(
      {0x7fffffffffffffffull, 0x8000000000000000ull});

  absl::poisson_distribution<int> dist(121);
  EXPECT_EQ(121, dist(urbg));
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

TEST(PoissonDistributionTest, StreamRejectsInvalidParameters) {
  using absl::random_internal::ParseFromStream;
  const absl::poisson_distribution<int32_t> kInitial(4.5);
  const absl::poisson_distribution<int8_t> kI8Initial(4.5);
  const absl::poisson_distribution<uint8_t> kU8Initial(4.5);
  const absl::poisson_distribution<int64_t> kI64Initial(4.5);
  const absl::poisson_distribution<absl::int128> kI128Initial(4.5);
  const absl::poisson_distribution<absl::uint128> kU128Initial(4.5);

  EXPECT_THAT(ParseFromStream(kInitial, ""), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "abc"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "nan"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "inf"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-inf"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "nan", /*use_non_finite_facet=*/true),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "inf", /*use_non_finite_facet=*/true),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-inf", /*use_non_finite_facet=*/true),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "1e20"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "1e400"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "0.0"),
              ParsesEqualTo(absl::poisson_distribution<int32_t>(0.0)));
  EXPECT_THAT(ParseFromStream(kInitial, "1e-320"),
              ParsesEqualTo(absl::poisson_distribution<int32_t>(1e-320)));

  EXPECT_THAT(ParseFromStream(kI8Initial, "128.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI8Initial, "200.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI8Initial, "127.0"),
              ParsesEqualTo(absl::poisson_distribution<int8_t>(127.0)));

  EXPECT_THAT(ParseFromStream(kU8Initial, "256.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Initial, "300.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kU8Initial, "255.0"),
              ParsesEqualTo(absl::poisson_distribution<uint8_t>(255.0)));

  constexpr double kMaxMean = absl::random_internal::kPoissonMaxMean;
  EXPECT_THAT(ParseFromStream(kI64Initial, absl::StrCat(kMaxMean)),
              ParsesEqualTo(absl::poisson_distribution<int64_t>(kMaxMean)));
  EXPECT_THAT(ParseFromStream(kI64Initial, absl::StrCat(kMaxMean * 10.0)),
              FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kI64Initial, "1e20"), FailsLeavingUnchanged());

  EXPECT_THAT(
      ParseFromStream(kI128Initial, absl::StrCat(kMaxMean)),
      ParsesEqualTo(absl::poisson_distribution<absl::int128>(kMaxMean)));
  EXPECT_THAT(ParseFromStream(kI128Initial, "1e20"), FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kU128Initial, absl::StrCat(kMaxMean)),
      ParsesEqualTo(absl::poisson_distribution<absl::uint128>(kMaxMean)));
  EXPECT_THAT(ParseFromStream(kU128Initial, "1e20"), FailsLeavingUnchanged());
}

// Fixed seed for deterministic sampling tests (fractional hex digits of Euler's
// number e, matching PoissonDistributionZTest above).
constexpr uint64_t kTestSeed = 0x2B7E151628AED2A6ULL;

template <typename IntType>
IntType SamplePoissonWithBits(double mean,
                              std::initializer_list<uint64_t> seq) {
  absl::random_internal::sequence_urbg urbg(seq);
  absl::poisson_distribution<IntType> dist(mean);
  return dist(urbg);
}

template <typename IntType>
std::vector<IntType> DrawPoissonSamples(double mean, size_t count) {
  absl::random_internal::pcg64_2018_engine rng(kTestSeed);
  absl::poisson_distribution<IntType> dist(mean);
  std::vector<IntType> samples(count);
  std::generate(samples.begin(), samples.end(), [&] { return dist(rng); });
  return samples;
}

template <typename IntType>
struct PoissonSampleSummary {
  IntType min_sample;
  IntType max_sample;
  size_t nonzero_count;
  double mean;
};

template <typename IntType>
void PrintTo(const PoissonSampleSummary<IntType>& s, std::ostream* os) {
  *os << "{min_sample=" << +s.min_sample << ", max_sample=" << +s.max_sample
      << ", nonzero_count=" << s.nonzero_count << ", mean=" << s.mean << "}";
}

template <typename IntType>
PoissonSampleSummary<IntType> SummarizePoissonSamples(
    double mean, size_t count, uint64_t seed = kTestSeed) {
  absl::random_internal::pcg64_2018_engine rng(seed);
  absl::poisson_distribution<IntType> dist(mean);
  IntType min_s = dist.max();
  IntType max_s = dist.min();
  size_t nonzero = 0;
  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const IntType x = dist(rng);
    min_s = (std::min)(min_s, x);
    max_s = (std::max)(max_s, x);
    if (x > 0) {
      ++nonzero;
    }
    sum += static_cast<double>(x);
  }
  return {min_s, max_s, nonzero, sum / static_cast<double>(count)};
}

MATCHER_P2(HasPositiveNonZeroAndMeanNear, expected_mean, tol, "") {
  using SampleType = decltype(arg.min_sample);
  if constexpr (std::numeric_limits<SampleType>::is_signed) {
    if (arg.min_sample < SampleType{0}) return false;
  }
  return arg.nonzero_count > 0 && std::abs(arg.mean - expected_mean) <= tol;
}

struct PoissonDrawResult {
  int64_t sample;
  size_t rng_draws;
};

void PrintTo(const PoissonDrawResult& r, std::ostream* os) {
  *os << "{sample=" << r.sample << ", rng_draws=" << r.rng_draws << "}";
}

PoissonDrawResult DrawLargeMeanSequence(double mean,
                                        std::initializer_list<uint64_t> seq) {
  absl::random_internal::sequence_urbg urbg(seq);
  absl::poisson_distribution<int64_t> dist(mean);
  const int64_t sample = dist(urbg);
  return {sample, urbg.invocations()};
}

MATCHER_P2(ProducesSampleInDraws, expected_sample, expected_draws, "") {
  return arg.sample == expected_sample &&
         arg.rng_draws == static_cast<size_t>(expected_draws);
}

struct OffsetMomentsSummary {
  double abs_z_score;
  double variance;
};

void PrintTo(const OffsetMomentsSummary& s, std::ostream* os) {
  *os << "{abs_z_score=" << s.abs_z_score << ", variance=" << s.variance << "}";
}

OffsetMomentsSummary SummarizeLargeMeanOffsets(double mean,
                                               uint64_t seed = kTestSeed) {
  constexpr size_t kSamples = 50000;
  absl::random_internal::pcg64_2018_engine rng(seed);
  absl::poisson_distribution<int64_t> dist(mean);
  std::vector<double> offsets(kSamples);
  for (size_t i = 0; i < kSamples; ++i) {
    offsets[i] = static_cast<double>(dist(rng)) - mean;
  }
  const auto m = absl::random_internal::ComputeDistributionMoments(offsets);
  return {std::abs(absl::random_internal::ZScore(0.0, m)), m.variance};
}

MATCHER_P2(HasUnbiasedMomentsWithVarianceNear, expected_var, rel_tol, "") {
  return arg.abs_z_score < 4.0 &&
         std::abs(arg.variance - expected_var) <= expected_var * rel_tol;
}

double EvalPoissonLogRatio(double x, double mean) {
  const double mode = std::ceil(mean) - 1.0;
  const double g_mode =
      absl::random_internal::PoissonLogRatioTerm(mode, mode - mean, mean);
  return absl::random_internal::PoissonLogRatio(x, x - mean, mean,
                                                std::log(mean), g_mode);
}

double ExactLgammaLogRatio(double x, double mean) {
  const double mode = std::ceil(mean) - 1.0;
  return (x - mode) * std::log(mean) -
         (std::lgamma(x + 1.0) - std::lgamma(mode + 1.0));
}

class RepeatMaxThenZeroUrbg {
 public:
  using result_type = uint64_t;
  static constexpr result_type(min)() { return 0; }
  static constexpr result_type(max)() { return ~uint64_t{0}; }
  explicit RepeatMaxThenZeroUrbg(size_t max_draws) : remaining_(max_draws) {}
  result_type operator()() {
    if (remaining_ == 0) return 0;
    --remaining_;
    return ~uint64_t{0};
  }

 private:
  size_t remaining_;
};

template <typename IntType>
IntType SamplePoissonAfterRepeatedMaxBits(double mean, size_t max_bit_draws) {
  RepeatMaxThenZeroUrbg urbg(max_bit_draws);
  absl::poisson_distribution<IntType> dist(mean);
  return dist(urbg);
}

TEST(PoissonDistributionTest, Int8KnuthClampsAtMax) {
  // All-ones bits yield U = 1 - 2^-53, driving Knuth accumulation to int8 max.
  EXPECT_EQ(SamplePoissonWithBits<int8_t>(50.0, {~uint64_t{0}}),
            (std::numeric_limits<int8_t>::max)());
}

TEST(PoissonDistributionTest, Uint8KnuthClampsAtMax) {
  // All-ones bits yield U = 1 - 2^-53, driving Knuth accumulation to uint8 max.
  EXPECT_EQ(SamplePoissonWithBits<uint8_t>(50.0, {~uint64_t{0}}),
            (std::numeric_limits<uint8_t>::max)());
}

TEST(PoissonDistributionTest, Int8KnuthBoundaryDoesNotOverflow) {
  // On pre-CL code, 127 max-bit draws followed by 0ULL at mean = 5.0
  // incremented int8_t past 127 (signed overflow UB) before decrementing.
  EXPECT_EQ(SamplePoissonAfterRepeatedMaxBits<int8_t>(5.0, 126), int8_t{126});
  EXPECT_EQ(SamplePoissonAfterRepeatedMaxBits<int8_t>(5.0, 127), int8_t{127});
  EXPECT_EQ(SamplePoissonAfterRepeatedMaxBits<int8_t>(5.0, 128), int8_t{127});
}

TEST(PoissonDistributionTest, Uint8KnuthBoundaryDoesNotWrap) {
  // On pre-CL code, 256 max-bit draws followed by 0ULL at mean = 5.0 wrapped
  // uint8_t to 0 instead of clamping at 255.
  EXPECT_EQ(SamplePoissonAfterRepeatedMaxBits<uint8_t>(5.0, 254), uint8_t{254});
  EXPECT_EQ(SamplePoissonAfterRepeatedMaxBits<uint8_t>(5.0, 255), uint8_t{255});
  EXPECT_EQ(SamplePoissonAfterRepeatedMaxBits<uint8_t>(5.0, 256), uint8_t{255});
}

TEST(PoissonDistributionTest, Int8RatioOfUniformsNearMaxStaysInBounds) {
  EXPECT_THAT(DrawPoissonSamples<int8_t>(127.0, 2000),
              AllOf(Each(Ge(int8_t{60})), Contains(int8_t{127})));
}

TEST(PoissonDistributionTest, Uint8RatioOfUniformsNearMaxStaysInBounds) {
  EXPECT_THAT(DrawPoissonSamples<uint8_t>(255.0, 2000),
              AllOf(Each(Ge(uint8_t{150})), Contains(uint8_t{255})));
}

TEST(PoissonDistributionTest, Int128AndUint128TinyMeanSampling) {
  EXPECT_EQ(SamplePoissonWithBits<absl::int128>(1e-20, {0ULL}),
            absl::int128{1});
  EXPECT_EQ(SamplePoissonWithBits<absl::int128>(1e-20, {~uint64_t{0}}),
            absl::int128{0});
  EXPECT_EQ(SamplePoissonWithBits<absl::uint128>(1e-20, {0ULL}),
            absl::uint128{1});
  EXPECT_EQ(SamplePoissonWithBits<absl::uint128>(1e-20, {~uint64_t{0}}),
            absl::uint128{0});
}

TEST(PoissonDistributionTest, Int128AndUint128KnuthSampling) {
  EXPECT_EQ(SamplePoissonWithBits<absl::int128>(5.0, {0x8000000000000001ULL}),
            absl::int128{7});
  EXPECT_EQ(SamplePoissonWithBits<absl::int128>(25.0, {0x8000000000000001ULL}),
            absl::int128{36});
  EXPECT_EQ(SamplePoissonWithBits<absl::uint128>(5.0, {0x8000000000000001ULL}),
            absl::uint128{7});
  EXPECT_EQ(SamplePoissonWithBits<absl::uint128>(25.0, {0x8000000000000001ULL}),
            absl::uint128{36});
}

TEST(PoissonDistributionTest, Int128AndUint128LargeMeanSampling) {
  constexpr uint64_t kUMax = ~uint64_t{0};
  EXPECT_EQ(SamplePoissonWithBits<absl::int128>(1e15, {kUMax, 0ULL}),
            absl::int128{1'000'000'000'000'000LL});
  EXPECT_EQ(SamplePoissonWithBits<absl::uint128>(1e15, {kUMax, 0ULL}),
            absl::uint128{1'000'000'000'000'000ULL});

  constexpr absl::int128 kI128Lo = absl::int128{999'000'000'000'000LL};
  constexpr absl::int128 kI128Hi = absl::int128{1'001'000'000'000'000LL};
  EXPECT_THAT(DrawPoissonSamples<absl::int128>(1e15, 1000),
              Each(AllOf(Ge(kI128Lo), Le(kI128Hi))));

  constexpr absl::uint128 kU128Lo = absl::uint128{999'000'000'000'000ULL};
  constexpr absl::uint128 kU128Hi = absl::uint128{1'001'000'000'000'000ULL};
  EXPECT_THAT(DrawPoissonSamples<absl::uint128>(1e15, 1000),
              Each(AllOf(Ge(kU128Lo), Le(kU128Hi))));
}

TEST(PoissonDistributionTest, ZeroMeanAlwaysReturnsZero) {
  EXPECT_EQ(SamplePoissonWithBits<int>(0.0, {0ULL}), 0);
  EXPECT_THAT(DrawPoissonSamples<uint64_t>(0.0, 1000), Each(uint64_t{0}));
}

TEST(PoissonDistributionTest, Uint64SingleStepKnuthMeanIsAccurate) {
  EXPECT_THAT(SummarizePoissonSamples<uint64_t>(1.0, 20000),
              HasPositiveNonZeroAndMeanNear(1.0, 0.05));
  EXPECT_THAT(SummarizePoissonSamples<uint64_t>(5.0, 20000),
              HasPositiveNonZeroAndMeanNear(5.0, 0.1));
}

TEST(PoissonDistributionTest, Uint64SplitKnuthMeanIsAccurate) {
  EXPECT_THAT(SummarizePoissonSamples<uint64_t>(20.0, 20000),
              HasPositiveNonZeroAndMeanNear(20.0, 0.2));
  EXPECT_THAT(SummarizePoissonSamples<uint64_t>(50.0, 20000),
              HasPositiveNonZeroAndMeanNear(50.0, 0.3));
}

TEST(PoissonDistributionTest, TinyMeanZeroBitsReturnsOne) {
  EXPECT_EQ(SamplePoissonWithBits<int>(1e-18, {0ULL}), 1);
  EXPECT_EQ(SamplePoissonWithBits<uint64_t>(1e-20, {0ULL}), uint64_t{1});
}

TEST(PoissonDistributionTest, SubnormalMeanZeroBitsReturnsOne) {
  EXPECT_EQ(SamplePoissonWithBits<int64_t>(
                std::numeric_limits<double>::denorm_min(), {0ULL}),
            int64_t{1});
}

TEST(PoissonDistributionTest, TinyMeanMaxBitsReturnsZero) {
  EXPECT_EQ(SamplePoissonWithBits<uint64_t>(1e-20, {~uint64_t{0}}),
            uint64_t{0});
}

TEST(PoissonDistributionTest, TinyMeanCanSampleCountTwo) {
  // Arrival-time thinning reaches X >= 2 rather than truncating to {0, 1}.
  EXPECT_EQ(SamplePoissonWithBits<uint64_t>(
                1e-20, {0ULL, 0ULL, 0ULL, 0x8000000000000000ULL, 0ULL, 0ULL,
                        0ULL, 0ULL}),
            uint64_t{2});
}

TEST(PoissonDistributionTest, TinyMeanStatisticalRateMatchesMean) {
  constexpr double kTinyMean = 9e-5;
  constexpr size_t kSamples = 200000;

  EXPECT_THAT(SummarizePoissonSamples<int>(kTinyMean, kSamples),
              HasPositiveNonZeroAndMeanNear(kTinyMean, 4.5e-5));
}

TEST(PoissonDistributionTest, LargeIntegerMeanAcceptsUpperAndLowerModes) {
  constexpr uint64_t kUMax = ~uint64_t{0};
  constexpr uint64_t kNegPow2Minus17 =
      (uint64_t{1} << 63) | (uint64_t{1} << (63 - 17));

  // Integer means have dual modes at mean and mean - 1, both accepted at
  // u = 1 - 2^-53 on the first proposal (2 URBG draws).
  EXPECT_THAT(DrawLargeMeanSequence(1e10, {kUMax, 0ULL}),
              ProducesSampleInDraws(10'000'000'000LL, 2));
  EXPECT_THAT(DrawLargeMeanSequence(1e10, {kUMax, kNegPow2Minus17}),
              ProducesSampleInDraws(9'999'999'999LL, 2));
  EXPECT_THAT(DrawLargeMeanSequence(1e12, {kUMax, 0ULL}),
              ProducesSampleInDraws(1'000'000'000'000LL, 2));
  EXPECT_THAT(DrawLargeMeanSequence(1e15, {kUMax, 0ULL}),
              ProducesSampleInDraws(1'000'000'000'000'000LL, 2));
}

TEST(PoissonDistributionTest, FractionalLargeMeanAcceptsModeOnFirstProposal) {
  constexpr uint64_t kUMax = ~uint64_t{0};
  constexpr uint64_t kNegPow2Minus25 =
      (uint64_t{1} << 63) | (uint64_t{1} << (63 - 25));
  const double kMaxFracMean = std::nextafter(1e15, 0.0);

  EXPECT_THAT(DrawLargeMeanSequence(51.5, {kUMax, kNegPow2Minus25}),
              ProducesSampleInDraws(51, 2));
  EXPECT_THAT(DrawLargeMeanSequence(1e10 + 0.375, {kUMax, 0ULL}),
              ProducesSampleInDraws(10'000'000'000LL, 2));
  EXPECT_THAT(DrawLargeMeanSequence(kMaxFracMean, {kUMax, kNegPow2Minus25}),
              ProducesSampleInDraws(999'999'999'999'999LL, 2));
}

TEST(PoissonDistributionTest, FractionalLargeMeanRejectsOffModeAtMaxUniform) {
  constexpr uint64_t kUMax = ~uint64_t{0};
  constexpr uint64_t kNegPow2Minus25 =
      (uint64_t{1} << 63) | (uint64_t{1} << (63 - 25));
  constexpr uint64_t kNegPow2Minus2 =
      (uint64_t{1} << 63) | (uint64_t{1} << (63 - 2));

  // For mean = 51.5 (unique mode 51), off-mode proposals dx = +1 (0ULL) and
  // dx = -1 (-0.25) reject at u = 1 - 2^-53 before accepting dx = 0 on the
  // second iteration (4 URBG draws).
  EXPECT_THAT(
      DrawLargeMeanSequence(51.5, {kUMax, 0ULL, kUMax, kNegPow2Minus25}),
      ProducesSampleInDraws(51, 4));
  EXPECT_THAT(DrawLargeMeanSequence(
                  51.5, {kUMax, kNegPow2Minus2, kUMax, kNegPow2Minus25}),
              ProducesSampleInDraws(51, 4));
}

class PoissonLargeMeanMomentsTest : public ::testing::TestWithParam<double> {};

TEST_P(PoissonLargeMeanMomentsTest, OffsetMomentsAreUnbiased) {
  const double mean = GetParam();
  EXPECT_THAT(SummarizeLargeMeanOffsets(mean),
              HasUnbiasedMomentsWithVarianceNear(mean, 0.025));
}

INSTANTIATE_TEST_SUITE_P(All, PoissonLargeMeanMomentsTest,
                         ::testing::Values(51.5, 1e10, 1e10 + 0.375, 1e12, 1e15,
                                           std::nextafter(1e15, 0.0)));

TEST(PoissonDistributionTest, LogRatioMatchesLgammaAcrossTableBoundary) {
  for (const double mean : {51.0, 51.5}) {
    EXPECT_THAT(EvalPoissonLogRatio(0.0, mean),
                DoubleNear(ExactLgammaLogRatio(0.0, mean), 1e-12));
    EXPECT_THAT(EvalPoissonLogRatio(1.0, mean),
                DoubleNear(ExactLgammaLogRatio(1.0, mean), 1e-12));
    EXPECT_THAT(EvalPoissonLogRatio(19.0, mean),
                DoubleNear(ExactLgammaLogRatio(19.0, mean), 1e-12));
    EXPECT_THAT(EvalPoissonLogRatio(20.0, mean),
                DoubleNear(ExactLgammaLogRatio(20.0, mean), 1e-12));
  }
}

TEST(PoissonDistributionTest, LogRatioIsZeroAtBothModesForLargeIntegerMean) {
  EXPECT_THAT(EvalPoissonLogRatio(1e10 - 1.0, 1e10), DoubleNear(0.0, 1e-14));
  EXPECT_THAT(EvalPoissonLogRatio(1e10, 1e10), DoubleNear(0.0, 1e-14));
  EXPECT_THAT(EvalPoissonLogRatio(1e12, 1e12), DoubleNear(0.0, 1e-14));
  EXPECT_THAT(EvalPoissonLogRatio(1e15, 1e15), DoubleNear(0.0, 1e-14));
}

TEST(PoissonDistributionTest,
     LogRatioMatchesAdjacentStepsAtModerateFractionalMean) {
  EXPECT_THAT(EvalPoissonLogRatio(50.0, 51.5),
              DoubleNear(std::log(51.0 / 51.5), 1e-14));
  EXPECT_THAT(EvalPoissonLogRatio(51.0, 51.5), DoubleNear(0.0, 1e-14));
  EXPECT_THAT(EvalPoissonLogRatio(52.0, 51.5),
              DoubleNear(std::log(51.5 / 52.0), 1e-14));
}

TEST(PoissonDistributionTest,
     LogRatioMatchesAdjacentStepsAtLargeFractionalMean) {
  constexpr double kFracMean1e10 = 1e10 + 0.375;
  const double kMaxFracMean = std::nextafter(1e15, 0.0);
  const double kMaxFracMode = std::ceil(kMaxFracMean) - 1.0;
  const double kMaxFracPart = kMaxFracMean - kMaxFracMode;

  EXPECT_THAT(EvalPoissonLogRatio(1e10 - 1.0, kFracMean1e10),
              DoubleNear(std::log1p(-0.375 / kFracMean1e10), 1e-14));
  EXPECT_THAT(EvalPoissonLogRatio(1e10, kFracMean1e10), DoubleNear(0.0, 1e-14));
  EXPECT_THAT(EvalPoissonLogRatio(1e10 + 1.0, kFracMean1e10),
              DoubleNear(std::log1p(-0.625 / (1e10 + 1.0)), 1e-14));
  EXPECT_THAT(EvalPoissonLogRatio(kMaxFracMode - 1.0, kMaxFracMean),
              DoubleNear(std::log1p(-kMaxFracPart / kMaxFracMean), 1e-17));
  EXPECT_THAT(EvalPoissonLogRatio(kMaxFracMode, kMaxFracMean),
              DoubleNear(0.0, 1e-17));
  EXPECT_THAT(
      EvalPoissonLogRatio(kMaxFracMode + 1.0, kMaxFracMean),
      DoubleNear(std::log1p((kMaxFracPart - 1.0) / (kMaxFracMode + 1.0)),
                 1e-17));
}

}  // namespace
