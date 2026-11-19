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

#include "absl/random/internal/iostream_state_saver.h"

#include <stdio.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using absl::random_internal::make_istream_state_saver;
using absl::random_internal::make_ostream_state_saver;
using absl::random_internal::stream_precision_helper;

template <typename T>
typename std::enable_if_t<std::is_integral_v<T>, T>  //
StreamRoundTrip(T t) {
  std::stringstream ss;
  {
    auto saver = make_ostream_state_saver(ss);
    ss.precision(stream_precision_helper<T>::kPrecision);
    ss << t;
  }
  T result = 0;
  {
    auto saver = make_istream_state_saver(ss);
    ss >> result;
  }
  EXPECT_FALSE(ss.fail())            //
      << ss.str() << " "             //
      << (ss.good() ? "good " : "")  //
      << (ss.bad() ? "bad " : "")    //
      << (ss.eof() ? "eof " : "")    //
      << (ss.fail() ? "fail " : "");

  return result;
}

template <typename T>
typename std::enable_if_t<std::is_floating_point_v<T>, T>  //
StreamRoundTrip(T t) {
  std::stringstream ss;
  {
    auto saver = make_ostream_state_saver(ss);
    ss.precision(stream_precision_helper<T>::kPrecision);
    ss << t;
  }
  T result = 0;
  {
    auto saver = make_istream_state_saver(ss);
    result = absl::random_internal::read_floating_point<T>(ss);
  }
  EXPECT_FALSE(ss.fail())            //
      << ss.str() << " "             //
      << (ss.good() ? "good " : "")  //
      << (ss.bad() ? "bad " : "")    //
      << (ss.eof() ? "eof " : "")    //
      << (ss.fail() ? "fail " : "");

  return result;
}

TEST(IOStreamStateSaver, BasicSaverState) {
  std::stringstream ss;
  ss.precision(2);
  ss.fill('x');
  ss.flags(std::ios_base::dec | std::ios_base::right);

  {
    auto saver = make_ostream_state_saver(ss);
    ss.precision(10);
    EXPECT_NE('x', ss.fill());
    EXPECT_EQ(10, ss.precision());
    EXPECT_NE(std::ios_base::dec | std::ios_base::right, ss.flags());

    ss << 1.23;
  }

  EXPECT_EQ('x', ss.fill());
  EXPECT_EQ(2, ss.precision());
  EXPECT_EQ(std::ios_base::dec | std::ios_base::right, ss.flags());
}

TEST(IOStreamStateSaver, RoundTripInts) {
  const uint64_t kUintValues[] = {
      0,
      1,
      static_cast<uint64_t>(-1),
      2,
      static_cast<uint64_t>(-2),

      1 << 7,
      1 << 8,
      1 << 16,
      1ull << 32,
      1ull << 50,
      1ull << 62,
      1ull << 63,

      (1 << 7) - 1,
      (1 << 8) - 1,
      (1 << 16) - 1,
      (1ull << 32) - 1,
      (1ull << 50) - 1,
      (1ull << 62) - 1,
      (1ull << 63) - 1,

      static_cast<uint64_t>(-(1 << 8)),
      static_cast<uint64_t>(-(1 << 16)),
      static_cast<uint64_t>(-(1ll << 32)),
      static_cast<uint64_t>(-(1ll << 50)),
      static_cast<uint64_t>(-(1ll << 62)),

      static_cast<uint64_t>(-(1 << 8) - 1),
      static_cast<uint64_t>(-(1 << 16) - 1),
      static_cast<uint64_t>(-(1ll << 32) - 1),
      static_cast<uint64_t>(-(1ll << 50) - 1),
      static_cast<uint64_t>(-(1ll << 62) - 1),
  };

  for (const uint64_t u : kUintValues) {
    EXPECT_EQ(u, StreamRoundTrip<uint64_t>(u));

    int64_t x = static_cast<int64_t>(u);
    EXPECT_EQ(x, StreamRoundTrip<int64_t>(x));

    double d = static_cast<double>(x);
    EXPECT_EQ(d, StreamRoundTrip<double>(d));

    float f = d;
    EXPECT_EQ(f, StreamRoundTrip<float>(f));
  }
}

