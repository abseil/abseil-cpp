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

#include "absl/random/gaussian_distribution.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ios>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/log/log.h"
#include "absl/numeric/internal/representation.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

class GaussianDistributionTestPeer {
 public:
  static constexpr double kR = gaussian_distribution_base::kR;
  static constexpr double kRInv = gaussian_distribution_base::kRInv;
  static constexpr double kV = gaussian_distribution_base::kV;
  static constexpr size_t kNumStrips = gaussian_distribution_base::kMask + 1;

  static double x(size_t i) { return gaussian_distribution_base::zg_.x[i]; }
  static double f(size_t i) { return gaussian_distribution_base::zg_.f[i]; }
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

namespace {

using absl::random_internal::GaussianDistributionTestPeer;
using absl::random_internal::kChiSquared;

MATCHER(IsPositiveZero, "is +0.0") { return arg == 0 && !std::signbit(arg); }

template <typename RealType>
class GaussianDistributionInterfaceTest : public ::testing::Test {};

// double-double arithmetic is not supported well by either GCC or Clang; see
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99048,
// https://bugs.llvm.org/show_bug.cgi?id=49131, and
// https://bugs.llvm.org/show_bug.cgi?id=49132. Don't bother running these tests
// with double doubles until compiler support is better.
using RealTypes =
    std::conditional_t<absl::numeric_internal::IsDoubleDouble(),
                       ::testing::Types<float, double>,
                       ::testing::Types<float, double, long double>>;
TYPED_TEST_SUITE(GaussianDistributionInterfaceTest, RealTypes);

TYPED_TEST(GaussianDistributionInterfaceTest, SerializeTest) {
  using param_type =
      typename absl::gaussian_distribution<TypeParam>::param_type;

  const TypeParam kParams[] = {
      // Cases around 1.
      1,                                           //
      std::nextafter(TypeParam(1), TypeParam(0)),  // 1 - epsilon
      std::nextafter(TypeParam(1), TypeParam(2)),  // 1 + epsilon
      // Arbitrary values.
      TypeParam(1e-8), TypeParam(1e-4), TypeParam(2), TypeParam(1e4),
      TypeParam(1e8), TypeParam(1e20), TypeParam(2.5),
      // Boundary cases.
      std::numeric_limits<TypeParam>::infinity(),
      std::numeric_limits<TypeParam>::max(),
      std::numeric_limits<TypeParam>::epsilon(),
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(1)),           // min + epsilon
      std::numeric_limits<TypeParam>::min(),  // smallest normal
      // There are some errors dealing with denorms on apple platforms.
      std::numeric_limits<TypeParam>::denorm_min(),  // smallest denorm
      std::numeric_limits<TypeParam>::min() / 2,
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(0)),  // denorm_max
  };

  constexpr int kCount = 1000;
  absl::InsecureBitGen gen;

  // Use a loop to generate the combinations of {+/-x, +/-y}, and assign x, y to
  // all values in kParams,
  for (const auto mod : {0, 1, 2, 3}) {
    for (const auto x : kParams) {
      if (!std::isfinite(x)) continue;
      for (const auto y : kParams) {
        const TypeParam mean = (mod & 0x1) ? -x : x;
        const TypeParam stddev = (mod & 0x2) ? -y : y;
        const param_type param(mean, stddev);

        absl::gaussian_distribution<TypeParam> before(mean, stddev);
        EXPECT_EQ(before.mean(), param.mean());
        EXPECT_EQ(before.stddev(), param.stddev());

        {
          absl::gaussian_distribution<TypeParam> via_param(param);
          EXPECT_EQ(via_param, before);
          EXPECT_EQ(via_param.param(), before.param());
        }

        // Smoke test.
        auto sample_min = before.max();
        auto sample_max = before.min();
        for (int i = 0; i < kCount; i++) {
          auto sample = before(gen);
          if (sample > sample_max) sample_max = sample;
          if (sample < sample_min) sample_min = sample;
          EXPECT_GE(sample, before.min()) << before;
          EXPECT_LE(sample, before.max()) << before;
        }
        if (!std::is_same_v<TypeParam, long double>) {
          LOG(INFO) << "Range{" << mean << ", " << stddev << "}: " << sample_min
                    << ", " << sample_max;
        }

        std::stringstream ss;
        ss << before;

        if (!std::isfinite(mean) || !std::isfinite(stddev)) {
          // Streams do not parse inf/nan.
          continue;
        }

        // Validate stream serialization.
        absl::gaussian_distribution<TypeParam> after(-0.53f, 2.3456f);

        EXPECT_NE(before.mean(), after.mean());
        EXPECT_NE(before.stddev(), after.stddev());
        EXPECT_NE(before.param(), after.param());
        EXPECT_NE(before, after);

        ss >> after;

        EXPECT_EQ(before.mean(), after.mean());
        EXPECT_EQ(before.stddev(), after.stddev())  //
            << ss.str() << " "                      //
            << (ss.good() ? "good " : "")           //
            << (ss.bad() ? "bad " : "")             //
            << (ss.eof() ? "eof " : "")             //
            << (ss.fail() ? "fail " : "");
      }
    }
  }
}

