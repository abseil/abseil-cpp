#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <thread>  // NOLINT

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/strings/internal/str_format/bind.h"
#include "absl/types/optional.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {
namespace {

template <typename T, size_t N>
size_t ArraySize(T (&)[N]) {
  return N;
}

std::string LengthModFor(float) { return ""; }
std::string LengthModFor(double) { return ""; }
std::string LengthModFor(long double) { return "L"; }
std::string LengthModFor(char) { return "hh"; }
std::string LengthModFor(signed char) { return "hh"; }
std::string LengthModFor(unsigned char) { return "hh"; }
std::string LengthModFor(short) { return "h"; }           // NOLINT
std::string LengthModFor(unsigned short) { return "h"; }  // NOLINT
std::string LengthModFor(int) { return ""; }
std::string LengthModFor(unsigned) { return ""; }
std::string LengthModFor(long) { return "l"; }                 // NOLINT
std::string LengthModFor(unsigned long) { return "l"; }        // NOLINT
std::string LengthModFor(long long) { return "ll"; }           // NOLINT
std::string LengthModFor(unsigned long long) { return "ll"; }  // NOLINT

std::string EscCharImpl(int v) {
  if (std::isprint(static_cast<unsigned char>(v))) {
    return std::string(1, static_cast<char>(v));
  }
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "\\%#.2x",
                   static_cast<unsigned>(v & 0xff));
  assert(n > 0 && n < sizeof(buf));
  return std::string(buf, n);
}

std::string Esc(char v) { return EscCharImpl(v); }
std::string Esc(signed char v) { return EscCharImpl(v); }
std::string Esc(unsigned char v) { return EscCharImpl(v); }

template <typename T>
std::string Esc(const T &v) {
  std::ostringstream oss;
  oss << v;
  return oss.str();
}

void StrAppendV(std::string *dst, const char *format, va_list ap) {
  // First try with a small fixed size buffer
  static const int kSpaceLength = 1024;
  char space[kSpaceLength];

  // It's possible for methods that use a va_list to invalidate
  // the data in it upon use.  The fix is to make a copy
  // of the structure before using it and use that copy instead.
  va_list backup_ap;
  va_copy(backup_ap, ap);
  int result = vsnprintf(space, kSpaceLength, format, backup_ap);
  va_end(backup_ap);
  if (result < kSpaceLength) {
    if (result >= 0) {
      // Normal case -- everything fit.
      dst->append(space, result);
      return;
    }
    if (result < 0) {
      // Just an error.
      return;
    }
  }

  // Increase the buffer size to the size requested by vsnprintf,
  // plus one for the closing \0.
  int length = result + 1;
  char *buf = new char[length];

  // Restore the va_list before we use it again
  va_copy(backup_ap, ap);
  result = vsnprintf(buf, length, format, backup_ap);
  va_end(backup_ap);

  if (result >= 0 && result < length) {
    // It fit
    dst->append(buf, result);
  }
  delete[] buf;
}

void StrAppend(std::string *out, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  StrAppendV(out, format, ap);
  va_end(ap);
}

std::string StrPrint(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  std::string result;
  StrAppendV(&result, format, ap);
  va_end(ap);
  return result;
}

class FormatConvertTest : public ::testing::Test { };

template <typename T>
void TestStringConvert(const T& str) {
  const FormatArgImpl args[] = {FormatArgImpl(str)};
  struct Expectation {
    const char *out;
    const char *fmt;
  };
  const Expectation kExpect[] = {
    {"hello",  "%1$s"      },
    {"",       "%1$.s"     },
    {"",       "%1$.0s"    },
    {"h",      "%1$.1s"    },
    {"he",     "%1$.2s"    },
    {"hello",  "%1$.10s"   },
    {" hello", "%1$6s"     },
    {"   he",  "%1$5.2s"   },
    {"he   ",  "%1$-5.2s"  },
    {"hello ", "%1$-6.10s" },
  };
  for (const Expectation &e : kExpect) {
    UntypedFormatSpecImpl format(e.fmt);
    EXPECT_EQ(e.out, FormatPack(format, absl::MakeSpan(args)));
  }
}

