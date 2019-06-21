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

#ifndef ABSL_RANDOM_INTERNAL_FAST_UNIFORM_BITS_H_
#define ABSL_RANDOM_INTERNAL_FAST_UNIFORM_BITS_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace absl {
namespace random_internal {
// Computes the length of the range of values producible by the URBG, or returns
// zero if that would encompass the entire range of representable values in
// URBG::result_type.
template <typename URBG>
constexpr typename URBG::result_type constexpr_range() {
  using result_type = typename URBG::result_type;
  return ((URBG::max)() == (std::numeric_limits<result_type>::max)() &&
          (URBG::min)() == std::numeric_limits<result_type>::lowest())
             ? result_type{0}
             : (URBG::max)() - (URBG::min)() + result_type{1};
}

// FastUniformBits implements a fast path to acquire uniform independent bits
// from a type which conforms to the [rand.req.urbg] concept.
// Parameterized by:
//  `UIntType`: the result (output) type
//  `Width`: binary output width
//
// The std::independent_bits_engine [rand.adapt.ibits] adaptor can be
// instantiated from an existing generator through a copy or a move. It does
// not, however, facilitate the production of pseudorandom bits from an un-owned
// generator that will outlive the std::independent_bits_engine instance.
template <typename UIntType = uint64_t,
          size_t Width = std::numeric_limits<UIntType>::digits>
class FastUniformBits {
  static_assert(std::is_unsigned<UIntType>::value,
                "Class-template FastUniformBits<> must be parameterized using "
                "an unsigned type.");

  // `kWidth` is the width, in binary digits, of the output. By default it is
  // the number of binary digits in the `result_type`.
  static constexpr size_t kWidth = Width;
  static_assert(kWidth > 0,
                "Class-template FastUniformBits<> Width argument must be > 0");

  static_assert(kWidth <= std::numeric_limits<UIntType>::digits,
                "Class-template FastUniformBits<> Width argument must be <= "
                "width of UIntType.");

  static constexpr bool kIsMaxWidth =
      (kWidth >= std::numeric_limits<UIntType>::digits);

  // Computes a mask of `n` bits for the `UIntType`.
  static constexpr UIntType constexpr_mask(size_t n) {
    return (UIntType(1) << n) - 1;
  }

 public:
  using result_type = UIntType;

  static constexpr result_type(min)() { return 0; }
  static constexpr result_type(max)() {
    return kIsMaxWidth ? (std::numeric_limits<result_type>::max)()
                       : constexpr_mask(kWidth);
  }

  template <typename URBG>
  result_type operator()(URBG& g);  // NOLINT(runtime/references)

 private:
  // Variate() generates a single random variate, always returning a value
  // in the closed interval [0 ... FastUniformBitsURBGConstants::kRangeMask]
  // (kRangeMask+1 is a power of 2).
  template <typename URBG>
  typename URBG::result_type Variate(URBG& g);  // NOLINT(runtime/references)

  // generate() generates a random value, dispatched on whether
  // the underlying URNG must loop over multiple calls or not.
  template <typename URBG>
  result_type Generate(URBG& g,  // NOLINT(runtime/references)
                       std::true_type /* avoid_looping */);

  template <typename URBG>
  result_type Generate(URBG& g,  // NOLINT(runtime/references)
                       std::false_type /* avoid_looping */);
};

// FastUniformBitsURBGConstants computes the URBG-derived constants used
// by FastUniformBits::Generate and FastUniformBits::Variate.
// Parameterized by the FastUniformBits parameter:
//   `URBG`: The underlying UniformRandomNumberGenerator.
//
// The values here indicate the URBG range as well as providing an indicator
// whether the URBG output is a power of 2, and kRangeMask, which allows masking
// the generated output to kRangeBits.
template <typename URBG>
class FastUniformBitsURBGConstants {
  // Computes the floor of the log. (i.e., std::floor(std::log2(N));
  static constexpr size_t constexpr_log2(size_t n) {
    return (n <= 1) ? 0 : 1 + constexpr_log2(n / 2);
  }

  // Computes a mask of n bits for the URBG::result_type.
  static constexpr typename URBG::result_type constexpr_mask(size_t n) {
    return (typename URBG::result_type(1) << n) - 1;
  }

 public:
  using result_type = typename URBG::result_type;

  // The range of the URNG, max - min + 1, or zero if that result would cause
  // overflow.
  static constexpr result_type kRange = constexpr_range<URBG>();

  static constexpr bool kPowerOfTwo =
      (kRange == 0) || ((kRange & (kRange - 1)) == 0);

  // kRangeBits describes the number number of bits suitable to mask off of URNG
  // variate, which is:
  // kRangeBits = floor(log2(kRange))
  static constexpr size_t kRangeBits =
      kRange == 0 ? std::numeric_limits<result_type>::digits
                  : constexpr_log2(kRange);

  // kRangeMask is the mask used when sampling variates from the URNG when the
  // width of the URNG range is not a power of 2.
  // Y = (2 ^ kRange) - 1
  static constexpr result_type kRangeMask =
      kRange == 0 ? (std::numeric_limits<result_type>::max)()
                  : constexpr_mask(kRangeBits);

  static_assert((URBG::max)() != (URBG::min)(),
                "Class-template FastUniformBitsURBGConstants<> "
                "URBG::max and URBG::min may not be equal.");

  static_assert(std::is_unsigned<result_type>::value,
                "Class-template FastUniformBitsURBGConstants<> "
                "URBG::result_type must be unsigned.");