// http://www.itl.nist.gov/div898/handbook/eda/section3/eda3661.htm

class GaussianModel {
 public:
  GaussianModel(double mean, double stddev) : mean_(mean), stddev_(stddev) {}

  double mean() const { return mean_; }
  double variance() const { return stddev() * stddev(); }
  double stddev() const { return stddev_; }
  double skew() const { return 0; }
  double kurtosis() const { return 3.0; }

  // The inverse CDF, or PercentPoint function.
  double InverseCDF(double p) {
    ABSL_ASSERT(p >= 0.0);
    ABSL_ASSERT(p < 1.0);
    return mean() + stddev() * -absl::random_internal::InverseNormalSurvival(p);
  }

 private:
  const double mean_;
  const double stddev_;
};

struct Param {
  double mean;
  double stddev;
  double p_fail;  // Z-Test probability of failure.
  int trials;     // Z-Test trials.
};

// GaussianDistributionTests implements a z-test for the gaussian
// distribution.
class GaussianDistributionTests : public testing::TestWithParam<Param>,
                                  public GaussianModel {
 public:
  GaussianDistributionTests()
      : GaussianModel(GetParam().mean, GetParam().stddev) {}

  // SingleZTest provides a basic z-squared test of the mean vs. expected
  // mean for data generated by the poisson distribution.
  template <typename D>
  bool SingleZTest(const double p, const size_t samples);

  // SingleChiSquaredTest provides a basic chi-squared test of the normal
  // distribution.
  template <typename D>
  double SingleChiSquaredTest();

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{0x2B7E151628AED2A6};
};

template <typename D>
bool GaussianDistributionTests::SingleZTest(const double p,
                                            const size_t samples) {
  D dis(mean(), stddev());

  std::vector<double> data;
  data.reserve(samples);
  for (size_t i = 0; i < samples; i++) {
    const double x = dis(rng_);
    data.push_back(x);
  }

  const double max_err = absl::random_internal::MaxErrorTolerance(p);
  const auto m = absl::random_internal::ComputeDistributionMoments(data);
  const double z = absl::random_internal::ZScore(mean(), m);
  const bool pass = absl::random_internal::Near("z", z, 0.0, max_err);

  // NOTE: Informational statistical test:
  //
  // Compute the Jarque-Bera test statistic given the excess skewness
  // and kurtosis. The statistic is drawn from a chi-square(2) distribution.
  // https://en.wikipedia.org/wiki/Jarque%E2%80%93Bera_test
  //
  // The null-hypothesis (normal distribution) is rejected when
  // (p = 0.05 => jb > 5.99)
  // (p = 0.01 => jb > 9.21)
  // NOTE: JB has a large type-I error rate, so it will reject the
  // null-hypothesis even when it is true more often than the z-test.
  //
  const double jb =
      static_cast<double>(m.n) / 6.0 *
      (std::pow(m.skewness, 2.0) + std::pow(m.kurtosis - 3.0, 2.0) / 4.0);

  if (!pass || jb > 9.21) {
    // clang-format off
    LOG(INFO)
        << "p=" << p << " max_err=" << max_err << "\n"
           " mean=" << m.mean << " vs. " << mean() << "\n"
           " stddev=" << std::sqrt(m.variance) << " vs. " << stddev() << "\n"
           " skewness=" << m.skewness << " vs. " << skew() << "\n"
           " kurtosis=" << m.kurtosis << " vs. " << kurtosis() << "\n"
           " z=" << z << " vs. 0\n"
           " jb=" << jb << " vs. 9.21";
    // clang-format on
  }
  return pass;
}