TEST_F(FormatConvertTest, BasicString) {
  TestStringConvert("hello");  // As char array.
  TestStringConvert(static_cast<const char*>("hello"));
  TestStringConvert(std::string("hello"));
  TestStringConvert(string_view("hello"));
}

TEST_F(FormatConvertTest, NullString) {
  const char* p = nullptr;
  UntypedFormatSpecImpl format("%s");
  EXPECT_EQ("", FormatPack(format, {FormatArgImpl(p)}));
}

TEST_F(FormatConvertTest, StringPrecision) {
  // We cap at the precision.
  char c = 'a';
  const char* p = &c;
  UntypedFormatSpecImpl format("%.1s");
  EXPECT_EQ("a", FormatPack(format, {FormatArgImpl(p)}));

  // We cap at the NUL-terminator.
  p = "ABC";
  UntypedFormatSpecImpl format2("%.10s");
  EXPECT_EQ("ABC", FormatPack(format2, {FormatArgImpl(p)}));
}

// Pointer formatting is implementation defined. This checks that the argument
// can be matched to `ptr`.
MATCHER_P(MatchesPointerString, ptr, "") {
  if (ptr == nullptr && arg == "(nil)") {
    return true;
  }
  void* parsed = nullptr;
  if (sscanf(arg.c_str(), "%p", &parsed) != 1) {
    ABSL_RAW_LOG(FATAL, "Could not parse %s", arg.c_str());
  }
  return ptr == parsed;
}

TEST_F(FormatConvertTest, Pointer) {
  static int x = 0;
  const int *xp = &x;
  char c = 'h';
  char *mcp = &c;
  const char *cp = "hi";
  const char *cnil = nullptr;
  const int *inil = nullptr;
  using VoidF = void (*)();
  VoidF fp = [] {}, fnil = nullptr;
  volatile char vc;
  volatile char *vcp = &vc;
  volatile char *vcnil = nullptr;
  const FormatArgImpl args_array[] = {
      FormatArgImpl(xp),   FormatArgImpl(cp),  FormatArgImpl(inil),
      FormatArgImpl(cnil), FormatArgImpl(mcp), FormatArgImpl(fp),
      FormatArgImpl(fnil), FormatArgImpl(vcp), FormatArgImpl(vcnil),
  };
  auto args = absl::MakeConstSpan(args_array);

  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%20p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%.1p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%.20p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%30.20p"), args),
              MatchesPointerString(&x));

  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%-p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%-20p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%-.1p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%.20p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%-30.20p"), args),
              MatchesPointerString(&x));

  // const char*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%2$p"), args),
              MatchesPointerString(cp));
  // null const int*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%3$p"), args),
              MatchesPointerString(nullptr));
  // null const char*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%4$p"), args),
              MatchesPointerString(nullptr));
  // nonconst char*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%5$p"), args),
              MatchesPointerString(mcp));

  // function pointers
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%6$p"), args),
              MatchesPointerString(reinterpret_cast<const void*>(fp)));
  EXPECT_THAT(
      FormatPack(UntypedFormatSpecImpl("%8$p"), args),
      MatchesPointerString(reinterpret_cast<volatile const void *>(vcp)));

  // null function pointers
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%7$p"), args),
              MatchesPointerString(nullptr));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%9$p"), args),
              MatchesPointerString(nullptr));
}

struct Cardinal {
  enum Pos { k1 = 1, k2 = 2, k3 = 3 };
  enum Neg { kM1 = -1, kM2 = -2, kM3 = -3 };
};

TEST_F(FormatConvertTest, Enum) {
  const Cardinal::Pos k3 = Cardinal::k3;
  const Cardinal::Neg km3 = Cardinal::kM3;
  const FormatArgImpl args[] = {FormatArgImpl(k3), FormatArgImpl(km3)};
  UntypedFormatSpecImpl format("%1$d");
  UntypedFormatSpecImpl format2("%2$d");
  EXPECT_EQ("3", FormatPack(format, absl::MakeSpan(args)));
  EXPECT_EQ("-3", FormatPack(format2, absl::MakeSpan(args)));
}

template <typename T>
class TypedFormatConvertTest : public FormatConvertTest { };

TYPED_TEST_SUITE_P(TypedFormatConvertTest);