  static_assert(kRangeMask > 0,
                "Class-template FastUniformBitsURBGConstants<> "
                "URBG does not generate sufficient random bits.");

  static_assert(kRange == 0 ||
                    kRangeBits < std::numeric_limits<result_type>::digits,
                "Class-template FastUniformBitsURBGConstants<> "
                "URBG range computation error.");
};

// FastUniformBitsLoopingConstants computes the looping constants used
// by FastUniformBits::Generate. These constants indicate how multiple
// URBG::result_type values are combined into an output_value.
// Parameterized by the FastUniformBits parameters:
//  `UIntType`: output type.
//  `Width`: binary output width,
//  `URNG`: The underlying UniformRandomNumberGenerator.
//
// The looping constants describe the sets of loop counters and mask values
// which control how individual variates are combined the final output.  The
// algorithm ensures that the number of bits used by any individual call differs
// by at-most one bit from any other call. This is simplified into constants
// which describe two loops, with the second loop parameters providing one extra
// bit per variate.
//
// See [rand.adapt.ibits] for more details on the use of these constants.
template <typename UIntType, size_t Width, typename URBG>
class FastUniformBitsLoopingConstants {
 private:
  static constexpr size_t kWidth = Width;
  using urbg_result_type = typename URBG::result_type;
  using uint_result_type = UIntType;

 public:
  using result_type =
      typename std::conditional<(sizeof(urbg_result_type) <=
                                 sizeof(uint_result_type)),
                                uint_result_type, urbg_result_type>::type;

 private:
  // Estimate N as ceil(width / urng width), and W0 as (width / N).
  static constexpr size_t kRangeBits =
      FastUniformBitsURBGConstants<URBG>::kRangeBits;

  // The range of the URNG, max - min + 1, or zero if that result would cause
  // overflow.
  static constexpr result_type kRange = constexpr_range<URBG>();
  static constexpr size_t kEstimateN =
      kWidth / kRangeBits + (kWidth % kRangeBits != 0);
  static constexpr size_t kEstimateW0 = kWidth / kEstimateN;
  static constexpr result_type kEstimateY0 = (kRange >> kEstimateW0)
                                             << kEstimateW0;

 public:
  // Parameters for the two loops:
  // kN0, kN1 are the number of underlying calls required for each loop.
  // KW0, kW1 are shift widths for each loop.
  //
  static constexpr size_t kN1 = (kRange - kEstimateY0) >
                                        (kEstimateY0 / kEstimateN)
                                    ? kEstimateN + 1
                                    : kEstimateN;
  static constexpr size_t kN0 = kN1 - (kWidth % kN1);
  static constexpr size_t kW0 = kWidth / kN1;
  static constexpr size_t kW1 = kW0 + 1;

  static constexpr result_type kM0 = (result_type(1) << kW0) - 1;
  static constexpr result_type kM1 = (result_type(1) << kW1) - 1;

  static_assert(
      kW0 <= kRangeBits,
      "Class-template FastUniformBitsLoopingConstants::kW0 too large.");

  static_assert(
      kW0 > 0,
      "Class-template FastUniformBitsLoopingConstants::kW0 too small.");
};

template <typename UIntType, size_t Width>
template <typename URBG>
typename FastUniformBits<UIntType, Width>::result_type
FastUniformBits<UIntType, Width>::operator()(
    URBG& g) {  // NOLINT(runtime/references)
  using constants = FastUniformBitsURBGConstants<URBG>;
  return Generate(
      g, std::integral_constant<bool, constants::kRangeMask >= (max)()>{});
}

template <typename UIntType, size_t Width>
template <typename URBG>
typename URBG::result_type FastUniformBits<UIntType, Width>::Variate(
    URBG& g) {  // NOLINT(runtime/references)
  using constants = FastUniformBitsURBGConstants<URBG>;
  if (constants::kPowerOfTwo) {
    return g() - (URBG::min)();
  }

  // Use rejection sampling to ensure uniformity across the range.
  typename URBG::result_type u;
  do {
    u = g() - (URBG::min)();
  } while (u > constants::kRangeMask);
  return u;
}

template <typename UIntType, size_t Width>
template <typename URBG>
typename FastUniformBits<UIntType, Width>::result_type
FastUniformBits<UIntType, Width>::Generate(
    URBG& g,  // NOLINT(runtime/references)
    std::true_type /* avoid_looping */) {
  // The width of the result_type is less than than the width of the random bits
  // provided by URNG.  Thus, generate a single value and then simply mask off
  // the required bits.
  return Variate(g) & (max)();
}

template <typename UIntType, size_t Width>
template <typename URBG>
typename FastUniformBits<UIntType, Width>::result_type
FastUniformBits<UIntType, Width>::Generate(
    URBG& g,  // NOLINT(runtime/references)
    std::false_type /* avoid_looping */) {
  // The width of the result_type is wider than the number of random bits
  // provided by URNG. Thus we merge several variates of URNG into the result
  // using a shift and mask.  The constants type generates the parameters used
  // ensure that the bits are distributed across all the invocations of the
  // underlying URNG.
  using constants = FastUniformBitsLoopingConstants<UIntType, Width, URBG>;

  result_type s = 0;
  for (size_t n = 0; n < constants::kN0; ++n) {
    auto u = Variate(g);
    s = (s << constants::kW0) + (u & constants::kM0);
  }
  for (size_t n = constants::kN0; n < constants::kN1; ++n) {
    auto u = Variate(g);
    s = (s << constants::kW1) + (u & constants::kM1);
  }
  return s;
}

}  // namespace random_internal
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_FAST_UNIFORM_BITS_H_