template <typename D>
double GaussianDistributionTests::SingleChiSquaredTest() {
  const size_t kSamples = 10000;
  const int kBuckets = 50;

  // The InverseCDF is the percent point function of the
  // distribution, and can be used to assign buckets
  // roughly uniformly.
  std::vector<double> cutoffs;
  const double kInc = 1.0 / static_cast<double>(kBuckets);
  for (double p = kInc; p < 1.0; p += kInc) {
    cutoffs.push_back(InverseCDF(p));
  }
  if (cutoffs.back() != std::numeric_limits<double>::infinity()) {
    cutoffs.push_back(std::numeric_limits<double>::infinity());
  }

  D dis(mean(), stddev());

  std::vector<int32_t> counts(cutoffs.size(), 0);
  for (int j = 0; j < kSamples; j++) {
    const double x = dis(rng_);
    auto it = std::upper_bound(cutoffs.begin(), cutoffs.end(), x);
    counts[std::distance(cutoffs.begin(), it)]++;
  }

  // Null-hypothesis is that the distribution is a gaussian distribution
  // with the provided mean and stddev (not estimated from the data).
  const int dof = static_cast<int>(counts.size()) - 1;

  // Our threshold for logging is 1-in-50.
  const double threshold = absl::random_internal::ChiSquareValue(dof, 0.98);

  const double expected =
      static_cast<double>(kSamples) / static_cast<double>(counts.size());

  double chi_square = absl::random_internal::ChiSquareWithExpected(
      std::begin(counts), std::end(counts), expected);
  double p = absl::random_internal::ChiSquarePValue(chi_square, dof);

  // Log if the chi_square value is above the threshold.
  if (chi_square > threshold) {
    for (size_t i = 0; i < cutoffs.size(); i++) {
      LOG(INFO) << i << " : (" << cutoffs[i] << ") = " << counts[i];
    }

    // clang-format off
    LOG(INFO) << "mean=" << mean() << " stddev=" << stddev() << "\n"
                 " expected " << expected << "\n"
              << kChiSquared << " " << chi_square << " (" << p << ")\n"
              << kChiSquared << " @ 0.98 = " << threshold;
    // clang-format on
  }
  return p;
}

TEST_P(GaussianDistributionTests, ZTest) {
  // TODO(absl-team): Run these tests against std::normal_distribution<double>
  // to validate outcomes are similar.
  const size_t kSamples = 10000;
  const auto& param = GetParam();
  const int expected_failures =
      std::max(1, static_cast<int>(std::ceil(param.trials * param.p_fail)));
  const double p = absl::random_internal::RequiredSuccessProbability(
      param.p_fail, param.trials);

  int failures = 0;
  for (int i = 0; i < param.trials; i++) {
    failures +=
        SingleZTest<absl::gaussian_distribution<double>>(p, kSamples) ? 0 : 1;
  }
  EXPECT_LE(failures, expected_failures);
}

TEST_P(GaussianDistributionTests, ChiSquaredTest) {
  const int kTrials = 20;
  int failures = 0;

  for (int i = 0; i < kTrials; i++) {
    double p_value =
        SingleChiSquaredTest<absl::gaussian_distribution<double>>();
    if (p_value < 0.0025) {  // 1/400
      failures++;
    }
  }
  // There is a 0.05% chance of producing at least one failure, so raise the
  // failure threshold high enough to allow for a flake rate of less than one in
  // 10,000.
  EXPECT_LE(failures, 4);
}