std::vector<std::string> AllFlagCombinations() {
  const char kFlags[] = {'-', '#', '0', '+', ' '};
  std::vector<std::string> result;
  for (size_t fsi = 0; fsi < (1ull << ArraySize(kFlags)); ++fsi) {
    std::string flag_set;
    for (size_t fi = 0; fi < ArraySize(kFlags); ++fi)
      if (fsi & (1ull << fi))
        flag_set += kFlags[fi];
    result.push_back(flag_set);
  }
  return result;
}

TYPED_TEST_P(TypedFormatConvertTest, AllIntsWithFlags) {
  typedef TypeParam T;
  typedef typename std::make_unsigned<T>::type UnsignedT;
  using remove_volatile_t = typename std::remove_volatile<T>::type;
  const T kMin = std::numeric_limits<remove_volatile_t>::min();
  const T kMax = std::numeric_limits<remove_volatile_t>::max();
  const T kVals[] = {
      remove_volatile_t(1),
      remove_volatile_t(2),
      remove_volatile_t(3),
      remove_volatile_t(123),
      remove_volatile_t(-1),
      remove_volatile_t(-2),
      remove_volatile_t(-3),
      remove_volatile_t(-123),
      remove_volatile_t(0),
      kMax - remove_volatile_t(1),
      kMax,
      kMin + remove_volatile_t(1),
      kMin,
  };
  const char kConvChars[] = {'d', 'i', 'u', 'o', 'x', 'X'};
  const std::string kWid[] = {"", "4", "10"};
  const std::string kPrec[] = {"", ".", ".0", ".4", ".10"};

  const std::vector<std::string> flag_sets = AllFlagCombinations();

  for (size_t vi = 0; vi < ArraySize(kVals); ++vi) {
    const T val = kVals[vi];
    SCOPED_TRACE(Esc(val));
    const FormatArgImpl args[] = {FormatArgImpl(val)};
    for (size_t ci = 0; ci < ArraySize(kConvChars); ++ci) {
      const char conv_char = kConvChars[ci];
      for (size_t fsi = 0; fsi < flag_sets.size(); ++fsi) {
        const std::string &flag_set = flag_sets[fsi];
        for (size_t wi = 0; wi < ArraySize(kWid); ++wi) {
          const std::string &wid = kWid[wi];
          for (size_t pi = 0; pi < ArraySize(kPrec); ++pi) {
            const std::string &prec = kPrec[pi];

            const bool is_signed_conv = (conv_char == 'd' || conv_char == 'i');
            const bool is_unsigned_to_signed =
                !std::is_signed<T>::value && is_signed_conv;
            // Don't consider sign-related flags '+' and ' ' when doing
            // unsigned to signed conversions.
            if (is_unsigned_to_signed &&
                flag_set.find_first_of("+ ") != std::string::npos) {
              continue;
            }

            std::string new_fmt("%");
            new_fmt += flag_set;
            new_fmt += wid;
            new_fmt += prec;
            // old and new always agree up to here.
            std::string old_fmt = new_fmt;
            new_fmt += conv_char;
            std::string old_result;
            if (is_unsigned_to_signed) {
              // don't expect agreement on unsigned formatted as signed,
              // as printf can't do that conversion properly. For those
              // cases, we do expect agreement with printf with a "%u"
              // and the unsigned equivalent of 'val'.
              UnsignedT uval = val;
              old_fmt += LengthModFor(uval);
              old_fmt += "u";
              old_result = StrPrint(old_fmt.c_str(), uval);
            } else {
              old_fmt += LengthModFor(val);
              old_fmt += conv_char;
              old_result = StrPrint(old_fmt.c_str(), val);
            }

            SCOPED_TRACE(std::string() + " old_fmt: \"" + old_fmt +
                         "\"'"
                         " new_fmt: \"" +
                         new_fmt + "\"");
            UntypedFormatSpecImpl format(new_fmt);
            EXPECT_EQ(old_result, FormatPack(format, absl::MakeSpan(args)));
          }
        }
      }
    }
  }
}

