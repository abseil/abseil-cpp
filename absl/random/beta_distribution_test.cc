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

#include "absl/random/beta_distribution.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
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

namespace {

using ::testing::AllOf;
using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::Eq;
using ::testing::FloatEq;
using ::testing::Ge;
using ::testing::Le;
using ::testing::Pair;

template <typename IntType>
class BetaDistributionInterfaceTest : public ::testing::Test {};

constexpr bool ShouldExerciseLongDoubleTests() {
  // long double arithmetic is not supported well by either GCC or Clang on
  // most platforms specifically not when implemented in terms of double-double;
  // see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99048,
  // https://bugs.llvm.org/show_bug.cgi?id=49131, and
  // https://bugs.llvm.org/show_bug.cgi?id=49132.
  // So a conservative choice here is to disable long-double tests pretty much
  // everywhere except on x64 but only if long double is not implemented as
  // double-double.
#if defined(__i686__) && defined(__x86_64__)
  return !absl::numeric_internal::IsDoubleDouble();
#else
  return false;
#endif
}

using RealTypes =
    std::conditional_t<ShouldExerciseLongDoubleTests(),
                       ::testing::Types<float, double, long double>,
                       ::testing::Types<float, double>>;
TYPED_TEST_SUITE(BetaDistributionInterfaceTest, RealTypes);

TYPED_TEST(BetaDistributionInterfaceTest, DefaultParamType) {
  using Dist = absl::beta_distribution<TypeParam>;
  using param_type = typename Dist::param_type;
  static_assert(std::is_trivially_copyable_v<param_type>);
  static_assert(std::is_trivially_destructible_v<param_type>);
  EXPECT_THAT(param_type(), Eq(param_type(TypeParam(1), TypeParam(1))));
  EXPECT_THAT(param_type(TypeParam(2)),
              Eq(param_type(TypeParam(2), TypeParam(1))));
  EXPECT_THAT(Dist().param(), Eq(param_type()));
}

TYPED_TEST(BetaDistributionInterfaceTest, SerializeTest) {
  // The threshold for whether std::exp(1/a) is finite.
  const TypeParam kSmallA =
      1.0f / std::log((std::numeric_limits<TypeParam>::max)());
  // The threshold for whether a * std::log(a) is finite.
  const TypeParam kLargeA =
      std::exp(std::log((std::numeric_limits<TypeParam>::max)()) -
               std::log(std::log((std::numeric_limits<TypeParam>::max)())));
  using param_type = typename absl::beta_distribution<TypeParam>::param_type;

  constexpr int kCount = 1000;
  absl::InsecureBitGen gen;
  const TypeParam kValues[] = {
      TypeParam(1e-20),
      TypeParam(1e-12),
      TypeParam(1e-8),
      TypeParam(1e-4),
      TypeParam(1e-3),
      TypeParam(0.1),
      TypeParam(0.25),
      std::nextafter(TypeParam(0.5), TypeParam(0)),  // 0.5 - epsilon
      std::nextafter(TypeParam(0.5), TypeParam(1)),  // 0.5 + epsilon
      TypeParam(0.5),
      TypeParam(1.0),                              //
      std::nextafter(TypeParam(1), TypeParam(0)),  // 1 - epsilon
      std::nextafter(TypeParam(1), TypeParam(2)),  // 1 + epsilon
      TypeParam(12.5),
      TypeParam(1e2),
      TypeParam(1e8),
      TypeParam(1e12),
      TypeParam(1e20),                        //
      kSmallA,                                //
      std::nextafter(kSmallA, TypeParam(0)),  //
      std::nextafter(kSmallA, TypeParam(1)),  //
      kLargeA,                                //
      std::nextafter(kLargeA, TypeParam(0)),  //
      std::nextafter(kLargeA, std::numeric_limits<TypeParam>::max()),
      // Boundary cases.
      std::numeric_limits<TypeParam>::max(),
      std::numeric_limits<TypeParam>::epsilon(),
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(1)),                  // min + epsilon
      std::numeric_limits<TypeParam>::min(),         // smallest normal
      std::numeric_limits<TypeParam>::denorm_min(),  // smallest denorm
      std::numeric_limits<TypeParam>::min() / 2,     // denorm
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(0)),  // denorm_max
  };
  for (TypeParam alpha : kValues) {
    for (TypeParam beta : kValues) {
      LOG(INFO) << absl::StreamFormat("Smoke test for Beta(%a, %a)", alpha,
                                      beta);

      param_type param(alpha, beta);
      absl::beta_distribution<TypeParam> before(alpha, beta);
      EXPECT_EQ(before.alpha(), param.alpha());
      EXPECT_EQ(before.beta(), param.beta());

      {
        absl::beta_distribution<TypeParam> via_param(param);
        EXPECT_EQ(via_param, before);
        EXPECT_EQ(via_param.param(), before.param());
      }

      // Smoke test.
      for (int i = 0; i < kCount; ++i) {
        auto sample = before(gen);
        EXPECT_TRUE(std::isfinite(sample));
        EXPECT_GE(sample, before.min());
        EXPECT_LE(sample, before.max());
      }

      // Validate stream serialization.
      std::stringstream ss;
      ss << before;
      absl::beta_distribution<TypeParam> after(3.8f, 1.43f);
      EXPECT_NE(before.alpha(), after.alpha());
      EXPECT_NE(before.beta(), after.beta());
      EXPECT_NE(before.param(), after.param());
      EXPECT_NE(before, after);

      ss >> after;

      EXPECT_EQ(before.alpha(), after.alpha());
      EXPECT_EQ(before.beta(), after.beta());
      EXPECT_EQ(before, after)           //
          << ss.str() << " "             //
          << (ss.good() ? "good " : "")  //
          << (ss.bad() ? "bad " : "")    //
          << (ss.eof() ? "eof " : "")    //
          << (ss.fail() ? "fail " : "");
    }
  }
}