TEST(IOStreamStateSaver, RoundTripFloats) {
  static_assert(
      stream_precision_helper<float>::kPrecision >= 9,
      "stream_precision_helper<float>::kPrecision should be at least 9");

  const float kValues[] = {
      1,
      std::nextafter(1.0f, 0.0f),  // 1 - epsilon
      std::nextafter(1.0f, 2.0f),  // 1 + epsilon

      1.0e+1f,
      1.0e-1f,
      1.0e+2f,
      1.0e-2f,
      1.0e+10f,
      1.0e-10f,

      0.00000051110000111311111111f,
      -0.00000051110000111211111111f,

      1.234678912345678912345e+6f,
      1.234678912345678912345e-6f,
      1.234678912345678912345e+30f,
      1.234678912345678912345e-30f,
      1.234678912345678912345e+38f,
      1.0234678912345678912345e-38f,

      // Boundary cases.
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::epsilon(),
      std::nextafter(std::numeric_limits<float>::min(),
                     1.0f),               // min + epsilon
      std::numeric_limits<float>::min(),  // smallest normal
      // There are some errors dealing with denorms on apple platforms.
      std::numeric_limits<float>::denorm_min(),  // smallest denorm
      std::numeric_limits<float>::min() / 2,
      std::nextafter(std::numeric_limits<float>::min(),
                     0.0f),  // denorm_max
      std::nextafter(std::numeric_limits<float>::denorm_min(), 1.0f),
  };

  for (const float f : kValues) {
    EXPECT_EQ(f, StreamRoundTrip<float>(f));
    EXPECT_EQ(-f, StreamRoundTrip<float>(-f));

    double d = f;
    EXPECT_EQ(d, StreamRoundTrip<double>(d));
    EXPECT_EQ(-d, StreamRoundTrip<double>(-d));

    // Avoid undefined behavior (overflow/underflow).
    if (f <= static_cast<float>(std::numeric_limits<int64_t>::max()) &&
        f >= static_cast<float>(std::numeric_limits<int64_t>::lowest())) {
      int64_t x = static_cast<int64_t>(f);
      EXPECT_EQ(x, StreamRoundTrip<int64_t>(x));
    }
  }
}

TEST(IOStreamStateSaver, RoundTripDoubles) {
  static_assert(
      stream_precision_helper<double>::kPrecision >= 17,
      "stream_precision_helper<double>::kPrecision should be at least 17");

  const double kValues[] = {
      1,
      std::nextafter(1.0, 0.0),  // 1 - epsilon
      std::nextafter(1.0, 2.0),  // 1 + epsilon

      1.0e+1,
      1.0e-1,
      1.0e+2,
      1.0e-2,
      1.0e+10,
      1.0e-10,

      0.00000051110000111311111111,
      -0.00000051110000111211111111,

      1.234678912345678912345e+6,
      1.234678912345678912345e-6,
      1.234678912345678912345e+30,
      1.234678912345678912345e-30,
      1.234678912345678912345e+38,
      1.0234678912345678912345e-38,

      1.0e+100,
      1.0e-100,
      1.234678912345678912345e+308,
      1.0234678912345678912345e-308,
      2.22507385850720138e-308,

      // Boundary cases.
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::epsilon(),
      std::nextafter(std::numeric_limits<double>::min(),
                     1.0),                 // min + epsilon
      std::numeric_limits<double>::min(),  // smallest normal
      // There are some errors dealing with denorms on apple platforms.
      std::numeric_limits<double>::denorm_min(),  // smallest denorm
      std::numeric_limits<double>::min() / 2,
      std::nextafter(std::numeric_limits<double>::min(),
                     0.0),  // denorm_max
      std::nextafter(std::numeric_limits<double>::denorm_min(), 1.0f),
  };

  for (const double d : kValues) {
    EXPECT_EQ(d, StreamRoundTrip<double>(d));
    EXPECT_EQ(-d, StreamRoundTrip<double>(-d));

    // Avoid undefined behavior (overflow/underflow).
    if (d <= std::numeric_limits<float>::max() &&
        d >= std::numeric_limits<float>::lowest()) {
      float f = static_cast<float>(d);
      EXPECT_EQ(f, StreamRoundTrip<float>(f));
    }

    // Avoid undefined behavior (overflow/underflow).
    if (d <= static_cast<double>(std::numeric_limits<int64_t>::max()) &&
        d >= static_cast<double>(std::numeric_limits<int64_t>::lowest())) {
      int64_t x = static_cast<int64_t>(d);
      EXPECT_EQ(x, StreamRoundTrip<int64_t>(x));
    }
  }
}