TYPED_TEST_P(TypedFormatConvertTest, Char) {
  typedef TypeParam T;
  using remove_volatile_t = typename std::remove_volatile<T>::type;
  static const T kMin = std::numeric_limits<remove_volatile_t>::min();
  static const T kMax = std::numeric_limits<remove_volatile_t>::max();
  T kVals[] = {
    remove_volatile_t(1), remove_volatile_t(2), remove_volatile_t(10),
    remove_volatile_t(-1), remove_volatile_t(-2), remove_volatile_t(-10),
    remove_volatile_t(0),
    kMin + remove_volatile_t(1), kMin,
    kMax - remove_volatile_t(1), kMax
  };
  for (const T &c : kVals) {
    const FormatArgImpl args[] = {FormatArgImpl(c)};
    UntypedFormatSpecImpl format("%c");
    EXPECT_EQ(StrPrint("%c", c), FormatPack(format, absl::MakeSpan(args)));
  }
}

REGISTER_TYPED_TEST_CASE_P(TypedFormatConvertTest, AllIntsWithFlags, Char);

typedef ::testing::Types<
    int, unsigned, volatile int,
    short, unsigned short,
    long, unsigned long,
    long long, unsigned long long,
    signed char, unsigned char, char>
    AllIntTypes;
INSTANTIATE_TYPED_TEST_CASE_P(TypedFormatConvertTestWithAllIntTypes,
                              TypedFormatConvertTest, AllIntTypes);
TEST_F(FormatConvertTest, VectorBool) {
  // Make sure vector<bool>'s values behave as bools.
  std::vector<bool> v = {true, false};
  const std::vector<bool> cv = {true, false};
  EXPECT_EQ("1,0,1,0",
            FormatPack(UntypedFormatSpecImpl("%d,%d,%d,%d"),
                       absl::Span<const FormatArgImpl>(
                           {FormatArgImpl(v[0]), FormatArgImpl(v[1]),
                            FormatArgImpl(cv[0]), FormatArgImpl(cv[1])})));
}


TEST_F(FormatConvertTest, Int128) {
  absl::int128 positive = static_cast<absl::int128>(0x1234567890abcdef) * 1979;
  absl::int128 negative = -positive;
  absl::int128 max = absl::Int128Max(), min = absl::Int128Min();
  const FormatArgImpl args[] = {FormatArgImpl(positive),
                                FormatArgImpl(negative), FormatArgImpl(max),
                                FormatArgImpl(min)};

  struct Case {
    const char* format;
    const char* expected;
  } cases[] = {
      {"%1$d", "2595989796776606496405"},
      {"%1$30d", "        2595989796776606496405"},
      {"%1$-30d", "2595989796776606496405        "},
      {"%1$u", "2595989796776606496405"},
      {"%1$x", "8cba9876066020f695"},
      {"%2$d", "-2595989796776606496405"},
      {"%2$30d", "       -2595989796776606496405"},
      {"%2$-30d", "-2595989796776606496405       "},
      {"%2$u", "340282366920938460867384810655161715051"},
      {"%2$x", "ffffffffffffff73456789f99fdf096b"},
      {"%3$d", "170141183460469231731687303715884105727"},
      {"%3$u", "170141183460469231731687303715884105727"},
      {"%3$x", "7fffffffffffffffffffffffffffffff"},
      {"%4$d", "-170141183460469231731687303715884105728"},
      {"%4$x", "80000000000000000000000000000000"},
  };

  for (auto c : cases) {
    UntypedFormatSpecImpl format(c.format);
    EXPECT_EQ(c.expected, FormatPack(format, absl::MakeSpan(args)));
  }
}

TEST_F(FormatConvertTest, Uint128) {
  absl::uint128 v = static_cast<absl::uint128>(0x1234567890abcdef) * 1979;
  absl::uint128 max = absl::Uint128Max();
  const FormatArgImpl args[] = {FormatArgImpl(v), FormatArgImpl(max)};

  struct Case {
    const char* format;
    const char* expected;
  } cases[] = {
      {"%1$d", "2595989796776606496405"},
      {"%1$30d", "        2595989796776606496405"},
      {"%1$-30d", "2595989796776606496405        "},
      {"%1$u", "2595989796776606496405"},
      {"%1$x", "8cba9876066020f695"},
      {"%2$d", "340282366920938463463374607431768211455"},
      {"%2$u", "340282366920938463463374607431768211455"},
      {"%2$x", "ffffffffffffffffffffffffffffffff"},
  };

  for (auto c : cases) {
    UntypedFormatSpecImpl format(c.format);
    EXPECT_EQ(c.expected, FormatPack(format, absl::MakeSpan(args)));
  }
}