TYPED_TEST(BetaDistributionInterfaceTest, DegenerateCases) {
  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng(0x2B7E151628AED2A6);

  // Extreme cases when the params are abnormal.
  constexpr int kCount = 1000;
  const TypeParam kSmallValues[] = {
      std::numeric_limits<TypeParam>::min(),
      std::numeric_limits<TypeParam>::denorm_min(),
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(0)),  // denorm_max
      std::numeric_limits<TypeParam>::epsilon(),
  };
  const TypeParam kLargeValues[] = {
      std::numeric_limits<TypeParam>::max() * static_cast<TypeParam>(0.9999),
      std::numeric_limits<TypeParam>::max() - 1,
      std::numeric_limits<TypeParam>::max(),
  };
  {
    // Small alpha and beta.
    // Useful WolframAlpha plots:
    //   * plot InverseBetaRegularized[x, 0.0001, 0.0001] from 0.495 to 0.505
    //   * Beta[1.0, 0.0000001, 0.0000001]
    //   * Beta[0.9999, 0.0000001, 0.0000001]
    for (TypeParam alpha : kSmallValues) {
      for (TypeParam beta : kSmallValues) {
        int zeros = 0;
        int ones = 0;
        absl::beta_distribution<TypeParam> d(alpha, beta);
        for (int i = 0; i < kCount; ++i) {
          TypeParam x = d(rng);
          if (x == 0.0) {
            zeros++;
          } else if (x == 1.0) {
            ones++;
          }
        }
        EXPECT_EQ(ones + zeros, kCount);
        if (alpha == beta) {
          EXPECT_NE(ones, 0);
          EXPECT_NE(zeros, 0);
        }
      }
    }
  }
  {
    // Small alpha, large beta.
    // Useful WolframAlpha plots:
    //   * plot InverseBetaRegularized[x, 0.0001, 10000] from 0.995 to 1
    //   * Beta[0, 0.0000001, 1000000]
    //   * Beta[0.001, 0.0000001, 1000000]
    //   * Beta[1, 0.0000001, 1000000]
    for (TypeParam alpha : kSmallValues) {
      for (TypeParam beta : kLargeValues) {
        absl::beta_distribution<TypeParam> d(alpha, beta);
        for (int i = 0; i < kCount; ++i) {
          // Beta(epsilon, max) can rightly yield a subnormal from the Gamma
          // tail, so only require the sample to be below min_normal.
          EXPECT_LT(d(rng), (std::numeric_limits<TypeParam>::min)());
        }
      }
    }
  }
  {
    // Large alpha, small beta.
    // Useful WolframAlpha plots:
    //   * plot InverseBetaRegularized[x, 10000, 0.0001] from 0 to 0.001
    //   * Beta[0.99, 1000000, 0.0000001]
    //   * Beta[1, 1000000, 0.0000001]
    for (TypeParam alpha : kLargeValues) {
      for (TypeParam beta : kSmallValues) {
        absl::beta_distribution<TypeParam> d(alpha, beta);
        for (int i = 0; i < kCount; ++i) {
          EXPECT_EQ(d(rng), 1.0);
        }
      }
    }
  }
  {
    // Large alpha and beta.
    absl::beta_distribution<TypeParam> d(std::numeric_limits<TypeParam>::max(),
                                         std::numeric_limits<TypeParam>::max());
    for (int i = 0; i < kCount; ++i) {
      EXPECT_EQ(d(rng), 0.5);
    }
  }
  {
    // Large alpha and beta but unequal.
    absl::beta_distribution<TypeParam> d(
        std::numeric_limits<TypeParam>::max(),
        std::numeric_limits<TypeParam>::max() * 0.9999);
    for (int i = 0; i < kCount; ++i) {
      TypeParam x = d(rng);
      EXPECT_NE(x, 0.5f);
      EXPECT_FLOAT_EQ(x, 0.500025f);
    }
  }
}

class BetaDistributionModel {
 public:
  explicit BetaDistributionModel(::testing::tuple<double, double> p)
      : alpha_(::testing::get<0>(p)), beta_(::testing::get<1>(p)) {}

  double Mean() const { return alpha_ / (alpha_ + beta_); }

  double Variance() const {
    return alpha_ * beta_ / (alpha_ + beta_ + 1) / (alpha_ + beta_) /
           (alpha_ + beta_);
  }

  double Kurtosis() const {
    return 3 + 6 *
                   ((alpha_ - beta_) * (alpha_ - beta_) * (alpha_ + beta_ + 1) -
                    alpha_ * beta_ * (2 + alpha_ + beta_)) /
                   alpha_ / beta_ / (alpha_ + beta_ + 2) / (alpha_ + beta_ + 3);
  }

 protected:
  const double alpha_;
  const double beta_;
};

class BetaDistributionTest
    : public ::testing::TestWithParam<::testing::tuple<double, double>>,
      public BetaDistributionModel {
 public:
  BetaDistributionTest() : BetaDistributionModel(GetParam()) {}

 protected:
  template <class D>
  bool SingleZTestOnMeanAndVariance(double p, size_t samples);

  template <class D>
  bool SingleChiSquaredTest(double p, size_t samples, size_t buckets);

  absl::InsecureBitGen rng_;
};

template <class D>
bool BetaDistributionTest::SingleZTestOnMeanAndVariance(double p,
                                                        size_t samples) {
  D dis(alpha_, beta_);

  std::vector<double> data;
  data.reserve(samples);
  for (size_t i = 0; i < samples; i++) {
    const double variate = dis(rng_);
    EXPECT_FALSE(std::isnan(variate));
    // Note that equality is allowed on both sides.
    EXPECT_GE(variate, 0.0);
    EXPECT_LE(variate, 1.0);
    data.push_back(variate);
  }

  // We validate that the sample mean and sample variance are indeed from a
  // Beta distribution with the given shape parameters.
  const auto m = absl::random_internal::ComputeDistributionMoments(data);

  // The variance of the sample mean is variance / n.
  const double mean_stddev = std::sqrt(Variance() / static_cast<double>(m.n));

  // The variance of the sample variance is (approximately):
  //   (kurtosis - 1) * variance^2 / n
  const double variance_stddev = std::sqrt(
      (Kurtosis() - 1) * Variance() * Variance() / static_cast<double>(m.n));
  // z score for the sample variance.
  const double z_variance = (m.variance - Variance()) / variance_stddev;

  const double max_err = absl::random_internal::MaxErrorTolerance(p);
  const double z_mean = absl::random_internal::ZScore(Mean(), m);
  const bool pass =
      absl::random_internal::Near("z", z_mean, 0.0, max_err) &&
      absl::random_internal::Near("z_variance", z_variance, 0.0, max_err);
  if (!pass) {
    LOG(INFO) << "Beta(" << alpha_ << ", " << beta_ << "), mean: sample "
              << m.mean << ", expect " << Mean() << ", which is "
              << std::abs(m.mean - Mean()) / mean_stddev
              << " stddevs away, variance: sample " << m.variance << ", expect "
              << Variance() << ", which is "
              << std::abs(m.variance - Variance()) / variance_stddev
              << " stddevs away.";
  }
  return pass;
}