TEST(IOStreamStateSaver, RoundTripLongDoubles) {
  // Technically, C++ only guarantees that long double is at least as large as a
  // double.  Practically it varies from 64-bits to 128-bits.
  //
  // So it is best to consider long double a best-effort extended precision
  // type.

  static_assert(
      stream_precision_helper<long double>::kPrecision >= 36,
      "stream_precision_helper<long double>::kPrecision should be at least 36");

  using real_type = long double;
  const real_type kValues[] = {
      1,
      std::nextafter(1.0, 0.0),  // 1 - epsilon
      std::nextafter(1.0, 2.0),  // 1 + epsilon

      1.0e+1,
      1.0e-1,
      1.0e+2,
      1.0e-2,
      1.0e+10,
      1.0e-10,

      0.00000051110000111311111111,
      -0.00000051110000111211111111,

      1.2346789123456789123456789123456789e+6,
      1.2346789123456789123456789123456789e-6,
      1.2346789123456789123456789123456789e+30,
      1.2346789123456789123456789123456789e-30,
      1.2346789123456789123456789123456789e+38,
      1.2346789123456789123456789123456789e-38,
      1.2346789123456789123456789123456789e+308,
      1.2346789123456789123456789123456789e-308,

      1.0e+100,
      1.0e-100,
      1.234678912345678912345e+308,
      1.0234678912345678912345e-308,

      // Boundary cases.
      std::numeric_limits<real_type>::max(),
      std::numeric_limits<real_type>::lowest(),
      std::numeric_limits<real_type>::epsilon(),
      std::nextafter(std::numeric_limits<real_type>::min(),
                     real_type(1)),           // min + epsilon
      std::numeric_limits<real_type>::min(),  // smallest normal
      // There are some errors dealing with denorms on apple platforms.
      std::numeric_limits<real_type>::denorm_min(),  // smallest denorm
      std::numeric_limits<real_type>::min() / 2,
      std::nextafter(std::numeric_limits<real_type>::min(),
                     0.0),  // denorm_max
      std::nextafter(std::numeric_limits<real_type>::denorm_min(), 1.0f),
  };

  int index = -1;
  for (const long double dd : kValues) {
    index++;
    EXPECT_EQ(dd, StreamRoundTrip<real_type>(dd)) << index;
    EXPECT_EQ(-dd, StreamRoundTrip<real_type>(-dd)) << index;

    // Avoid undefined behavior (overflow/underflow).
    if (dd <= std::numeric_limits<double>::max() &&
        dd >= std::numeric_limits<double>::lowest()) {
      double d = static_cast<double>(dd);
      EXPECT_EQ(d, StreamRoundTrip<double>(d));
    }

    // Avoid undefined behavior (overflow/underflow).
    if (dd <= static_cast<long double>(std::numeric_limits<int64_t>::max()) &&
        dd >=
            static_cast<long double>(std::numeric_limits<int64_t>::lowest())) {
      int64_t x = static_cast<int64_t>(dd);
      EXPECT_EQ(x, StreamRoundTrip<int64_t>(x));
    }
  }
}

TEST(StrToDTest, DoubleMin) {
  const char kV[] = "2.22507385850720138e-308";
  char* end;
  double x = std::strtod(kV, &end);
  EXPECT_EQ(std::numeric_limits<double>::min(), x);
  // errno may equal ERANGE.
}