TEST_F(FormatConvertTest, Float) {
#ifdef _MSC_VER
  // MSVC has a different rounding policy than us so we can't test our
  // implementation against the native one there.
  return;
#endif  // _MSC_VER

  const char *const kFormats[] = {
      "%",  "%.3", "%8.5", "%500",   "%.5000", "%.60", "%.30",   "%03",
      "%+", "% ",  "%-10", "%#15.3", "%#.0",   "%.0",  "%1$*2$", "%1$.*2$"};

  std::vector<double> doubles = {0.0,
                                 -0.0,
                                 .99999999999999,
                                 99999999999999.,
                                 std::numeric_limits<double>::max(),
                                 -std::numeric_limits<double>::max(),
                                 std::numeric_limits<double>::min(),
                                 -std::numeric_limits<double>::min(),
                                 std::numeric_limits<double>::lowest(),
                                 -std::numeric_limits<double>::lowest(),
                                 std::numeric_limits<double>::epsilon(),
                                 std::numeric_limits<double>::epsilon() + 1,
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity()};

  // Some regression tests.
  doubles.push_back(0.99999999999999989);

  if (std::numeric_limits<double>::has_denorm != std::denorm_absent) {
    doubles.push_back(std::numeric_limits<double>::denorm_min());
    doubles.push_back(-std::numeric_limits<double>::denorm_min());
  }

  for (double base :
       {1., 12., 123., 1234., 12345., 123456., 1234567., 12345678., 123456789.,
        1234567890., 12345678901., 123456789012., 1234567890123.}) {
    for (int exp = -123; exp <= 123; ++exp) {
      for (int sign : {1, -1}) {
        doubles.push_back(sign * std::ldexp(base, exp));
      }
    }
  }

  // Workaround libc bug.
  // https://sourceware.org/bugzilla/show_bug.cgi?id=22142
  const bool gcc_bug_22142 =
      StrPrint("%f", std::numeric_limits<double>::max()) !=
      "1797693134862315708145274237317043567980705675258449965989174768031"
      "5726078002853876058955863276687817154045895351438246423432132688946"
      "4182768467546703537516986049910576551282076245490090389328944075868"
      "5084551339423045832369032229481658085593321233482747978262041447231"
      "68738177180919299881250404026184124858368.000000";

  if (!gcc_bug_22142) {
    for (int exp = -300; exp <= 300; ++exp) {
      const double all_ones_mantissa = 0x1fffffffffffff;
      doubles.push_back(std::ldexp(all_ones_mantissa, exp));
    }
  }

  if (gcc_bug_22142) {
    for (auto &d : doubles) {
      using L = std::numeric_limits<double>;
      double d2 = std::abs(d);
      if (d2 == L::max() || d2 == L::min() || d2 == L::denorm_min()) {
        d = 0;
      }
    }
  }

  // Remove duplicates to speed up the logic below.
  std::sort(doubles.begin(), doubles.end());
  doubles.erase(std::unique(doubles.begin(), doubles.end()), doubles.end());

#ifndef __APPLE__
  // Apple formats NaN differently (+nan) vs. (nan)
  doubles.push_back(std::nan(""));
#endif

  // Reserve the space to ensure we don't allocate memory in the output itself.
  std::string str_format_result;
  str_format_result.reserve(1 << 20);
  std::string string_printf_result;
  string_printf_result.reserve(1 << 20);

  for (const char *fmt : kFormats) {
    for (char f : {'f', 'F',  //
                   'g', 'G',  //
                   'a', 'A',  //
                   'e', 'E'}) {
      std::string fmt_str = std::string(fmt) + f;

      if (fmt == absl::string_view("%.5000") && f != 'f' && f != 'F') {
        // This particular test takes way too long with snprintf.
        // Disable for the case we are not implementing natively.
        continue;
      }

      for (double d : doubles) {
        int i = -10;
        FormatArgImpl args[2] = {FormatArgImpl(d), FormatArgImpl(i)};
        UntypedFormatSpecImpl format(fmt_str);

        string_printf_result.clear();
        StrAppend(&string_printf_result, fmt_str.c_str(), d, i);
        str_format_result.clear();

        {
          AppendPack(&str_format_result, format, absl::MakeSpan(args));
        }

        if (string_printf_result != str_format_result) {
          // We use ASSERT_EQ here because failures are usually correlated and a
          // bug would print way too many failed expectations causing the test
          // to time out.
          ASSERT_EQ(string_printf_result, str_format_result)
              << fmt_str << " " << StrPrint("%.18g", d) << " "
              << StrPrint("%a", d) << " " << StrPrint("%.1080f", d);
        }
      }
    }
  }
}