std::vector<Param> GenParams() {
  return {
      // Mean around 0.
      Param{0.0, 1.0, 0.01, 100},
      Param{0.0, 1e2, 0.01, 100},
      Param{0.0, 1e4, 0.01, 100},
      Param{0.0, 1e8, 0.01, 100},
      Param{0.0, 1e16, 0.01, 100},
      Param{0.0, 1e-3, 0.01, 100},
      Param{0.0, 1e-5, 0.01, 100},
      Param{0.0, 1e-9, 0.01, 100},
      Param{0.0, 1e-17, 0.01, 100},

      // Mean around 1.
      Param{1.0, 1.0, 0.01, 100},
      Param{1.0, 1e2, 0.01, 100},
      Param{1.0, 1e-2, 0.01, 100},

      // Mean around 100 / -100
      Param{1e2, 1.0, 0.01, 100},
      Param{-1e2, 1.0, 0.01, 100},
      Param{1e2, 1e6, 0.01, 100},
      Param{-1e2, 1e6, 0.01, 100},

      // More extreme
      Param{1e4, 1e4, 0.01, 100},
      Param{1e8, 1e4, 0.01, 100},
      Param{1e12, 1e4, 0.01, 100},
  };
}

std::string ParamName(const ::testing::TestParamInfo<Param>& info) {
  const auto& p = info.param;
  std::string name = absl::StrCat("mean_", absl::SixDigits(p.mean), "__stddev_",
                                  absl::SixDigits(p.stddev));
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(All, GaussianDistributionTests,
                         ::testing::ValuesIn(GenParams()), ParamName);

// NOTE: absl::gaussian_distribution is not guaranteed to be stable.
TEST(GaussianDistributionTest, StabilityTest) {
  // absl::gaussian_distribution stability relies on the underlying zignor
  // data, bit extraction, std::exp, std::log, and std::abs.
  absl::random_internal::sequence_urbg urbg({
      0x0003eb76f6f7f755ull,
      0xFFCEA50FDB2F953Bull,
      0xC332DDEFBE6C5AA5ull,
      0x6558218568AB9702ull,
      0x2AEF7DAD5B6E2F84ull,
      0x1521B62829076170ull,
      0xECDD4775619F1510ull,
      0x13CCA830EB61BD96ull,
      0x0334FE1EAA0363CFull,
      0xB5735C904C70A239ull,
      0xD59E9E0BCBAADE14ull,
      0xEECC86BC60622CA7ull,
      // Wedge accept (strip 59, j ~= +0.995, v ~= 0.875) -> 2 calls.
      0x7F5C28F5C28F5C3Bull,
      0xE000000000000001ull,
      // Wedge reject (strip 59, j ~= -0.9985, v ~= 0.0625) + retry -> 3 calls.
      0x80315AF024D06A3Bull,
      0x1000000000000001ull,
      0x4000000000000020ull,
      // Tail fallback (strip 0, j = -0.9375): reject then accept -> 5 calls.
      0x8800000000000000ull,
      0x0000100000000001ull,
      0xC000000000000001ull,
      0x13CCA830EB61BD96ull,
      0x00000076F6F7F755ull,
  });

  std::vector<int> output(15);

  {
    absl::gaussian_distribution<double> dist;
    std::generate(std::begin(output), std::end(output),
                  [&] { return static_cast<int>(10000000.0 * dist(urbg)); });

    EXPECT_EQ(22, urbg.invocations());
    EXPECT_THAT(
        output,  //
        testing::ElementsAre(1494, -24125, -9192725, 25518841, 9991550, 1351856,
                             -3581104, 3456682, 333530, -9491133, -7563160,
                             -2555292, 15937097, 10123234, -41861012));
  }

  urbg.reset();
  {
    absl::gaussian_distribution<float> dist;
    std::generate(std::begin(output), std::end(output),
                  [&] { return static_cast<int>(1000000.0f * dist(urbg)); });

    EXPECT_EQ(22, urbg.invocations());
    EXPECT_THAT(
        output,  //
        testing::ElementsAre(149, -2412, -919272, 2551884, 999155, 135185,
                             -358110, 345668, 33353, -949113, -756316, -255529,
                             1593709, 1012323, -4186101));
  }
}

// This is an implementation-specific test. If any part of the implementation
// changes, then it is likely that this test will change as well.
TEST(GaussianDistributionTest, AlgorithmBounds) {
  absl::gaussian_distribution<double> dist;

  // In ~95% of cases, a single value is used to generate the output.
  // for all inputs where |x| < 0.750461021389 this should be the case.
  //
  // The exact constraints are based on the ziggurat tables, and any
  // changes to the ziggurat tables may require adjusting these bounds.
  //
  // for i in range(0, len(X)-1):
  //   print i, X[i+1]/X[i], (X[i+1]/X[i] > 0.984375)
  //
  // 0.125 <= |values| <= 0.75. Positive values have bit 63 == 0; negative
  // values use two's complement (0xa000... == -0.75, 0xf000... == -0.125).
  const uint64_t kValues[] = {
      0x1000000000000100ull, 0x2000000000000100ull, 0x3000000000000100ull,
      0x4000000000000100ull, 0x5000000000000100ull, 0x6000000000000100ull,
      // negative values (-0.75 to -0.125)
      0xa000000000000100ull, 0xb000000000000100ull, 0xc000000000000100ull,
      0xd000000000000100ull, 0xe000000000000100ull, 0xf000000000000100ull};

  // 0.875 <= |values| <= 0.984375
  const uint64_t kExtraValues[] = {
      0x7000000000000100ull, 0x7800000000000100ull,  //
      0x7c00000000000100ull, 0x7e00000000000100ull,  //
      // negative values (-0.875 to -0.984375)
      0x9000000000000100ull, 0x8800000000000100ull,  //
      0x8400000000000100ull, 0x8200000000000100ull};

  auto make_box = [](uint64_t v, uint64_t box) {
    return (v & 0xffffffffffffff80ull) | box;
  };

  // The box is the lower 7 bits of the value. When the box == 0, then
  // the algorithm uses an escape hatch to select the result for large
  // outputs.
  for (uint64_t box = 0; box < 0x7f; box++) {
    for (const uint64_t v : kValues) {
      // Extra values are added to the sequence to attempt to avoid
      // infinite loops from rejection sampling on bugs/errors.
      absl::random_internal::sequence_urbg urbg(
          {make_box(v, box), 0x0003eb76f6f7f755ull, 0x5FCEA50FDB2F953Bull});

      auto a = dist(urbg);
      EXPECT_EQ(1, urbg.invocations()) << box << " " << std::hex << v;
      if (v & 0x8000000000000000ull) {
        EXPECT_LT(a, 0.0) << box << " " << std::hex << v;
      } else {
        EXPECT_GT(a, 0.0) << box << " " << std::hex << v;
      }
    }
    if (box > 10 && box < 100) {
      // The center boxes use the fast algorithm for more
      // than 98.4375% of values.
      for (const uint64_t v : kExtraValues) {
        absl::random_internal::sequence_urbg urbg(
            {make_box(v, box), 0x0003eb76f6f7f755ull, 0x5FCEA50FDB2F953Bull});

        auto a = dist(urbg);
        EXPECT_EQ(1, urbg.invocations()) << box << " " << std::hex << v;
        if (v & 0x8000000000000000ull) {
          EXPECT_LT(a, 0.0) << box << " " << std::hex << v;
        } else {
          EXPECT_GT(a, 0.0) << box << " " << std::hex << v;
        }
      }
    }
  }

  // When the box == 0, the fallback algorithm uses a ratio of uniforms,
  // which consumes 2 additional values from the urbg.
  // Fallback also requires that the initial value be > 0.9271586026096681.
  auto make_fallback = [](uint64_t v) { return (v & 0xffffffffffffff80ull); };

  double tail[2];
  {
    // 0.9375
    absl::random_internal::sequence_urbg urbg(
        {make_fallback(0x7800000000000000ull), 0x13CCA830EB61BD96ull,
         0x00000076f6f7f755ull});
    tail[0] = dist(urbg);
    EXPECT_EQ(3, urbg.invocations());
    EXPECT_GT(tail[0], 0);
  }
  {
    // -0.9375
    absl::random_internal::sequence_urbg urbg(
        {make_fallback(0x8800000000000000ull), 0x13CCA830EB61BD96ull,
         0x00000076f6f7f755ull});
    tail[1] = dist(urbg);
    EXPECT_EQ(3, urbg.invocations());
    EXPECT_LT(tail[1], 0);
  }
  EXPECT_EQ(tail[0], -tail[1]);
  EXPECT_EQ(418610, static_cast<int64_t>(tail[0] * 100000.0));

  // When the box != 0, the fallback algorithm computes a wedge function.
  // Depending on the box, the threshold for varies as high as
  // 0.991522480228.
  {
    // 0.9921875, 0.875
    absl::random_internal::sequence_urbg urbg(
        {make_box(0x7f00000000000000ull, 120), 0xe000000000000001ull,
         0x13CCA830EB61BD96ull});
    tail[0] = dist(urbg);
    EXPECT_EQ(2, urbg.invocations());
    EXPECT_GT(tail[0], 0);
  }
  {
    // -0.9921875, 0.875
    absl::random_internal::sequence_urbg urbg(
        {make_box(0x8100000000000000ull, 120), 0xe000000000000001ull,
         0x13CCA830EB61BD96ull});
    tail[1] = dist(urbg);
    EXPECT_EQ(2, urbg.invocations());
    EXPECT_LT(tail[1], 0);
  }
  EXPECT_EQ(tail[0], -tail[1]);
  EXPECT_EQ(61948, static_cast<int64_t>(tail[0] * 100000.0));

  // Fallback rejected, try again.
  {
    // -0.9921875, 0.0625
    absl::random_internal::sequence_urbg urbg(
        {make_box(0x8100000000000000ull, 120), 0x1000000000000001,
         make_box(0x1000000000000100ull, 50), 0x13CCA830EB61BD96ull});
    dist(urbg);
    EXPECT_EQ(3, urbg.invocations());
  }
}

TEST(GaussianDistributionTest, ZeroReachableAndPositiveSign) {
  auto sample_double = [](std::initializer_list<uint64_t> words) {
    absl::gaussian_distribution<double> dist;
    absl::random_internal::sequence_urbg urbg(words);
    double value = dist(urbg);
    return std::pair<double, size_t>{value, urbg.invocations()};
  };
  auto sample_float = [](std::initializer_list<uint64_t> words) {
    absl::gaussian_distribution<float> dist;
    absl::random_internal::sequence_urbg urbg(words);
    float value = dist(urbg);
    return std::pair<float, size_t>{value, urbg.invocations()};
  };

  // s == 0 across fast-path strips [0, 126], including unused bits [7..9].
  EXPECT_THAT(sample_double({0x0000000000000000ull}),
              testing::Pair(IsPositiveZero(), 1));
  EXPECT_THAT(sample_double({0x0000000000000040ull}),
              testing::Pair(IsPositiveZero(), 1));
  EXPECT_THAT(sample_double({0x000000000000007eull}),
              testing::Pair(IsPositiveZero(), 1));
  EXPECT_THAT(sample_double({0x00000000000003aaull}),
              testing::Pair(IsPositiveZero(), 1));

  // Top strip i == 127 has zg_.x[128] == 0.0, so 0.0 passes through the wedge.
  EXPECT_THAT(sample_double({0x000000000000007full, 0x8000000000000000ull}),
              testing::Pair(IsPositiveZero(), 2));

  // Exact +0.0f is reachable for float as well.
  EXPECT_THAT(sample_float({0x0000000000000000ull}),
              testing::Pair(IsPositiveZero(), 1));
  EXPECT_THAT(sample_float({0x000000000000007full, 0x8000000000000000ull}),
              testing::Pair(IsPositiveZero(), 2));
}

TEST(GaussianDistributionTest, DisjointStripAndMantissaNearZero) {
  absl::gaussian_distribution<double> dist;

  // Smallest positive and negative non-zero variates j = +/-2^-53 must be
  // completely independent of strip index i across all 128 strips.
  std::vector<double> pos_ratios;
  std::vector<double> neg_ratios;
  pos_ratios.reserve(GaussianDistributionTestPeer::kNumStrips);
  neg_ratios.reserve(GaussianDistributionTestPeer::kNumStrips);

  constexpr uint64_t kPlusOne = uint64_t{1} << 10;
  constexpr uint64_t kMinusOne = ~uint64_t{0x3ff};
  for (size_t i = 0; i < GaussianDistributionTestPeer::kNumStrips; ++i) {
    // Provide a mid-range wedge draw in case i == 127.
    absl::random_internal::sequence_urbg pos_urbg(
        {kPlusOne | i, 0x8000000000000000ull});
    absl::random_internal::sequence_urbg neg_urbg(
        {kMinusOne | i, 0x8000000000000000ull});
    const double pos = dist(pos_urbg);
    const double neg = dist(neg_urbg);
    const double expected = 0x1p-53 * GaussianDistributionTestPeer::x(i);
    pos_ratios.push_back(pos / expected);
    neg_ratios.push_back(neg / -expected);
  }
  EXPECT_THAT(pos_ratios, testing::Each(testing::DoubleEq(1.0)));
  EXPECT_THAT(neg_ratios, testing::Each(testing::DoubleEq(1.0)));

  // At the sole asymmetric endpoint j = -1.0 (s = -2^53):
  // - strip 0 routes to the negative tail fallback (< -kR);
  // - strips 1..127 enter the wedge test at f(x) = f[i], rejecting for any v
  //   bounded away from 1 by more than libm's ~1 ULP exp() tolerance.
  {
    absl::random_internal::sequence_urbg urbg(
        {0x8000000000000000ull, 0x13CCA830EB61BD96ull, 0x00000076f6f7f755ull});
    EXPECT_THAT(dist(urbg), testing::Lt(-GaussianDistributionTestPeer::kR));
    EXPECT_THAT(urbg.invocations(), testing::Eq(3));
  }
  // v = 1 - 2^-32 keeps zg_.f[i+1] + v * (zg_.f[i] - zg_.f[i+1]) at least
  // ~6.7e-13 above zg_.f[i], well above cross-platform 1 ULP libm exp()
  // differences (~1e-16).
  constexpr uint64_t kNearOneWedge = 0xffffffff00000000ull;
  std::vector<size_t> neg_one_invocations;
  neg_one_invocations.reserve(GaussianDistributionTestPeer::kNumStrips - 1);
  for (size_t i = 1; i < GaussianDistributionTestPeer::kNumStrips; ++i) {
    absl::random_internal::sequence_urbg urbg(
        {0x8000000000000000ull | i, kNearOneWedge, 0x0ull});
    EXPECT_THAT(dist(urbg), IsPositiveZero());
    neg_one_invocations.push_back(urbg.invocations());
  }
  EXPECT_THAT(neg_one_invocations, testing::Each(testing::Eq(3)));

  // Mode-region density check across |Z| < 2^-4 (the region where bit overlap
  // previously occurred) and |Z| < 2^-8.
  absl::random_internal::pcg64_2018_engine rng(0x2B7E151628AED2A6ull);
  constexpr int kSamples = 500000;
  constexpr double kEps4 = 0x1p-4;
  constexpr double kEps8 = 0x1p-8;
  int pos_eps4 = 0;
  int neg_eps4 = 0;
  int abs_eps8 = 0;
  for (int n = 0; n < kSamples; ++n) {
    const double z = dist(rng);
    if (std::abs(z) < kEps4) {
      if (z >= 0.0) {
        ++pos_eps4;
      } else {
        ++neg_eps4;
      }
      if (std::abs(z) < kEps8) {
        ++abs_eps8;
      }
    }
  }
  const double expected_p4 = std::erf(kEps4 / std::sqrt(2.0));
  const double expected_p8 = std::erf(kEps8 / std::sqrt(2.0));
  const double se_p4 = std::sqrt(expected_p4 * (1.0 - expected_p4) / kSamples);
  const double se_p8 = std::sqrt(expected_p8 * (1.0 - expected_p8) / kSamples);
  const double se_half_p4 =
      std::sqrt((expected_p4 / 2.0) * (1.0 - expected_p4 / 2.0) / kSamples);

  EXPECT_THAT(static_cast<double>(pos_eps4 + neg_eps4) / kSamples,
              testing::DoubleNear(expected_p4, 4.0 * se_p4));
  EXPECT_THAT(static_cast<double>(pos_eps4) / kSamples,
              testing::DoubleNear(expected_p4 / 2.0, 4.0 * se_half_p4));
  EXPECT_THAT(static_cast<double>(neg_eps4) / kSamples,
              testing::DoubleNear(expected_p4 / 2.0, 4.0 * se_half_p4));
  EXPECT_THAT(static_cast<double>(abs_eps8) / kSamples,
              testing::DoubleNear(expected_p8, 4.0 * se_p8));
}

TEST(GaussianDistributionTest, ZigguratTableConsistency) {
  constexpr size_t kC = GaussianDistributionTestPeer::kNumStrips;
  constexpr double kR = GaussianDistributionTestPeer::kR;
  constexpr double kRInv = GaussianDistributionTestPeer::kRInv;
  constexpr double kV = GaussianDistributionTestPeer::kV;

  EXPECT_THAT(kRInv, testing::DoubleEq(1.0 / kR));
  EXPECT_THAT(GaussianDistributionTestPeer::x(1), testing::DoubleEq(kR));
  EXPECT_THAT(GaussianDistributionTestPeer::x(kC), testing::DoubleEq(0.0));
  EXPECT_THAT(GaussianDistributionTestPeer::f(kC), testing::DoubleEq(1.0));

  // Verify strict monotonicity of x (decreasing) and f (increasing).
  for (size_t i = 0; i < kC; ++i) {
    EXPECT_THAT(GaussianDistributionTestPeer::x(i),
                testing::Gt(GaussianDistributionTestPeer::x(i + 1)));
    EXPECT_THAT(GaussianDistributionTestPeer::f(i),
                testing::Lt(GaussianDistributionTestPeer::f(i + 1)));
  }

  // Strip 0 box area (x[0] * f[1]) and true tail area must match kV.
  EXPECT_THAT(std::abs(GaussianDistributionTestPeer::x(0) *
                           GaussianDistributionTestPeer::f(1) -
                       kV) /
                  kV,
              testing::Lt(1e-15));
  const double sqrt_pi_over_2 = std::sqrt(std::acos(0.0));
  const double tail_area = kR * std::exp(-0.5 * kR * kR) +
                           sqrt_pi_over_2 * std::erfc(kR / std::sqrt(2.0));
  EXPECT_THAT(std::abs(tail_area - kV) / kV, testing::Lt(1e-13));

  // Every strip i in [1, kC - 1] must have equal area kV to < 1e-13.
  std::vector<double> strip_area_rel_errors;
  strip_area_rel_errors.reserve(kC - 1);
  for (size_t i = 1; i < kC; ++i) {
    const double area = GaussianDistributionTestPeer::x(i) *
                        (GaussianDistributionTestPeer::f(i + 1) -
                         GaussianDistributionTestPeer::f(i));
    strip_area_rel_errors.push_back(std::abs(area - kV) / kV);
  }
  EXPECT_THAT(strip_area_rel_errors, testing::Each(testing::Lt(1e-13)));

  // Every stored f[i] must match std::exp(-0.5 * x[i] * x[i]) within 1 ULP.
  std::vector<double> f_ulp_errors;
  f_ulp_errors.reserve(kC + 1);
  for (size_t i = 0; i <= kC; ++i) {
    const double xi = GaussianDistributionTestPeer::x(i);
    const double expected_f = std::exp(-0.5 * xi * xi);
    const double ulp =
        std::nextafter(expected_f, std::numeric_limits<double>::infinity()) -
        expected_f;
    f_ulp_errors.push_back(
        std::abs(GaussianDistributionTestPeer::f(i) - expected_f) / ulp);
  }
  EXPECT_THAT(f_ulp_errors, testing::Each(testing::Le(1.0)));
}

}  // namespace