TEST(StrToDTest, DoubleDenormMin) {
  const char kV[] = "4.94065645841246544e-324";
  char* end;
  double x = std::strtod(kV, &end);
  EXPECT_EQ(std::numeric_limits<double>::denorm_min(), x);
  // errno may equal ERANGE.
}

template <typename FloatType>
struct FloatReadResult {
  bool ok = false;
  bool failed = false;
  FloatType out = 0;
  FloatType initial = 0;
};

template <typename FloatType>
void PrintTo(const FloatReadResult<FloatType>& r, std::ostream* os) {
  *os << "{ok=" << (r.ok ? "true" : "false")
      << ", failed=" << (r.failed ? "true" : "false") << ", out=" << r.out
      << "}";
}

template <typename FloatType>
FloatReadResult<FloatType> ReadFloat(const std::string& input) {
  std::istringstream is(input);
  const FloatType val =
      absl::random_internal::read_floating_point<FloatType>(is);
  return {!is.fail(), is.fail(), val, FloatType{0}};
}

template <typename IntType>
struct IntReadResult {
  bool ok = false;
  bool failed = false;
  IntType out;
  IntType initial;
};

template <typename IntType>
void PrintTo(const IntReadResult<IntType>& r, std::ostream* os) {
  using format_type =
      typename absl::random_internal::stream_format_type<IntType>::type;
  *os << "{ok=" << (r.ok ? "true" : "false")
      << ", failed=" << (r.failed ? "true" : "false")
      << ", out=" << static_cast<format_type>(r.out)
      << ", initial=" << static_cast<format_type>(r.initial) << "}";
}

template <typename IntType>
IntReadResult<IntType> ReadInt(const std::string& input, IntType sentinel) {
  IntType out = sentinel;
  std::istringstream is(input);
  const bool ok = absl::random_internal::read_integral(is, out);
  return {ok, is.fail(), out, sentinel};
}

template <typename IntType>
IntReadResult<IntType> ReadInt(const std::wstring& input, IntType sentinel) {
  IntType out = sentinel;
  std::wistringstream is(input);
  const bool ok = absl::random_internal::read_integral(is, out);
  return {ok, is.fail(), out, sentinel};
}

MATCHER(FailsPreservingOut,
        "fails extraction and leaves output equal to initial value") {
  return !arg.ok && arg.failed && arg.out == arg.initial;
}

MATCHER_P(ReadsValue, expected,
          "succeeds without failbit and extracts expected value") {
  return arg.ok && !arg.failed && arg.out == expected;
}

TEST(ReadFloatingPointTest, EmptyAndInvalidStreamsPreserveFailbit) {
  using absl::random_internal::read_floating_point;

  EXPECT_THAT(ReadFloat<double>(""), FailsPreservingOut());
  EXPECT_THAT(ReadFloat<double>("   "), FailsPreservingOut());
  EXPECT_THAT(ReadFloat<double>("invalid"), FailsPreservingOut());

  // Underflow to 0.0: preserve whatever failbit std::num_get sets (libc++ and
  // MSVC fail on ERANGE underflow; libstdc++ succeeds with 0.0).
  {
    std::istringstream raw("1e-400");
    double raw_val = 1.0;
    raw >> raw_val;

    std::istringstream is("1e-400");
    const double val = read_floating_point<double>(is);
    EXPECT_EQ(is.fail(), raw.fail());
    EXPECT_EQ(val, 0.0);
  }
  // Overflow: all standard libraries set failbit (assigning +inf or DBL_MAX).
  {
    std::istringstream is("1e400");
    const double val = read_floating_point<double>(is);
    EXPECT_TRUE(is.fail());
    EXPECT_TRUE(std::isinf(val) || val == (std::numeric_limits<double>::max)());
  }

  // Pre-failed stream returns 0 without consuming input or clearing failbit.
  {
    std::istringstream is("1.5");
    is.setstate(std::ios_base::failbit);
    const double val = read_floating_point<double>(is);
    EXPECT_TRUE(is.fail());
    EXPECT_EQ(val, 0.0);
    is.clear();
    double remaining = 0.0;
    is >> remaining;
    EXPECT_EQ(remaining, 1.5);
  }

  // Pre-failed badbit streams short-circuit, preserving badbit and failbit.
  {
    std::istream null_is(nullptr);
    const double val = read_floating_point<double>(null_is);
    EXPECT_TRUE(null_is.bad());
    EXPECT_TRUE(null_is.fail());
    EXPECT_EQ(val, 0.0);
  }
  {
    std::istringstream is("4.94065645841246544e-324");
    is.setstate(std::ios_base::badbit);
    const double val = read_floating_point<double>(is);
    EXPECT_TRUE(is.bad());
    EXPECT_TRUE(is.fail());
    EXPECT_EQ(val, 0.0);
  }

  // Subnormal and min values continue to succeed without failbit.
  EXPECT_THAT(ReadFloat<double>("4.94065645841246544e-324"),
              ReadsValue(std::numeric_limits<double>::denorm_min()));
}