TEST_F(FormatConvertTest, FloatRound) {
  std::string s;
  const auto format = [&](const char *fmt, double d) -> std::string & {
    s.clear();
    FormatArgImpl args[1] = {FormatArgImpl(d)};
    AppendPack(&s, UntypedFormatSpecImpl(fmt), absl::MakeSpan(args));
#if !defined(_MSC_VER)
    // MSVC has a different rounding policy than us so we can't test our
    // implementation against the native one there.
    EXPECT_EQ(StrPrint(fmt, d), s);
#endif  // _MSC_VER

    return s;
  };
  // All of these values have to be exactly represented.
  // Otherwise we might not be testing what we think we are testing.

  // These values can fit in a 64bit "fast" representation.
  const double exact_value = 0.00000000000005684341886080801486968994140625;
  assert(exact_value == std::pow(2, -44));
  // Round up at a 5xx.
  EXPECT_EQ(format("%.13f", exact_value), "0.0000000000001");
  // Round up at a >5
  EXPECT_EQ(format("%.14f", exact_value), "0.00000000000006");
  // Round down at a <5
  EXPECT_EQ(format("%.16f", exact_value), "0.0000000000000568");
  // Nine handling
  EXPECT_EQ(format("%.35f", exact_value),
            "0.00000000000005684341886080801486969");
  EXPECT_EQ(format("%.36f", exact_value),
            "0.000000000000056843418860808014869690");
  // Round down the last nine.
  EXPECT_EQ(format("%.37f", exact_value),
            "0.0000000000000568434188608080148696899");
  EXPECT_EQ(format("%.10f", 0.000003814697265625), "0.0000038147");
  // Round up the last nine
  EXPECT_EQ(format("%.11f", 0.000003814697265625), "0.00000381470");
  EXPECT_EQ(format("%.12f", 0.000003814697265625), "0.000003814697");

  // Round to even (down)
  EXPECT_EQ(format("%.43f", exact_value),
            "0.0000000000000568434188608080148696899414062");
  // Exact
  EXPECT_EQ(format("%.44f", exact_value),
            "0.00000000000005684341886080801486968994140625");
  // Round to even (up), let make the last digits 75 instead of 25
  EXPECT_EQ(format("%.43f", exact_value + std::pow(2, -43)),
            "0.0000000000001705302565824240446090698242188");
  // Exact, just to check.
  EXPECT_EQ(format("%.44f", exact_value + std::pow(2, -43)),
            "0.00000000000017053025658242404460906982421875");

  // This value has to be small enough that it won't fit in the uint128
  // representation for printing.
  const double small_exact_value =
      0.000000000000000000000000000000000000752316384526264005099991383822237233803945956334136013765601092018187046051025390625;  // NOLINT
  assert(small_exact_value == std::pow(2, -120));
  // Round up at a 5xx.
  EXPECT_EQ(format("%.37f", small_exact_value),
            "0.0000000000000000000000000000000000008");
  // Round down at a <5
  EXPECT_EQ(format("%.38f", small_exact_value),
            "0.00000000000000000000000000000000000075");
  // Round up at a >5
  EXPECT_EQ(format("%.41f", small_exact_value),
            "0.00000000000000000000000000000000000075232");
  // Nine handling
  EXPECT_EQ(format("%.55f", small_exact_value),
            "0.0000000000000000000000000000000000007523163845262640051");
  EXPECT_EQ(format("%.56f", small_exact_value),
            "0.00000000000000000000000000000000000075231638452626400510");
  EXPECT_EQ(format("%.57f", small_exact_value),
            "0.000000000000000000000000000000000000752316384526264005100");
  EXPECT_EQ(format("%.58f", small_exact_value),
            "0.0000000000000000000000000000000000007523163845262640051000");
  // Round down the last nine
  EXPECT_EQ(format("%.59f", small_exact_value),
            "0.00000000000000000000000000000000000075231638452626400509999");
  // Round up the last nine
  EXPECT_EQ(format("%.79f", small_exact_value),
            "0.000000000000000000000000000000000000"
            "7523163845262640050999913838222372338039460");

  // Round to even (down)
  EXPECT_EQ(format("%.119f", small_exact_value),
            "0.000000000000000000000000000000000000"
            "75231638452626400509999138382223723380"
            "394595633413601376560109201818704605102539062");
  // Exact
  EXPECT_EQ(format("%.120f", small_exact_value),
            "0.000000000000000000000000000000000000"
            "75231638452626400509999138382223723380"
            "3945956334136013765601092018187046051025390625");
  // Round to even (up), let make the last digits 75 instead of 25
  EXPECT_EQ(format("%.119f", small_exact_value + std::pow(2, -119)),
            "0.000000000000000000000000000000000002"
            "25694915357879201529997415146671170141"
            "183786900240804129680327605456113815307617188");
  // Exact, just to check.
  EXPECT_EQ(format("%.120f", small_exact_value + std::pow(2, -119)),
            "0.000000000000000000000000000000000002"
            "25694915357879201529997415146671170141"
            "1837869002408041296803276054561138153076171875");
}