template <class D>
bool BetaDistributionTest::SingleChiSquaredTest(double p, size_t samples,
                                                size_t buckets) {
  constexpr double kErr = 1e-7;
  std::vector<double> cutoffs, expected;
  const double bucket_width = 1.0 / static_cast<double>(buckets);
  int i = 1;
  int unmerged_buckets = 0;
  for (; i < buckets; ++i) {
    const double p = bucket_width * static_cast<double>(i);
    const double boundary =
        absl::random_internal::BetaIncompleteInv(alpha_, beta_, p);
    // The intention is to add `boundary` to the list of `cutoffs`. It becomes
    // problematic, however, when the boundary values are not monotone, due to
    // numerical issues when computing the inverse regularized incomplete
    // Beta function. In these cases, we merge that bucket with its previous
    // neighbor and merge their expected counts.
    if ((cutoffs.empty() && boundary < kErr) ||
        (!cutoffs.empty() && boundary <= cutoffs.back())) {
      unmerged_buckets++;
      continue;
    }
    if (boundary >= 1.0 - 1e-10) {
      break;
    }
    cutoffs.push_back(boundary);
    expected.push_back(static_cast<double>(1 + unmerged_buckets) *
                       bucket_width * static_cast<double>(samples));
    unmerged_buckets = 0;
  }
  cutoffs.push_back(std::numeric_limits<double>::infinity());
  // Merge all remaining buckets.
  expected.push_back(static_cast<double>(buckets - i + 1) * bucket_width *
                     static_cast<double>(samples));
  // Make sure that we don't merge all the buckets, making this test
  // meaningless.
  EXPECT_GE(cutoffs.size(), 3) << alpha_ << ", " << beta_;

  D dis(alpha_, beta_);

  std::vector<int32_t> counts(cutoffs.size(), 0);
  for (int i = 0; i < samples; i++) {
    const double x = dis(rng_);
    auto it = std::upper_bound(cutoffs.begin(), cutoffs.end(), x);
    counts[std::distance(cutoffs.begin(), it)]++;
  }

  // Null-hypothesis is that the distribution is beta distributed with the
  // provided alpha, beta params (not estimated from the data).
  const int dof = cutoffs.size() - 1;

  const double chi_square = absl::random_internal::ChiSquare(
      counts.begin(), counts.end(), expected.begin(), expected.end());
  const bool pass =
      (absl::random_internal::ChiSquarePValue(chi_square, dof) >= p);
  if (!pass) {
    for (size_t i = 0; i < cutoffs.size(); i++) {
      LOG(INFO) << "cutoff[" << i << "] = " << cutoffs[i] << ", actual count "
                << counts[i] << ", expected " << static_cast<int>(expected[i]);
    }

    LOG(INFO) << "Beta(" << alpha_ << ", " << beta_ << ") "
              << absl::random_internal::kChiSquared << " " << chi_square
              << ", p = "
              << absl::random_internal::ChiSquarePValue(chi_square, dof);
  }
  return pass;
}

TEST_P(BetaDistributionTest, TestSampleStatistics) {
  static constexpr int kRuns = 20;
  static constexpr double kPFail = 0.02;
  const double p =
      absl::random_internal::RequiredSuccessProbability(kPFail, kRuns);
  static constexpr int kSampleCount = 10000;
  static constexpr int kBucketCount = 100;
  int failed = 0;
  for (int i = 0; i < kRuns; ++i) {
    if (!SingleZTestOnMeanAndVariance<absl::beta_distribution<double>>(
            p, kSampleCount)) {
      failed++;
    }
    if (!SingleChiSquaredTest<absl::beta_distribution<double>>(
            0.005, kSampleCount, kBucketCount)) {
      failed++;
    }
  }
  // Set so that the test is not flaky at --runs_per_test=10000
  EXPECT_LE(failed, 5);
}