TEST(ReadIntegralTest, UnsignedRejectsNegativeAndPreservesOut) {
  using absl::random_internal::read_integral;

  EXPECT_THAT(ReadInt<uint64_t>("", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint64_t>("   ", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint64_t>("-", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint64_t>("-0", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint64_t>("-1", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint64_t>("  -42", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint64_t>("abc", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint64_t>("99999999999999999999999", 77),
              FailsPreservingOut());

  EXPECT_THAT(ReadInt<uint8_t>("-0", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint8_t>("-1", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint8_t>("256", 77), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint8_t>("300", 77), FailsPreservingOut());

  EXPECT_THAT(ReadInt<bool>("-0", true), FailsPreservingOut());
  EXPECT_THAT(ReadInt<bool>("-1", true), FailsPreservingOut());
  EXPECT_THAT(ReadInt<bool>("2", true), FailsPreservingOut());

  EXPECT_THAT(ReadInt<uint32_t>(L"  -5", 99), FailsPreservingOut());
  EXPECT_THAT(ReadInt<uint32_t>(L"  42", 0), ReadsValue(42u));

  uint32_t out = 55;
  std::istream null_is(nullptr);
  EXPECT_FALSE(read_integral(null_is, out));
  EXPECT_TRUE(null_is.bad());
  EXPECT_EQ(out, 55u);
}

TEST(ReadIntegralTest, SignedAndNarrowBounds) {
  using absl::random_internal::read_integral;

  EXPECT_THAT(ReadInt<int8_t>("", 11), FailsPreservingOut());
  EXPECT_THAT(ReadInt<int8_t>("abc", 11), FailsPreservingOut());
  EXPECT_THAT(ReadInt<int8_t>("-129", 11), FailsPreservingOut());
  EXPECT_THAT(ReadInt<int8_t>("128", 11), FailsPreservingOut());
  EXPECT_THAT(ReadInt<int8_t>("-254", 11), FailsPreservingOut());
  EXPECT_THAT(ReadInt<int8_t>("258", 11), FailsPreservingOut());

  EXPECT_THAT(ReadInt<int8_t>("-128", 0), ReadsValue(int8_t{-128}));
  EXPECT_THAT(ReadInt<int8_t>("127", 0), ReadsValue(int8_t{127}));
  EXPECT_THAT(ReadInt<uint8_t>("0", 1), ReadsValue(uint8_t{0}));
  EXPECT_THAT(ReadInt<uint8_t>("255", 1), ReadsValue(uint8_t{255}));

  {
    int8_t lo = 0;
    int8_t hi = 0;
    std::istringstream is("-128 127");
    EXPECT_TRUE(read_integral(is, lo));
    EXPECT_TRUE(read_integral(is, hi));
    EXPECT_EQ(lo, -128);
    EXPECT_EQ(hi, 127);
  }
  {
    uint8_t lo = 1;
    uint8_t hi = 1;
    std::istringstream is("0 255");
    EXPECT_TRUE(read_integral(is, lo));
    EXPECT_TRUE(read_integral(is, hi));
    EXPECT_EQ(lo, 0u);
    EXPECT_EQ(hi, 255u);
  }
}

}  // namespace