// We don't actually store the results. This is just to exercise the rest of the
// machinery.
struct NullSink {
  friend void AbslFormatFlush(NullSink *sink, string_view str) {}
};

template <typename... T>
bool FormatWithNullSink(absl::string_view fmt, const T &... a) {
  NullSink sink;
  FormatArgImpl args[] = {FormatArgImpl(a)...};
  return FormatUntyped(&sink, UntypedFormatSpecImpl(fmt), absl::MakeSpan(args));
}

TEST_F(FormatConvertTest, ExtremeWidthPrecision) {
  for (const char *fmt : {"f"}) {
    for (double d : {1e-100, 1.0, 1e100}) {
      constexpr int max = std::numeric_limits<int>::max();
      EXPECT_TRUE(FormatWithNullSink(std::string("%.*") + fmt, max, d));
      EXPECT_TRUE(FormatWithNullSink(std::string("%1.*") + fmt, max, d));
      EXPECT_TRUE(FormatWithNullSink(std::string("%*") + fmt, max, d));
      EXPECT_TRUE(FormatWithNullSink(std::string("%*.*") + fmt, max, max, d));
    }
  }
}

TEST_F(FormatConvertTest, LongDouble) {
#ifdef _MSC_VER
  // MSVC has a different rounding policy than us so we can't test our
  // implementation against the native one there.
  return;
#endif  // _MSC_VER
  const char *const kFormats[] = {"%",    "%.3", "%8.5", "%9",  "%.5000",
                                  "%.60", "%+",  "% ",   "%-10"};

  std::vector<long double> doubles = {
      0.0,
      -0.0,
      std::numeric_limits<long double>::max(),
      -std::numeric_limits<long double>::max(),
      std::numeric_limits<long double>::min(),
      -std::numeric_limits<long double>::min(),
      std::numeric_limits<long double>::infinity(),
      -std::numeric_limits<long double>::infinity()};

  for (long double base : {1.L, 12.L, 123.L, 1234.L, 12345.L, 123456.L,
                           1234567.L, 12345678.L, 123456789.L, 1234567890.L,
                           12345678901.L, 123456789012.L, 1234567890123.L,
                           // This value is not representable in double, but it
                           // is in long double that uses the extended format.
                           // This is to verify that we are not truncating the
                           // value mistakenly through a double.
                           10000000000000000.25L}) {
    for (int exp : {-1000, -500, 0, 500, 1000}) {
      for (int sign : {1, -1}) {
        doubles.push_back(sign * std::ldexp(base, exp));
        doubles.push_back(sign / std::ldexp(base, exp));
      }
    }
  }

  // Regression tests
  //
  // Using a string literal because not all platforms support hex literals or it
  // might be out of range.
  doubles.push_back(std::strtold("-0xf.ffffffb5feafffbp-16324L", nullptr));

  for (const char *fmt : kFormats) {
    for (char f : {'f', 'F',  //
                   'g', 'G',  //
                   'a', 'A',  //
                   'e', 'E'}) {
      std::string fmt_str = std::string(fmt) + 'L' + f;

      if (fmt == absl::string_view("%.5000") && f != 'f' && f != 'F') {
        // This particular test takes way too long with snprintf.
        // Disable for the case we are not implementing natively.
        continue;
      }

      for (auto d : doubles) {
        FormatArgImpl arg(d);
        UntypedFormatSpecImpl format(fmt_str);
        // We use ASSERT_EQ here because failures are usually correlated and a
        // bug would print way too many failed expectations causing the test to
        // time out.
        ASSERT_EQ(StrPrint(fmt_str.c_str(), d), FormatPack(format, {&arg, 1}))
            << fmt_str << " " << StrPrint("%.18Lg", d) << " "
            << StrPrint("%La", d) << " " << StrPrint("%.1080Lf", d);
      }
    }
  }
}