std::string ParamName(
    const ::testing::TestParamInfo<::testing::tuple<double, double>>& info) {
  std::string name = absl::StrCat("alpha_", ::testing::get<0>(info.param),
                                  "__beta_", ::testing::get<1>(info.param));
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(
    TestSampleStatisticsCombinations, BetaDistributionTest,
    ::testing::Combine(::testing::Values(0.1, 0.2, 0.9, 1.1, 2.5, 10.0, 123.4),
                       ::testing::Values(0.1, 0.2, 0.9, 1.1, 2.5, 10.0, 123.4)),
    ParamName);

INSTANTIATE_TEST_SUITE_P(
    TestSampleStatistics_SelectedPairs, BetaDistributionTest,
    ::testing::Values(std::make_pair(0.5, 1000), std::make_pair(1000, 0.5),
                      std::make_pair(900, 1000), std::make_pair(10000, 20000),
                      std::make_pair(4e5, 2e7), std::make_pair(1e7, 1e5)),
    ParamName);

// NOTE: absl::beta_distribution is not guaranteed to be stable.
TEST(BetaDistributionTest, StabilityTest) {
  // absl::beta_distribution stability relies on the stability of
  // absl::random_interna::RandU64ToDouble, std::exp, std::log, std::pow,
  // and std::sqrt.
  //
  // This test also depends on the stability of std::frexp.
  using testing::ElementsAre;
  absl::random_internal::sequence_urbg urbg({
      0xffff00000000e6c8ull, 0xffff0000000006c8ull, 0x800003766295CFA9ull,
      0x11C819684E734A41ull, 0x832603766295CFA9ull, 0x7fbe76c8b4395800ull,
      0xB3472DCA7B14A94Aull, 0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull,
      0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull, 0x00035C904C70A239ull,
      0x00009E0BCBAADE14ull, 0x0000000000622CA7ull, 0x4864f22c059bf29eull,
      0x247856d8b862665cull, 0xe46e86e9a1337e10ull, 0xd8c8541f3519b133ull,
      0xffe75b52c567b9e4ull, 0xfffff732e5709c5bull, 0xff1f7f0b983532acull,
      0x1ec2e8986d2362caull, 0xC332DDEFBE6C5AA5ull, 0x6558218568AB9702ull,
      0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull, 0xECDD4775619F1510ull,
      0x814c8e35fe9a961aull, 0x0c3cd59c9b638a02ull, 0xcb3bb6478a07715cull,
      0x1224e62c978bbc7full, 0x671ef2cb04e81f6eull, 0x3c1cbd811eaf1808ull,
      0x1bbc23cfa8fac721ull, 0xa4c2cda65e596a51ull, 0xb77216fad37adf91ull,
      0x836d794457c08849ull, 0xe083df03475f49d7ull, 0xbc9feb512e6b0d6cull,
      0xb12d74fdd718c8c5ull, 0x12ff09653bfbe4caull, 0x8dd03a105bc4ee7eull,
      0x5738341045ba0d85ull, 0xf3fd722dc65ad09eull, 0xfa14fd21ea2a5705ull,
      0xffe6ea4d6edb0c73ull, 0xD07E9EFE2BF11FB4ull, 0x95DBDA4DAE909198ull,
      0xEAAD8E716B93D5A0ull, 0xD08ED1D0AFC725E0ull, 0x8E3C5B2F8E7594B7ull,
      0x8FF6E2FBF2122B64ull, 0x8888B812900DF01Cull, 0x4FAD5EA0688FC31Cull,
      0xD1CFF191B3A8C1ADull, 0x2F2F2218BE0E1777ull, 0xEA752DFE8B021FA1ull,
  });

  // Convert the real-valued result into a unit64 where we compare
  // 5 (float) or 10 (double) decimal digits plus the base-2 exponent.
  auto float_to_u64 = [](float d) {
    int exp = 0;
    auto f = std::frexp(d, &exp);
    return (static_cast<uint64_t>(1e5 * f) * 10000) + std::abs(exp);
  };
  auto double_to_u64 = [](double d) {
    int exp = 0;
    auto f = std::frexp(d, &exp);
    return (static_cast<uint64_t>(1e10 * f) * 10000) + std::abs(exp);
  };

  std::vector<uint64_t> output(20);
  {
    // Algorithm Joehnk (float)
    absl::beta_distribution<float> dist(0.1f, 0.2f);
    std::generate(std::begin(output), std::end(output),
                  [&] { return float_to_u64(dist(urbg)); });
    EXPECT_EQ(44, urbg.invocations());
    EXPECT_THAT(output,  //
                testing::ElementsAre(
                    998340000, 619030004, 500000001, 999990000, 996280000,
                    500000001, 844740004, 847210001, 999970000, 872320000,
                    585480007, 933280000, 869080042, 647670031, 528240004,
                    969980004, 626050008, 915930002, 833450033, 878040015));
  }

  urbg.reset();
  {
    // Algorithm Joehnk (double)
    absl::beta_distribution<double> dist(0.1, 0.2);
    std::generate(std::begin(output), std::end(output),
                  [&] { return double_to_u64(dist(urbg)); });
    EXPECT_EQ(44, urbg.invocations());
    EXPECT_THAT(
        output,  //
        testing::ElementsAre(
            99834713000000, 61903356870004, 50000000000001, 99999721170000,
            99628374770000, 99999999990000, 84474397860004, 84721276240001,
            99997407490000, 87232528120000, 58548364780007, 93328932910000,
            86908237770042, 64767917930031, 52824581970004, 96998544140004,
            62605946270008, 91593604380002, 83345031740033, 87804397230015));
  }

  urbg.reset();
  {
    // Algorithm Cheng 1
    absl::beta_distribution<double> dist(0.9, 2.0);
    std::generate(std::begin(output), std::end(output),
                  [&] { return double_to_u64(dist(urbg)); });
    EXPECT_EQ(62, urbg.invocations());
    EXPECT_THAT(
        output,  //
        testing::ElementsAre(
            62069004780001, 64433204450001, 53607416560000, 89644295430008,
            61434586310019, 55172615890002, 62187161490000, 56433684810003,
            80454622050005, 86418558710003, 92920514700001, 64645184680001,
            58549183380000, 84881283650005, 71078728590002, 69949694970000,
            73157461710001, 68592191300001, 70747623900000, 78584696930005));
  }

  urbg.reset();
  {
    // Algorithm Cheng 2
    absl::beta_distribution<double> dist(1.5, 2.5);
    std::generate(std::begin(output), std::end(output),
                  [&] { return double_to_u64(dist(urbg)); });
    EXPECT_EQ(54, urbg.invocations());
    EXPECT_THAT(
        output,  //
        testing::ElementsAre(
            75000029250001, 76751482860001, 53264575220000, 69193133650005,
            78028324470013, 91573587560002, 59167523770000, 60658618560002,
            80075870540000, 94141320460004, 63196592770003, 78883906300002,
            96797992590001, 76907587800001, 56645167560000, 65408302280003,
            53401156320001, 64731238570000, 83065573750001, 79788333820001));
  }
}

// This is an implementation-specific test. If any part of the implementation
// changes, then it is likely that this test will change as well.  Also, if
// dependencies of the distribution change, such as RandU64ToDouble, then this
// is also likely to change.
TEST(BetaDistributionTest, AlgorithmBounds) {
#if (defined(__i386__) || defined(_M_IX86)) && FLT_EVAL_METHOD != 0
  // We're using an x87-compatible FPU, and intermediate operations are
  // performed with 80-bit floats. This produces slightly different results from
  // what we expect below.
  GTEST_SKIP()
      << "Skipping the test because we detected x87 floating-point semantics";
#endif

  {
    absl::random_internal::sequence_urbg urbg(
        {0x7fbe76c8b4395800ull, 0x8000000000000000ull});
    // u=0.499, v=0.5
    absl::beta_distribution<double> dist(1e-4, 1e-4);
    double a = dist(urbg);
    EXPECT_EQ(a, 2.0202860861579454e-09);
    EXPECT_EQ(2, urbg.invocations());
  }

  // Test that both the float & double algorithms appropriately reject the
  // initial draw.
  {
    // 1/alpha = 1/beta = 2.
    absl::beta_distribution<float> dist(0.5, 0.5);

    // first two outputs are close to 1.0 - epsilon,
    // thus:  (u ^ 2 + v ^ 2) > 1.0
    absl::random_internal::sequence_urbg urbg(
        {0xffff00000006e6c8ull, 0xffff00000007c7c8ull, 0x800003766295CFA9ull,
         0x11C819684E734A41ull});
    {
      double y = absl::beta_distribution<double>(0.5, 0.5)(urbg);
      EXPECT_EQ(4, urbg.invocations());
      EXPECT_EQ(y, 0.9810668952633862) << y;
    }

    // ...and:  log(u) * a ~= log(v) * b ~= -0.02
    // thus z ~= -0.02 + log(1 + e(~0))
    //        ~= -0.02 + 0.69
    // thus z > 0
    urbg.reset();
    {
      float x = absl::beta_distribution<float>(0.5, 0.5)(urbg);
      EXPECT_EQ(4, urbg.invocations());
      EXPECT_NEAR(0.98106688261032104, x, 0.0000005) << x << "f";
    }
  }
}

struct SampleMoments {
  double mean;
  double stddev;
  int zeros;
  int ones;
  int total;
};

void PrintTo(const SampleMoments& m, std::ostream* os) {
  *os << "{mean=" << absl::StreamFormat("%.17g", m.mean)
      << ", stddev=" << absl::StreamFormat("%.17g", m.stddev)
      << ", zeros=" << m.zeros << ", ones=" << m.ones << ", total=" << m.total
      << "}";
}

template <typename RealType>
SampleMoments SampleScaledMoments(RealType alpha, RealType beta, double scale,
                                  int samples) {
  absl::random_internal::pcg64_2018_engine rng(0x2B7E151628AED2A6ULL);
  absl::beta_distribution<RealType> dist(alpha, beta);
  std::vector<double> values;
  values.reserve(static_cast<size_t>(samples));
  int zeros = 0;
  int ones = 0;
  double sum = 0.0;
  for (int i = 0; i < samples; ++i) {
    const RealType sample = dist(rng);
    EXPECT_TRUE(std::isfinite(sample));
    EXPECT_GE(sample, RealType(0));
    EXPECT_LE(sample, RealType(1));
    if (sample == RealType(0)) ++zeros;
    if (sample == RealType(1)) ++ones;
    const double scaled = static_cast<double>(sample) * scale;
    values.push_back(scaled);
    sum += scaled;
  }
  const double mean = sum / samples;
  double sq_diff_sum = 0.0;
  for (double v : values) {
    const double d = v - mean;
    sq_diff_sum += d * d;
  }
  const double stddev = std::sqrt(sq_diff_sum / (samples - 1));
  return SampleMoments{mean, stddev, zeros, ones, samples};
}

template <typename RealType>
RealType SampleBetaWithBits(RealType alpha, RealType beta,
                            std::initializer_list<uint64_t> bits) {
  absl::random_internal::sequence_urbg urbg(bits);
  absl::beta_distribution<RealType> dist(alpha, beta);
  return dist(urbg);
}

template <typename RealType>
std::pair<RealType, size_t> SampleBetaWithBitsAndCount(
    RealType alpha, RealType beta, std::initializer_list<uint64_t> bits) {
  absl::random_internal::sequence_urbg urbg(bits);
  absl::beta_distribution<RealType> dist(alpha, beta);
  const RealType sample = dist(urbg);
  return {sample, urbg.invocations()};
}

MATCHER_P4(HasMomentsNear, expected_mean, mean_tol, expected_stddev, stddev_tol,
           absl::StrFormat("has mean within %.3g of %.6g and stddev within "
                           "%.3g of %.6g",
                           mean_tol, expected_mean, stddev_tol,
                           expected_stddev)) {
  const double mean_err = std::abs(arg.mean - expected_mean);
  const double stddev_err = std::abs(arg.stddev - expected_stddev);
  if (mean_err <= mean_tol && stddev_err <= stddev_tol) {
    return true;
  }
  *result_listener << "mean error " << mean_err << " (tol " << mean_tol
                   << "), stddev error " << stddev_err << " (tol " << stddev_tol
                   << ")";
  return false;
}

MATCHER_P2(IsBernoulliNear, expected_p, tol,
           absl::StrFormat("contains only 0/1 samples with mean within %.3g "
                           "of %.6g",
                           tol, expected_p)) {
  if (arg.zeros + arg.ones != arg.total) {
    *result_listener << "has " << (arg.total - arg.zeros - arg.ones)
                     << " non-binary samples";
    return false;
  }
  const double err = std::abs(arg.mean - expected_p);
  if (err <= tol) {
    return true;
  }
  *result_listener << "mean error " << err << " (tol " << tol << ")";
  return false;
}

TEST(BetaDistributionTest, HighSkewMeanAccuracy) {
  constexpr int kSamples = 20000;

  EXPECT_THAT(SampleScaledMoments<float>(0.5f, 1e8f, 1e8, kSamples),
              HasMomentsNear(0.5, 0.05, std::sqrt(0.5), 0.06));
  EXPECT_THAT(SampleScaledMoments<double>(0.5, 1e18, 1e18, kSamples),
              HasMomentsNear(0.5, 0.05, std::sqrt(0.5), 0.06));
  EXPECT_THAT(SampleScaledMoments<double>(2.0, 1e20, 1e20, kSamples),
              HasMomentsNear(2.0, 0.1, std::sqrt(2.0), 0.08));
}

TEST(BetaDistributionTest, HighAlphaUpperTailRoundsToOneWithoutBias) {
  // True 1 - X mean is 5e-19 and P(1 - X > 2^-54) < 1e-20, so every sample
  // must round to exactly 1.0 rather than suffering cancellation bias.
  EXPECT_THAT(SampleScaledMoments<double>(1e18, 0.5, 1.0, 20000),
              IsBernoulliNear(1.0, 0.0));
}

TEST(BetaDistributionTest, LargeBetaNearMaxPreservesMeanRatio) {
  absl::random_internal::pcg64_2018_engine rng(0x2B7E151628AED2A6ULL);
  const double max_val = (std::numeric_limits<double>::max)();
  const double alpha = 1e305;
  const double r = alpha / max_val;
  const double expected_ratio = r / (1.0 + r);
  absl::beta_distribution<double> dist(alpha, max_val);
  absl::beta_distribution<double> inv_dist(max_val, alpha);

  EXPECT_THAT(dist(rng), DoubleNear(expected_ratio, expected_ratio * 1e-12));
  EXPECT_THAT(1.0 - inv_dist(rng),
              DoubleNear(expected_ratio, expected_ratio * 1e-12));
}

TEST(BetaDistributionTest, ChengLargeSymmetricPrecision) {
  constexpr int kSamples = 20000;
  // True stddev of Beta(a, a) is 1 / sqrt(4 * (2a + 1)). A 5% tolerance
  // catches both the +28.3% Logistic inflation from s = (a + r) - w
  // cancellation and discretization spikes from exp(v) around 1.0.
  const double sigma_1e22 = 1.0 / std::sqrt(4.0 * 2e22);
  const double sigma_1e30 = 1.0 / std::sqrt(4.0 * 2e30);
  const double sigma_1e9 = 1.0 / std::sqrt(4.0 * 2e9);

  EXPECT_THAT(
      SampleScaledMoments<double>(1e22, 1e22, 1.0, kSamples),
      HasMomentsNear(0.5, sigma_1e22 * 0.05, sigma_1e22, sigma_1e22 * 0.05));
  EXPECT_THAT(
      SampleScaledMoments<double>(1e30, 1e30, 1.0, kSamples),
      HasMomentsNear(0.5, sigma_1e30 * 0.05, sigma_1e30, sigma_1e30 * 0.05));
  EXPECT_THAT(
      SampleScaledMoments<float>(1e9f, 1e9f, 1.0, kSamples),
      HasMomentsNear(0.5, sigma_1e9 * 0.05, sigma_1e9, sigma_1e9 * 0.05));
}

TEST(BetaDistributionTest, ChengFloatProductUnderflowDoesNotFalseAccept) {
  // Verify float ChengBB computes t = log(u1*u1*u2) in double rather than
  // float: for u1 = u2 = 2^-65, float(u1*u1*u2) flushes to 0.0f (t = -inf)
  // and would falsely accept on the first draw (2 calls). The second draw
  // (u1 = 0.5, so v = 0) is accepted by the squeeze and returns exactly 0.5.
  EXPECT_THAT(SampleBetaWithBitsAndCount<float>(
                  5.0f, 5.0f,
                  {0ull, 0ull, 0x8000000000000000ull, 0x4000000000000000ull}),
              Pair(FloatEq(0.5f), 4u));
}

TEST(BetaDistributionTest, SkewedHugeBetaRetainsVariance) {
  constexpr int kSamples = 20000;
  // For b >= ThresholdForLargeA() and moderate a, b * Beta(a, b) ~ Gamma(a, 1)
  // with mean a and stddev sqrt(a), rather than collapsing to a constant.
  EXPECT_THAT(SampleScaledMoments<double>(2.0, 3e305, 3e305, kSamples),
              HasMomentsNear(2.0, 0.06, std::sqrt(2.0), 0.06));
  EXPECT_THAT(SampleScaledMoments<double>(1.0, 3e305, 3e305, kSamples),
              HasMomentsNear(1.0, 0.05, 1.0, 0.05));
  EXPECT_THAT(SampleScaledMoments<double>(0.5, 3e305, 3e305, kSamples),
              HasMomentsNear(0.5, 0.04, std::sqrt(0.5), 0.05));
  EXPECT_THAT(SampleScaledMoments<float>(2.0f, 1e37f, 1e37, kSamples),
              HasMomentsNear(2.0, 0.06, std::sqrt(2.0), 0.06));
  // Both sides of the Cheng BB y-formula overflow switch at b = 2^511.
  const double kChengSplit = std::ldexp(1.0, 511);
  const double kBelowSplit = std::nextafter(kChengSplit, 0.0);
  EXPECT_THAT(
      SampleScaledMoments<double>(2.0, kBelowSplit, kBelowSplit, kSamples),
      HasMomentsNear(2.0, 0.06, std::sqrt(2.0), 0.06));
  EXPECT_THAT(
      SampleScaledMoments<double>(2.0, kChengSplit, kChengSplit, kSamples),
      HasMomentsNear(2.0, 0.06, std::sqrt(2.0), 0.06));
  // Parameters just above 1 enter Cheng BB with a - 1 = b - 1 = eps and follow
  // Uniform(0, 1) (mean 1/2, stddev 1 / sqrt(12)) without y cancellation.
  const double kAboveOne = std::nextafter(1.0, 2.0);
  const float kAboveOneF = std::nextafter(1.0f, 2.0f);
  EXPECT_THAT(SampleScaledMoments<double>(kAboveOne, kAboveOne, 1.0, kSamples),
              HasMomentsNear(0.5, 0.01, 1.0 / std::sqrt(12.0), 0.01));
  EXPECT_THAT(SampleScaledMoments<float>(kAboveOneF, kAboveOneF, 1.0, kSamples),
              HasMomentsNear(0.5, 0.01, 1.0 / std::sqrt(12.0), 0.01));
}

TEST(BetaDistributionTest, SubnormalMeanRatioRetainsGammaShape) {
  constexpr int kSamples = 20000;
  constexpr double kDblMax = (std::numeric_limits<double>::max)();
  constexpr float kFltMax = (std::numeric_limits<float>::max)();
  // a / (a + b) < DBL_MIN, so samples are subnormal; max * Beta(a, max) must
  // still follow Gamma(a, 1) rather than flushing or quantizing a / (a + b).
  EXPECT_THAT(SampleScaledMoments<double>(0.5, kDblMax, kDblMax, kSamples),
              HasMomentsNear(0.5, 0.04, std::sqrt(0.5), 0.05));
  EXPECT_THAT(SampleScaledMoments<double>(3.0, kDblMax, kDblMax, kSamples),
              HasMomentsNear(3.0, 0.08, std::sqrt(3.0), 0.08));
  EXPECT_THAT(SampleScaledMoments<float>(0.5f, kFltMax, kFltMax, kSamples),
              HasMomentsNear(0.5, 0.04, std::sqrt(0.5), 0.05));
}

TEST(BetaDistributionTest, ZeroParametersSampleDegenerateLimits) {
  constexpr int kSamples = 20000;
  EXPECT_THAT(SampleScaledMoments<double>(0.0, 1.0, 1.0, kSamples),
              IsBernoulliNear(0.0, 0.0));
  EXPECT_THAT(SampleScaledMoments<double>(1.0, 0.0, 1.0, kSamples),
              IsBernoulliNear(1.0, 0.0));
  EXPECT_THAT(SampleScaledMoments<double>(0.0, 0.0, 1.0, kSamples),
              IsBernoulliNear(0.5, 0.015));
}

TEST(BetaDistributionTest, JoehnkSubnormalPowUsesLogSpace) {
  // Subnormal quantization on the direct pow path (1/alpha = 400 <= 500):
  // u^400 == v^400 == DBL_TRUE_MIN, so direct x / (x + y) would return 0.5
  // instead of ~1/3 from log space.
  EXPECT_THAT(
      SampleBetaWithBits<double>(
          0.0025, 0.0025, {0x27bf0fef76a3ce00ull, 0x27d0b5a68fa3ee00ull}),
      DoubleNear(0.33333333333332027, 1e-12));

  // Direct-pow bypass for large exponents (1/alpha = 1000 > 500):
  // u^1000 == 0.0 and v^1000 == DBL_TRUE_MIN, so log space yields ~0.30098.
  EXPECT_THAT(SampleBetaWithBits<double>(
                  0.001, 0.001, {0x7978d4fdf3b645a2ull, 0x79930be0ded288ceull}),
              DoubleNear(0.30097958588233603, 1e-12));
}

TEST(BetaDistributionTest, JoehnkEqualDrawsAvoidLogCancellation) {
  // x - z cancellation: equal draws u == v == 0.5 must return 0.5 rather than
  // 1.0 when |x| exceeds float/double precision relative to log(2).
  EXPECT_THAT(SampleBetaWithBits<float>(
                  1e-7f, 1e-7f, {0x8000000000000000ull, 0x8000000000000000ull}),
              FloatEq(0.5f));
  EXPECT_THAT(SampleBetaWithBits<double>(
                  1e-16, 1e-16, {0x8000000000000000ull, 0x8000000000000000ull}),
              DoubleEq(0.5));
}

TEST(BetaDistributionTest, JoehnkSkewedParametersPreserveMoments) {
  // Skewed Joehnk: pow(v, 1/1e18) rounds to 1.0, so the direct path must not
  // accept x + y <= 1.0. Scaled by 1e18, mean is 0.001 and stddev sqrt(0.001).
  EXPECT_THAT(SampleScaledMoments<double>(0.001, 1e18, 1e18, 50000),
              HasMomentsNear(0.001, 8e-4, std::sqrt(0.001), 0.03));
}

TEST(BetaDistributionTest, TinyParametersNearUnderflowFollowBernoulliRatio) {
  constexpr int kSamples = 20000;

  // Near-underflow tiny parameters: no -inf rejection truncation bias (true
  // mean 1/11 ~ 0.0909, not 0.1374).
  EXPECT_THAT(SampleScaledMoments<double>(6e-309, 6e-308, 1.0, kSamples),
              IsBernoulliNear(1.0 / 11.0, 0.01));

  // Unequal DEGENERATE_SMALL parameters follow Bernoulli(alpha / (alpha+beta))
  // rather than collapsing to deterministic 0.0 / 1.0.
  EXPECT_THAT(SampleScaledMoments<double>(1e-310, 2e-310, 1.0, kSamples),
              IsBernoulliNear(1.0 / 3.0, 0.015));
  EXPECT_THAT(SampleScaledMoments<double>(2e-310, 1e-310, 1.0, kSamples),
              IsBernoulliNear(2.0 / 3.0, 0.015));
}

TEST(BetaDistributionTest, LongDoubleSmoke) {
  if (absl::numeric_internal::IsDoubleDouble()) {
    GTEST_SKIP() << "Skipping long double test on double-double platforms";
  }
  absl::random_internal::pcg64_2018_engine rng(0x2B7E151628AED2A6ULL);
  const auto draw = [&rng](long double a, long double b) {
    absl::beta_distribution<long double> dist(a, b);
    return dist(rng);
  };
  const auto valid_unit = AllOf(Ge(0.0L), Le(1.0L));
  EXPECT_THAT(draw(0.1L, 0.2L), valid_unit);
  EXPECT_THAT(draw(0.5L, 2.0L), valid_unit);
  EXPECT_THAT(draw(2.0L, 5.0L), valid_unit);
  EXPECT_THAT(static_cast<double>(draw(1e20L, 1e20L)), DoubleNear(0.5, 1e-8));
  // Highly skewed Cheng BB: 1e30 * Beta(2, 1e30) ~ Gamma(2, 1).
  EXPECT_THAT(SampleScaledMoments<long double>(2.0L, 1e30L, 1e30, 20000),
              HasMomentsNear(2.0, 0.06, std::sqrt(2.0), 0.06));
  if constexpr (std::numeric_limits<long double>::min_exponent <= -16381) {
    // Non-zero long double Bernoulli probability that rounds to 0.0 in double
    // must return the endpoint without consuming a URBG draw.
    const long double tiny = std::ldexp(1.0L, -11000);
    const long double larger = std::ldexp(1.0L, -9000);
    EXPECT_THAT(SampleBetaWithBitsAndCount<long double>(tiny, larger, {0ull}),
                Pair(Eq(0.0L), 0u));
    EXPECT_THAT(SampleBetaWithBitsAndCount<long double>(larger, tiny, {0ull}),
                Pair(Eq(1.0L), 0u));
  }
}

// Direct tests of the out-of-line rejection steps. Cheng states below use the
// real (a, b, y) for the parameters unless noted; u1 is chosen to hit a
// specific v = y * log(u1 / (1 - u1)).
using ::absl::random_internal::BetaChengState;
using ::absl::random_internal::BetaChengStep;
using ::absl::random_internal::BetaJoehnkStep;
using ::absl::random_internal::MakeBetaChengState;

// Returns u1 such that y * log(u1 / (1 - u1)) ~= v.
double U1ForV(const BetaChengState<double>& state, double v) {
  return 1.0 / (1.0 + std::exp(-v / state.y));
}

// Mirrors BetaChengStep's computation of v from u1.
double ChengV(const BetaChengState<double>& state, double u1) {
  return state.y * std::log(u1 / (1.0 - u1));
}

// Cheng's exact log acceptance ratio, evaluated in long double.
long double ChengLhs(const BetaChengState<double>& state, double u1) {
  const long double logit = std::log(u1 / (1.0 - u1));
  const long double v = ChengV(state, u1);
  const long double a = state.a;
  const long double b = state.b;
  const long double u = a / (a + b) * std::expm1(v);
  return -(a + b) * std::log1p(u) + a * v + logit - std::log(4.0L);
}

// Returns BetaChengStep(u1, u2) for u2 chosen so log(u1 * u1 * u2) is
// lhs - 1e-9 (must accept) and lhs + 1e-9 (must reject).
std::pair<double, double> ChengDecisionsNearLhs(
    const BetaChengState<double>& state, double v) {
  const double u1 = U1ForV(state, v);
  const long double lhs = ChengLhs(state, u1);
  const long double u1_sq = static_cast<long double>(u1) * u1;
  const double u2_accept = static_cast<double>(std::exp(lhs - 1e-9L) / u1_sq);
  const double u2_reject = static_cast<double>(std::exp(lhs + 1e-9L) / u1_sq);
  return {BetaChengStep(u1, u2_accept, state),
          BetaChengStep(u1, u2_reject, state)};
}

struct ChengSample {
  double actual;
  long double expected;
};

void PrintTo(const ChengSample& s, std::ostream* os) {
  *os << absl::StreamFormat("{actual=%.17g, expected=%.17g}", s.actual,
                            static_cast<double>(s.expected));
}

// Returns an accepted BetaChengStep sample at v along with the closed form
// X = W / (b + W) (or b / (b + W) if inverted), W = a * exp(v).
ChengSample ChengSampleAt(const BetaChengState<double>& state,
                          double target_v) {
  const double u1 = U1ForV(state, target_v);
  const long double v = ChengV(state, u1);
  const long double w = static_cast<long double>(state.a) * std::exp(v);
  const long double b = state.b;
  // u2 = 1e-30 puts t = log(u1 * u1 * u2) far below lhs, forcing acceptance.
  return {BetaChengStep(u1, 1e-30, state),
          state.inverted ? b / (b + w) : w / (b + w)};
}

MATCHER_P(MatchesClosedForm, rel_tol,
          absl::StrFormat("is accepted and within %.3g relative of the "
                          "closed form",
                          rel_tol)) {
  const long double err = std::abs(arg.actual - arg.expected);
  return arg.actual >= 0 && err <= rel_tol * arg.expected;
}

TEST(BetaChengStepTest, SubnormalAFracHugeExpm1Rejects) {
  constexpr double kDblMax = (std::numeric_limits<double>::max)();
  // Beta(0.002, 0.9 * max) is Cheng BA with y = 500 and a / (a + b) < DBL_MIN.
  // At v ~ 709, exp(v) > max / 4 takes the (a * (expm1(v) / b)) branch; the
  // target density there is negligible, so the step must reject (not NaN).
  const BetaChengState<double> state =
      MakeBetaChengState(0.002, 0.9 * kDblMax, 500.0, /*use_algorithm_ba=*/true,
                         /*inverted=*/false);
  ASSERT_TRUE(state.subnormal_a_frac);
  EXPECT_THAT(BetaChengStep(U1ForV(state, 709.0), 0.5, state), DoubleEq(-1));
  const BetaChengState<double> inverted =
      MakeBetaChengState(0.002, 0.9 * kDblMax, 500.0, /*use_algorithm_ba=*/true,
                         /*inverted=*/true);
  EXPECT_THAT(BetaChengStep(U1ForV(inverted, 709.0), 0.5, inverted),
              DoubleEq(-1));
}

TEST(BetaChengStepTest, SubnormalAFracAcceptsSubnormalSample) {
  constexpr double kDblMax = (std::numeric_limits<double>::max)();
  // Accepted |v| > 0.5 draw with a / (a + b) < DBL_MIN: the subnormal result
  // a * exp(v) / b must not be quantized through a subnormal a_frac.
  const BetaChengState<double> state =
      MakeBetaChengState(0.002, 0.9 * kDblMax, 500.0, /*use_algorithm_ba=*/true,
                         /*inverted=*/false);
  EXPECT_THAT(ChengSampleAt(state, 1.6), MatchesClosedForm(1e-9));
}

TEST(BetaChengStepTest, Log1pOverflowGuardRejects) {
  constexpr double kDblMax = (std::numeric_limits<double>::max)();
  // Hand-built state (y = 709.5 / 30, beyond any real Cheng y) reaching
  // v = 709.5: u ~ 0.83, so log1p(u) > (max / 4) / b and the step must reject
  // before evaluating b * log1p(u) * (1 + r).
  const BetaChengState<double> state =
      MakeBetaChengState(1.0, 0.9 * kDblMax, 709.5 / 30.0,
                         /*use_algorithm_ba=*/false, /*inverted=*/false);
  EXPECT_THAT(BetaChengStep(U1ForV(state, 709.5), 0.5, state), DoubleEq(-1));
}

TEST(BetaChengStepTest, OverflowingExpRejects) {
  // Beta(0.002, 1) is Cheng BA with y = 500: v = 715 overflows exp(v).
  const BetaChengState<double> state =
      MakeBetaChengState(0.002, 1.0, 500.0, /*use_algorithm_ba=*/true,
                         /*inverted=*/false);
  EXPECT_THAT(BetaChengStep(U1ForV(state, 715.0), 0.5, state), DoubleEq(-1));
}

TEST(BetaChengStepTest, ExactAcceptanceMatchesLongDoubleNearSplices) {
  const auto kAcceptsThenRejects = Pair(Ge(0.0), DoubleEq(-1.0));
  // Beta(10, 10) exercises the non-precise (a <= 1e4, log1p(u)) exact
  // acceptance path for positive and negative v near u = +-0.25.
  const BetaChengState<double> bb =
      MakeBetaChengState(10.0, 10.0, std::sqrt(0.1),
                         /*use_algorithm_ba=*/false, /*inverted=*/false);
  EXPECT_THAT(ChengDecisionsNearLhs(bb, std::log(1.5) - 1e-6),
              kAcceptsThenRejects);
  EXPECT_THAT(ChengDecisionsNearLhs(bb, std::log(1.5) + 1e-6),
              kAcceptsThenRejects);
  EXPECT_THAT(ChengDecisionsNearLhs(bb, std::log(0.5) - 1e-6),
              kAcceptsThenRejects);
  EXPECT_THAT(ChengDecisionsNearLhs(bb, std::log(0.5) + 1e-6),
              kAcceptsThenRejects);
  // Beta(2e4, 2e4) (a > 1e4) switches a * (expm1(v) - v) between its series
  // and direct forms at |v| = 0.25.
  const BetaChengState<double> large =
      MakeBetaChengState(2e4, 2e4, std::sqrt((4e4 - 2) / (8e8 - 4e4)),
                         /*use_algorithm_ba=*/false, /*inverted=*/false);
  EXPECT_THAT(ChengDecisionsNearLhs(large, -0.25 + 1e-6), kAcceptsThenRejects);
  EXPECT_THAT(ChengDecisionsNearLhs(large, -0.25 - 1e-6), kAcceptsThenRejects);
}

TEST(BetaChengStepTest, SampleMatchesClosedFormAcrossBranches) {
  // Beta(0.5, 2) is Cheng BA with y = 2 and a_frac = 0.2. The non-inverted
  // output switches from a_frac * exp(v) / (1 + u) to 1 - b_frac / (1 + u) at
  // u = 0.2 * expm1(v) = 1, i.e. v = log(6).
  for (bool inverted : {false, true}) {
    const BetaChengState<double> ba =
        MakeBetaChengState(0.5, 2.0, 2.0, /*use_algorithm_ba=*/true, inverted);
    EXPECT_THAT(ChengSampleAt(ba, std::log(6.0) - 1e-6),
                MatchesClosedForm(1e-15));
    EXPECT_THAT(ChengSampleAt(ba, std::log(6.0) + 1e-6),
                MatchesClosedForm(1e-15));
    EXPECT_THAT(ChengSampleAt(ba, 0.5), MatchesClosedForm(1e-15));
    EXPECT_THAT(ChengSampleAt(ba, -0.5), MatchesClosedForm(1e-15));
  }
  // Beta(2e4, 2e4) (a > 1e4) adds the offset from a / (a + b) explicitly.
  const BetaChengState<double> large =
      MakeBetaChengState(2e4, 2e4, std::sqrt((4e4 - 2) / (8e8 - 4e4)),
                         /*use_algorithm_ba=*/false, /*inverted=*/false);
  EXPECT_THAT(ChengSampleAt(large, 0.01), MatchesClosedForm(1e-15));
  EXPECT_THAT(ChengSampleAt(large, -0.01), MatchesClosedForm(1e-15));
}

TEST(BetaJoehnkStepTest, DirectPowSumRoundingToOneUsesLogSpace) {
  // alpha = beta = 1, so x = u and y = v exactly; both sums round to 1.0.
  const double kTwoM30 = std::ldexp(1.0, -30);
  const double v = 1.0 - kTwoM30;
  // u + v = 1 + 2^-53 > 1 (ties to 1.0): must reject.
  EXPECT_THAT(BetaJoehnkStep(kTwoM30 + std::ldexp(1.0, -53), v, 1.0, 1.0, 1.0,
                             1.0, /*try_direct_pow=*/true),
              DoubleEq(-1));
  // u + v = 1 - 2^-54 < 1 (ties to 1.0): must accept u / (u + v).
  const double u = kTwoM30 - std::ldexp(1.0, -54);
  EXPECT_THAT(BetaJoehnkStep(u, v, 1.0, 1.0, 1.0, 1.0, /*try_direct_pow=*/true),
              DoubleNear(u / (u + v), 1e-12 * u));
}

TEST(BetaJoehnkStepTest, OneSidedInfiniteLogReturnsEndpoint) {
  // log(0.5) / 1e-310 overflows to -inf while the other side stays finite.
  constexpr double kTiny = 1e-310;
  EXPECT_THAT(BetaJoehnkStep(0.5, 0.9, kTiny, 1.0, 1.0 / kTiny, 1.0,
                             /*try_direct_pow=*/false),
              DoubleEq(0.0));
  EXPECT_THAT(BetaJoehnkStep(0.9, 0.5, 1.0, kTiny, 1.0, 1.0 / kTiny,
                             /*try_direct_pow=*/false),
              DoubleEq(1.0));
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

TEST(BetaDistributionTest, StreamRejectsInvalidParameters) {
  using absl::random_internal::ParseFromStream;
  const absl::beta_distribution<double> kInitial(2.0, 3.0);
  const absl::beta_distribution<float> kFloatInitial(2.0f, 3.0f);

  EXPECT_THAT(ParseFromStream(kInitial, ""), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-1.0 2.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2.0 -1.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "nan 2.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2.0 nan"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "inf 2.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2.0 inf"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "-inf 2.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2.0 -inf"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "1e500 3.0"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kInitial, "2.0 1e500"), FailsLeavingUnchanged());
  EXPECT_THAT(ParseFromStream(kFloatInitial, "1e40 1.0"),
              FailsLeavingUnchanged());

  // Non-finite values delivered with goodbit by a custom num_get facet.
  EXPECT_THAT(
      ParseFromStream(kInitial, "nan 2.0", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kInitial, "2.0 nan", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kInitial, "inf 2.0", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kInitial, "2.0 inf", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kInitial, "-inf 2.0", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kInitial, "2.0 -inf", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kFloatInitial, "nan 2.0", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());
  EXPECT_THAT(
      ParseFromStream(kFloatInitial, "2.0 inf", /*use_non_finite_facet=*/true),
      FailsLeavingUnchanged());

  EXPECT_THAT(ParseFromStream(kInitial, "0.0 0.0"),
              ParsesEqualTo(absl::beta_distribution<double>(0.0, 0.0)));
  EXPECT_THAT(ParseFromStream(kInitial, "1.5 2.5"),
              ParsesEqualTo(absl::beta_distribution<double>(1.5, 2.5)));
}

}  // namespace
