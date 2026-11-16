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

#include "absl/random/poisson_distribution.h"

#include <cassert>
#include <cmath>

#include "absl/base/config.h"
#include "absl/random/internal/fastmath.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

double PoissonLogRatioTerm(double x, double d, double mean) {
  const double s = x + mean;
  const double y = d / s;
  const double rem = StirlingLogFactorialRemainder(x);
  if (std::abs(y) <= 0.1) {
    // Use ln(mean / x) = -2 * (y + y^3/3 + y^5/5 + ...), where the linear
    // terms in (x + 0.5) * ln(mean / x) + (x - mean) cancel algebraically:
    //   -(d + 1) * y - (2 * x + 1) * y^3 * (1/3 + y^2/5 + ...).
    // For |y| <= 0.1, the omitted inner series remainder is
    // < 0.1^17 / (17 * 0.99) < 1e-18, giving relative truncation error
    // < 1e-16 (< 0.5 ULP) in g(x, mean) after scaling by 2 * x + 1.
    const double y2 = y * y;
    const double poly =
        (1.0 / 3.0) +
        y2 * ((1.0 / 5.0) +
              y2 * ((1.0 / 7.0) +
                    y2 * ((1.0 / 9.0) +
                          y2 * ((1.0 / 11.0) +
                                y2 * ((1.0 / 13.0) + y2 * (1.0 / 15.0))))));
    return -(d + 1.0) * y - (s + d + 1.0) * (y * y2) * poly - rem;
  }
  return (x + 0.5) * std::log(mean / x) + d - rem;
}

double PoissonLogRatio(double x, double d, double mean, double lmu,
                       double g_mode) {
  assert(x >= 0.0);
  if (x < 20.0) {
    // Exact ln(k!) for k = 0..19.
    static constexpr double kLogFactorial[20] = {
        0.0,
        0.0,
        0.69314718055994530942,
        1.79175946922805500081,
        3.17805383034794561965,
        4.78749174278204599425,
        6.57925121201010099506,
        8.52516136106541430017,
        10.60460290274525022842,
        12.80182748008146961121,
        15.10441257307551529523,
        17.50230784587388583929,
        19.98721449566188614952,
        22.55216385312342288557,
        25.19122118273868150009,
        27.89927138384089156609,
        30.67186010608067280376,
        33.50507345013688888401,
        36.39544520803305357622,
        39.33988418719949403622,
    };
    constexpr double kLog2PI = 1.83787706640934548356;
    return x * lmu - mean - kLogFactorial[static_cast<int>(x)] +
           0.5 * (kLog2PI + lmu) - g_mode;
  }
  return PoissonLogRatioTerm(x, d, mean) - g_mode;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