TEST_F(FormatConvertTest, IntAsFloat) {
  const int kMin = std::numeric_limits<int>::min();
  const int kMax = std::numeric_limits<int>::max();
  const int ia[] = {
    1, 2, 3, 123,
    -1, -2, -3, -123,
    0, kMax - 1, kMax, kMin + 1, kMin };
  for (const int fx : ia) {
    SCOPED_TRACE(fx);
    const FormatArgImpl args[] = {FormatArgImpl(fx)};
    struct Expectation {
      int line;
      std::string out;
      const char *fmt;
    };
    const double dx = static_cast<double>(fx);
    const Expectation kExpect[] = {
      { __LINE__, StrPrint("%f", dx), "%f" },
      { __LINE__, StrPrint("%12f", dx), "%12f" },
      { __LINE__, StrPrint("%.12f", dx), "%.12f" },
      { __LINE__, StrPrint("%12a", dx), "%12a" },
      { __LINE__, StrPrint("%.12a", dx), "%.12a" },
    };
    for (const Expectation &e : kExpect) {
      SCOPED_TRACE(e.line);
      SCOPED_TRACE(e.fmt);
      UntypedFormatSpecImpl format(e.fmt);
      EXPECT_EQ(e.out, FormatPack(format, absl::MakeSpan(args)));
    }
  }
}

template <typename T>
bool FormatFails(const char* test_format, T value) {
  std::string format_string = std::string("<<") + test_format + ">>";
  UntypedFormatSpecImpl format(format_string);

  int one = 1;
  const FormatArgImpl args[] = {FormatArgImpl(value), FormatArgImpl(one)};
  EXPECT_EQ(FormatPack(format, absl::MakeSpan(args)), "")
      << "format=" << test_format << " value=" << value;
  return FormatPack(format, absl::MakeSpan(args)).empty();
}

TEST_F(FormatConvertTest, ExpectedFailures) {
  // Int input
  EXPECT_TRUE(FormatFails("%p", 1));
  EXPECT_TRUE(FormatFails("%s", 1));
  EXPECT_TRUE(FormatFails("%n", 1));

  // Double input
  EXPECT_TRUE(FormatFails("%p", 1.));
  EXPECT_TRUE(FormatFails("%s", 1.));
  EXPECT_TRUE(FormatFails("%n", 1.));
  EXPECT_TRUE(FormatFails("%c", 1.));
  EXPECT_TRUE(FormatFails("%d", 1.));
  EXPECT_TRUE(FormatFails("%x", 1.));
  EXPECT_TRUE(FormatFails("%*d", 1.));

  // String input
  EXPECT_TRUE(FormatFails("%n", ""));
  EXPECT_TRUE(FormatFails("%c", ""));
  EXPECT_TRUE(FormatFails("%d", ""));
  EXPECT_TRUE(FormatFails("%x", ""));
  EXPECT_TRUE(FormatFails("%f", ""));
  EXPECT_TRUE(FormatFails("%*d", ""));
}

}  // namespace
}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl
