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

#include "absl/random/internal/randen_engine.h"

#include <algorithm>
#include <bitset>
#include <random>
#include <sstream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/random/internal/explicit_seed_seq.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"

#define UPDATE_GOLDEN 0

using randen_u64 = absl::random_internal::randen_engine<uint64_t>;
using randen_u32 = absl::random_internal::randen_engine<uint32_t>;
using absl::random_internal::ExplicitSeedSeq;

namespace {

template <typename UIntType>
class RandenEngineTypedTest : public ::testing::Test {};

using UIntTypes = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;

TYPED_TEST_SUITE(RandenEngineTypedTest, UIntTypes);

TYPED_TEST(RandenEngineTypedTest, VerifyReseedChangesAllValues) {
  using randen = typename absl::random_internal::randen_engine<TypeParam>;
  using result_type = typename randen::result_type;

  const size_t kNumOutputs = (sizeof(randen) * 2 / sizeof(TypeParam)) + 1;
  randen engine;

  // MSVC emits error 2719 without the use of std::ref below.
  //  * formal parameter with __declspec(align('#')) won't be aligned

  {
    std::seed_seq seq1{1, 2, 3, 4, 5, 6, 7};
    engine.seed(seq1);
  }
  result_type a[kNumOutputs];
  std::generate(std::begin(a), std::end(a), std::ref(engine));

  {
    std::random_device rd;
    std::seed_seq seq2{rd(), rd(), rd()};
    engine.seed(seq2);
  }
  result_type b[kNumOutputs];
  std::generate(std::begin(b), std::end(b), std::ref(engine));

  // Test that generated sequence changed as sequence of bits, i.e. if about
  // half of the bites were flipped between two non-correlated values.
  size_t changed_bits = 0;
  size_t unchanged_bits = 0;
  size_t total_set = 0;
  size_t total_bits = 0;
  size_t equal_count = 0;
  for (size_t i = 0; i < kNumOutputs; ++i) {
    equal_count += (a[i] == b[i]) ? 1 : 0;
    std::bitset<sizeof(result_type) * 8> bitset(a[i] ^ b[i]);
    changed_bits += bitset.count();
    unchanged_bits += bitset.size() - bitset.count();

    std::bitset<sizeof(result_type) * 8> a_set(a[i]);
    std::bitset<sizeof(result_type) * 8> b_set(b[i]);
    total_set += a_set.count() + b_set.count();
    total_bits += 2 * 8 * sizeof(result_type);
  }
  // On average, half the bits are changed between two calls.
  EXPECT_LE(changed_bits, 0.60 * (changed_bits + unchanged_bits));
  EXPECT_GE(changed_bits, 0.40 * (changed_bits + unchanged_bits));

  // Verify using a quick normal-approximation to the binomial.
  EXPECT_NEAR(total_set, total_bits * 0.5, 4 * std::sqrt(total_bits))
      << "@" << total_set / static_cast<double>(total_bits);

  // Also, A[i] == B[i] with probability (1/range) * N.
  // Give this a pretty wide latitude, though.
  const double kExpected = kNumOutputs / (1.0 * sizeof(result_type) * 8);
  EXPECT_LE(equal_count, 1.0 + kExpected);
}

// Number of values that needs to be consumed to clean two sizes of buffer
// and trigger third refresh. (slightly overestimates the actual state size).
constexpr size_t kTwoBufferValues = sizeof(randen_u64) / sizeof(uint16_t) + 1;

TYPED_TEST(RandenEngineTypedTest, VerifyDiscard) {
  using randen = typename absl::random_internal::randen_engine<TypeParam>;

  for (size_t num_used = 0; num_used < kTwoBufferValues; ++num_used) {
    randen engine_used;
    for (size_t i = 0; i < num_used; ++i) {
      engine_used();
    }

    for (size_t num_discard = 0; num_discard < kTwoBufferValues;
         ++num_discard) {
      randen engine1 = engine_used;
      randen engine2 = engine_used;
      for (size_t i = 0; i < num_discard; ++i) {
        engine1();
      }
      engine2.discard(num_discard);
      for (size_t i = 0; i < kTwoBufferValues; ++i) {
        const auto r1 = engine1();
        const auto r2 = engine2();
        ASSERT_EQ(r1, r2) << "used=" << num_used << " discard=" << num_discard;
      }
    }
  }
}

TYPED_TEST(RandenEngineTypedTest, StreamOperatorsResult) {
  using randen = typename absl::random_internal::randen_engine<TypeParam>;
  std::wostringstream os;
  std::wistringstream is;
  randen engine;

  EXPECT_EQ(&(os << engine), &os);
  EXPECT_EQ(&(is >> engine), &is);
}

TYPED_TEST(RandenEngineTypedTest, StreamSerialization) {
  using randen = typename absl::random_internal::randen_engine<TypeParam>;

  for (size_t discard = 0; discard < kTwoBufferValues; ++discard) {
    ExplicitSeedSeq seed_sequence{12, 34, 56};
    randen engine(seed_sequence);
    engine.discard(discard);

    std::stringstream stream;
    stream << engine;

    randen new_engine;
    stream >> new_engine;
    for (size_t i = 0; i < 64; ++i) {
      EXPECT_EQ(engine(), new_engine()) << " " << i;
    }
  }
}

constexpr size_t kNumGoldenOutputs = 127;

// This test is checking if randen_engine is meets interface requirements
// defined in [rand.req.urbg].
TYPED_TEST(RandenEngineTypedTest, RandomNumberEngineInterface) {
  using randen = typename absl::random_internal::randen_engine<TypeParam>;

  using E = randen;
  using T = typename E::result_type;

  static_assert(std::is_copy_constructible<E>::value,
                "randen_engine must be copy constructible");

  static_assert(absl::is_copy_assignable<E>::value,
                "randen_engine must be copy assignable");

  static_assert(std::is_move_constructible<E>::value,
                "randen_engine must be move constructible");

  static_assert(absl::is_move_assignable<E>::value,
                "randen_engine must be move assignable");

  static_assert(std::is_same<decltype(std::declval<E>()()), T>::value,
                "return type of operator() must be result_type");

  // Names after definition of [rand.req.urbg] in C++ standard.
  // e us a value of E
  // v is a lvalue of E
  // x, y are possibly const values of E
  // s is a value of T
  // q is a value satisfying requirements of seed_sequence
  // z is a value of type unsigned long long
  // os is a some specialization of basic_ostream
  // is is a some specialization of basic_istream

  E e, v;
  const E x, y;
  T s = 1;
  std::seed_seq q{1, 2, 3};
  unsigned long long z = 1;  // NOLINT(runtime/int)
  std::wostringstream os;
  std::wistringstream is;

  E{};
  E{x};
  E{s};
  E{q};

  e.seed();

  // MSVC emits error 2718 when using EXPECT_EQ(e, x)
  //  * actual parameter with __declspec(align('#')) won't be aligned
  EXPECT_TRUE(e == x);

  e.seed(q);
  {
    E tmp(q);
    EXPECT_TRUE(e == tmp);
  }

  e();
  {
    E tmp(q);
    EXPECT_TRUE(e != tmp);
  }

  e.discard(z);

  static_assert(std::is_same<decltype(x == y), bool>::value,
                "return type of operator== must be bool");

  static_assert(std::is_same<decltype(x != y), bool>::value,
                "return type of operator== must be bool");
}

TYPED_TEST(RandenEngineTypedTest, RandenEngineSFINAETest) {
  using randen = typename absl::random_internal::randen_engine<TypeParam>;
  using result_type = typename randen::result_type;

  {
    randen engine(result_type(1));
    engine.seed(result_type(1));
  }

  {
    result_type n = 1;
    randen engine(n);
    engine.seed(n);
  }

  {
    randen engine(1);
    engine.seed(1);
  }

  {
    int n = 1;
    randen engine(n);
    engine.seed(n);
  }

  {
    std::seed_seq seed_seq;
    randen engine(seed_seq);
    engine.seed(seed_seq);
  }

  {
    randen engine{std::seed_seq()};
    engine.seed(std::seed_seq());
  }
}

TEST(RandenTest, VerifyGoldenRanden64Default) {
#ifdef ABSL_IS_LITTLE_ENDIAN
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0xc3c14f134e433977, 0xdda9f47cd90410ee, 0x887bf3087fd8ca10,
      0xf0b780f545c72912, 0x15dbb1d37696599f, 0x30ec63baff3c6d59,
      0xb29f73606f7f20a6, 0x02808a316f49a54c, 0x3b8feaf9d5c8e50e,
      0x9cbf605e3fd9de8a, 0xc970ae1a78183bbb, 0xd8b2ffd356301ed5,
      0xf4b327fe0fc73c37, 0xcdfd8d76eb8f9a19, 0xc3a506eb91420c9d,
      0xd5af05dd3eff9556, 0x48db1bb78f83c4a1, 0x7023920e0d6bfe8c,
      0x58d3575834956d42, 0xed1ef4c26b87b840, 0x8eef32a23e0b2df3,
      0x497cabf3431154fc, 0x4e24370570029a8b, 0xd88b5749f090e5ea,
      0xc651a582a970692f, 0x78fcec2cbb6342f5, 0x463cb745612f55db,
      0x352ee4ad1816afe3, 0x026ff374c101da7e, 0x811ef0821c3de851,
      0x6f7e616704c4fa59, 0xa0660379992d58fc, 0x04b0a374a3b795c7,
      0x915f3445685da798, 0x26802a8ac76571ce, 0x4663352533ce1882,
      0xb9fdefb4a24dc738, 0x5588ba3a4d6e6c51, 0xa2101a42d35f1956,
      0x607195a5e200f5fd, 0x7e100308f3290764, 0xe1e5e03c759c0709,
      0x082572cc5da6606f, 0xcbcf585399e432f1, 0xe8a2be4f8335d8f1,
      0x0904469acbfee8f2, 0xf08bd31b6daecd51, 0x08e8a1f1a69da69a,
      0x6542a20aad57bff5, 0x2e9705bb053d6b46, 0xda2fc9db0713c391,
      0x78e3a810213b6ffb, 0xdc16a59cdd85f8a6, 0xc0932718cd55781f,
      0xb9bfb29c2b20bfe5, 0xb97289c1be0f2f9c, 0xc0a2a0e403a892d4,
      0x5524bb834771435b, 0x8265da3d39d1a750, 0xff4af3ab8d1b78c5,
      0xf0ec5f424bcad77f, 0x66e455f627495189, 0xc82d3120b57e3270,
      0x3424e47dc22596e3, 0xbc0c95129ccedcdd, 0xc191c595afc4dcbf,
      0x120392bd2bb70939, 0x7f90650ea6cd6ab4, 0x7287491832695ad3,
      0xa7c8fac5a7917eb0, 0xd088cb9418be0361, 0x7c1bf9839c7c1ce5,
      0xe2e991fa58e1e79e, 0x78565cdefd28c4ad, 0x7351b9fef98bafad,
      0x2a9eac28b08c96bf, 0x6c4f179696cb2225, 0x13a685861bab87e0,
      0x64c6de5aa0501971, 0x30537425cac70991, 0x01590d9dc6c532b7,
      0x7e05e3aa8ec720dc, 0x74a07d9c54e3e63f, 0x738184388f3bc1d2,
      0x26ffdc5067be3acb, 0x6bcdf185561f255f, 0xa0eaf2e1cf99b1c6,
      0x171df81934f68604, 0x7ea5a21665683e5a, 0x5d1cb02075ba1cea,
      0x957f38cbd2123fdf, 0xba6364eff80de02f, 0x606e0a0e41d452ee,
      0x892d8317de82f7a2, 0xe707b1db50f7b43e, 0x4eb28826766fcf5b,
      0x5a362d56e80a0951, 0x6ee217df16527d78, 0xf6737962ba6b23dd,
      0x443e63857d4076ca, 0x790d9a5f048adfeb, 0xd796b052151ee94d,
      0x033ed95c12b04a03, 0x8b833ff84893da5d, 0x3d6724b1bb15eab9,
      0x9877c4225061ca76, 0xd68d6810adf74fb3, 0x42e5352fe30ce989,
      0x265b565a7431fde7, 0x3cdbf7e358df4b8b, 0x2922a47f6d3e8779,
      0x52d2242f65b37f88, 0x5d836d6e2958d6b5, 0x29d40f00566d5e26,
      0x288db0e1124b14a0, 0x6c056608b7d9c1b6, 0x0b9471bdb8f19d32,
      0x8fb946504faa6c9d, 0x8943a9464540251c, 0xfd1fe27d144a09e0,
      0xea6ac458da141bda, 0x8048f217633fce36, 0xfeda1384ade74d31,
      0x4334b8b02ff7612f, 0xdbc8441f5227e216, 0x096d119a3605c85b,
      0x2b72b31c21b7d7d0};
#else
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0xdda9f47cd90410ee, 0xc3c14f134e433977, 0xf0b780f545c72912,
      0x887bf3087fd8ca10, 0x30ec63baff3c6d59, 0x15dbb1d37696599f,
      0x2808a316f49a54c,  0xb29f73606f7f20a6, 0x9cbf605e3fd9de8a,
      0x3b8feaf9d5c8e50e, 0xd8b2ffd356301ed5, 0xc970ae1a78183bbb,
      0xcdfd8d76eb8f9a19, 0xf4b327fe0fc73c37, 0xd5af05dd3eff9556,
      0xc3a506eb91420c9d, 0x7023920e0d6bfe8c, 0x48db1bb78f83c4a1,
      0xed1ef4c26b87b840, 0x58d3575834956d42, 0x497cabf3431154fc,
      0x8eef32a23e0b2df3, 0xd88b5749f090e5ea, 0x4e24370570029a8b,
      0x78fcec2cbb6342f5, 0xc651a582a970692f, 0x352ee4ad1816afe3,
      0x463cb745612f55db, 0x811ef0821c3de851, 0x26ff374c101da7e,
      0xa0660379992d58fc, 0x6f7e616704c4fa59, 0x915f3445685da798,
      0x4b0a374a3b795c7,  0x4663352533ce1882, 0x26802a8ac76571ce,
      0x5588ba3a4d6e6c51, 0xb9fdefb4a24dc738, 0x607195a5e200f5fd,
      0xa2101a42d35f1956, 0xe1e5e03c759c0709, 0x7e100308f3290764,
      0xcbcf585399e432f1, 0x82572cc5da6606f,  0x904469acbfee8f2,
      0xe8a2be4f8335d8f1, 0x8e8a1f1a69da69a,  0xf08bd31b6daecd51,
      0x2e9705bb053d6b46, 0x6542a20aad57bff5, 0x78e3a810213b6ffb,
      0xda2fc9db0713c391, 0xc0932718cd55781f, 0xdc16a59cdd85f8a6,
      0xb97289c1be0f2f9c, 0xb9bfb29c2b20bfe5, 0x5524bb834771435b,
      0xc0a2a0e403a892d4, 0xff4af3ab8d1b78c5, 0x8265da3d39d1a750,
      0x66e455f627495189, 0xf0ec5f424bcad77f, 0x3424e47dc22596e3,
      0xc82d3120b57e3270, 0xc191c595afc4dcbf, 0xbc0c95129ccedcdd,
      0x7f90650ea6cd6ab4, 0x120392bd2bb70939, 0xa7c8fac5a7917eb0,
      0x7287491832695ad3, 0x7c1bf9839c7c1ce5, 0xd088cb9418be0361,
      0x78565cdefd28c4ad, 0xe2e991fa58e1e79e, 0x2a9eac28b08c96bf,
      0x7351b9fef98bafad, 0x13a685861bab87e0, 0x6c4f179696cb2225,
      0x30537425cac70991, 0x64c6de5aa0501971, 0x7e05e3aa8ec720dc,
      0x1590d9dc6c532b7,  0x738184388f3bc1d2, 0x74a07d9c54e3e63f,
      0x6bcdf185561f255f, 0x26ffdc5067be3acb, 0x171df81934f68604,
      0xa0eaf2e1cf99b1c6, 0x5d1cb02075ba1cea, 0x7ea5a21665683e5a,
      0xba6364eff80de02f, 0x957f38cbd2123fdf, 0x892d8317de82f7a2,
      0x606e0a0e41d452ee, 0x4eb28826766fcf5b, 0xe707b1db50f7b43e,
      0x6ee217df16527d78, 0x5a362d56e80a0951, 0x443e63857d4076ca,
      0xf6737962ba6b23dd, 0xd796b052151ee94d, 0x790d9a5f048adfeb,
      0x8b833ff84893da5d, 0x33ed95c12b04a03,  0x9877c4225061ca76,
      0x3d6724b1bb15eab9, 0x42e5352fe30ce989, 0xd68d6810adf74fb3,
      0x3cdbf7e358df4b8b, 0x265b565a7431fde7, 0x52d2242f65b37f88,
      0x2922a47f6d3e8779, 0x29d40f00566d5e26, 0x5d836d6e2958d6b5,
      0x6c056608b7d9c1b6, 0x288db0e1124b14a0, 0x8fb946504faa6c9d,
      0xb9471bdb8f19d32,  0xfd1fe27d144a09e0, 0x8943a9464540251c,
      0x8048f217633fce36, 0xea6ac458da141bda, 0x4334b8b02ff7612f,
      0xfeda1384ade74d31, 0x96d119a3605c85b,  0xdbc8441f5227e216,
      0x541ad7efa6ddc1d3};
#endif

  randen_u64 engine;
#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    printf("0x%016lx, ", engine());
    if (i % 3 == 2) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
  engine.seed();
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

TEST(RandenTest, VerifyGoldenRanden64Seeded) {
#ifdef ABSL_IS_LITTLE_ENDIAN
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0x83a9e58f94d3dcd5, 0x70bbdff3d97949fb, 0x0438481f7471c1b4,
      0x34fdc58ee5fb5930, 0xceee4f2d2a937d17, 0xb5a26a68e432aea9,
      0x8b64774a3fb51740, 0xd89ac1fc74249c74, 0x03910d1d23fc3fdf,
      0xd38f630878aa897f, 0x0ee8f0f5615f7e44, 0x98f5a53df8279d52,
      0xb403f52c25938d0e, 0x240072996ea6e838, 0xd3a791246190fa61,
      0xaaedd3df7a7b4f80, 0xc6eacabe05deaf6e, 0xb7967dd8790edf4d,
      0x9a0a8e67e049d279, 0x0494f606aebc23e7, 0x598dcd687bc3e0ee,
      0x010ac81802d452a1, 0x6407c87160aa2842, 0x5a56e276486f93a0,
      0xc887a399d46a8f02, 0x9e1e6100fe93b740, 0x12d02e330f8901f6,
      0xc39ca52b47e790b7, 0xb0b0a2fa11e82e61, 0x1542d841a303806a,
      0x1fe659fd7d6e9d86, 0xb8c90d80746541ac, 0x239d56a5669ddc94,
      0xd40db57c8123d13c, 0x3abc2414153a0db0, 0x9bad665630cb8d61,
      0x0bd1fb90ee3f4bbc, 0x8f0b4d7e079b4e42, 0xfa0fb0e0ee59e793,
      0x51080b283e071100, 0x2c4b9e715081cc15, 0xbe10ed49de4941df,
      0xf8eaac9d4b1b0d37, 0x4bcce4b54605e139, 0xa64722b76765dda6,
      0xb9377d738ca28ab5, 0x779fad81a8ccc1af, 0x65cb3ee61ffd3ba7,
      0xd74e79087862836f, 0xd05b9c584c3f25bf, 0x2ba93a4693579827,
      0xd81530aff05420ce, 0xec06cea215478621, 0x4b1798a6796d65ad,
      0xf142f3fb3a6f6fa6, 0x002b7bf7e237b560, 0xf47f2605ef65b4f8,
      0x9804ec5517effc18, 0xaed3d7f8b7d481cd, 0x5651c24c1ce338d1,
      0x3e7a38208bf0a3c6, 0x6796a7b614534aed, 0x0d0f3b848358460f,
      0x0fa5fe7600b19524, 0x2b0cf38253faaedc, 0x10df9188233a9fd6,
      0x3a10033880138b59, 0x5fb0b0d23948e80f, 0x9e76f7b02fbf5350,
      0x0816052304b1a985, 0x30c9880db41fd218, 0x14aa399b65e20f28,
      0xe1454a8cace787b4, 0x325ac971b6c6f0f5, 0x716b1aa2784f3d36,
      0x3d5ce14accfd144f, 0x6c0c97710f651792, 0xbc5b0f59fb333532,
      0x2a90a7d2140470bc, 0x8da269f55c1e1c8d, 0xcfc37143895792ca,
      0xbe21eab1f30b238f, 0x8c47229dee4d65fd, 0x5743614ed1ed7d54,
      0x351372a99e9c476e, 0x2bd5ea15e5db085f, 0x6925fde46e0af4ca,
      0xed3eda2bdc1f45bd, 0xdef68c68d460fa6e, 0xe42a0de76253e2b5,
      0x4e5176dcbc29c305, 0xbfd85fba9f810f6e, 0x76a5a2a9beb815c6,
      0x01edc4ddceaf414c, 0xa4e98904b4bb3b4b, 0x00bd63ac7d2f1ddd,
      0xb8491fe6e998ddbb, 0xb386a3463dda6800, 0x0081887688871619,
      0x33d394b3344e9a38, 0x815dba65a3a8baf9, 0x4232f6ec02c2fd1a,
      0xb5cff603edd20834, 0x580189243f687663, 0xa8d5a2cbdc27fe99,
      0x725d881693fa0131, 0xa2be2c13db2c7ac5, 0x7b6a9614b509fd78,
      0xb6b136d71e717636, 0x660f1a71aff046ea, 0x0ba10ae346c8ec9e,
      0xe66dde53e3145b41, 0x3b18288c88c26be6, 0x4d9d9d2ff02db933,
      0x4167da8c70f46e8a, 0xf183beef8c6318b4, 0x4d889e1e71eeeef1,
      0x7175c71ad6689b6b, 0xfb9e42beacd1b7dd, 0xc33d0e91b29b5e0d,
      0xd39b83291ce47922, 0xc4d570fb8493d12e, 0x23d5a5724f424ae6,
      0x5245f161876b6616, 0x38d77dbd21ab578d, 0x9c3423311f4ecbfe,
      0x76fe31389bacd9d5,
  };
#else
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0xc09ebe4820855e97, 0xfe6d8e10f8237f98, 0xb118c04ddbdecd97,
      0x2af13429587cc557, 0x715bb51e4b7b2dee, 0x21c25d4aa7ad64bc,
      0x8550d11aaa0584c2, 0xc54a90f656dac0e6, 0xb330ca0932bdb916,
      0x32fecae3ac265555, 0x154832ce173833b3, 0x93d6b7ec48f4da12,
      0xe1b2a960334e9dad, 0x5963a6258b1188b4, 0x92f36aff5affb5d5,
      0x594b0ff255df357c, 0x3eb13a42bfda0b95, 0x4711d216a2ba4c78,
      0x72aa24982234501a, 0xe4191edf78b2af39, 0x49bbab565c77a159,
      0x65090c7977922016, 0xf939b071119a93a8, 0x6e1d23821035a828,
      0xecd5cfdd69445b09, 0x511acf082b829975, 0xfe2371384c3f8400,
      0x4f58d3fd268d9c4a, 0x276bf979005c014d, 0xce7843c375f260d5,
      0x96a4ddd7d20401dc, 0x40a10a62346d709f, 0x4ff1fef08693c44a,
      0xcb2cc2ee0374b3ed, 0x68468395ccc2f005, 0x519662d6916206b7,
      0xc848436d6b1f74c4, 0x83270055fca47d39, 0x5762f82562b7208c,
      0x37135e9d06c809d8, 0x124ebd487c750336, 0x65819924508480d0,
      0xcdb6eff7a78fc001, 0xb1c9859c81671970, 0x9a13f65d6a7f1cc5,
      0x8c19db116d9a08ad, 0x910f2803871c40a0, 0x8e848711411a619c,
      0xe5407f5108de8751, 0x4ac734f6e0fef8ad, 0xc7ac42cb37c18e46,
      0x6d807d769560aca,  0x9594d50aa649776d, 0x2d7ba7c03faa3445,
      0x797b766727eef114, 0x19b6451e86362e44, 0xbe39b0562c160a22,
      0xff639de9f104b20,  0xbe3888cf3e7029e2, 0x534f2ad79acd2608,
      0xd6991e4c4d39fdf4, 0x7a24578b14b23b80, 0x5499c40f364d03a8,
      0x3018421d4618640c, 0xc4444c95baa5dc49, 0x425e8c43f86d2c98,
      0x9799ae001c432a45, 0x5d7a09c0f7707c0d, 0x4519c7c7c5e9216e,
      0x29887cc0b1629ac8, 0xc3b05674c37068b5, 0xc78e9ac786f724fe,
      0x98697e4532f9304d, 0x9fe224c3678a4fa8, 0x1326fce0436288dd,
      0x29078f1af4d02a88, 0xd1102148f48a1a4c, 0x48a6101774733de7,
      0x32988e28998f4b01, 0x350aaac0434430b1, 0xf01e1dcc4f3d81a4,
      0x4e453e6dc53152b9, 0xa60093efd24cef18, 0x2738dc8ad7d403b4,
      0x6a540c9a5a656beb, 0x348e9a1f81e9f4f6, 0xe4a9553a4956fcf1,
      0x2ea7715d02f35372, 0xb11b11ef5ed86f01, 0x1ce4c3ce10b4fdb1,
      0x478f3f963ddce14,  0xadab37b4b846683b, 0xf67f910809fb9331,
      0x191d1433befc9902, 0x362a78bfa13afe50, 0x998360c031072c66,
      0x4c549728a153a412, 0x1444957eed100fff, 0x1ba997c567f0ccc5,
      0x380736514a20db0,  0x58cef4d946c03562, 0xa967a60608662775,
      0x699c0e6109f294be, 0xb24f5172e716cf04, 0x3036ea5229560a3b,
      0x6b34099470653f08, 0x3c9d025a6c3f5990, 0xe2ca924786559a50,
      0xb675ba86d07bab9b, 0x3071b26e9e804cca, 0x809394b8f68ed56a,
      0x2def76978cbc3bef, 0x51dee763daee1e39, 0x455b41fbcf947d0b,
      0xb8d63d2416b07af0, 0x81444a3f5d8323ed, 0x8d3506690125fc45,
      0xb29099c0631cadc1, 0xebf50c72e025db42, 0x22cc4e9039a9b527,
      0x412e75a6018a74ff, 0x64c17eaf8dda3d2b, 0x804f908eae5cbcbe,
      0x466a10e375d53918, 0x9bbe00bb73df5ed7, 0xd51991503872d538,
      0xac53fd153f650dff};
#endif

  ExplicitSeedSeq seed_sequence{12, 34, 56};
  randen_u64 engine(seed_sequence);
#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    printf("0x%016lx, ", engine());
    if (i % 3 == 2) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
  engine.seed(seed_sequence);
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

TEST(RandenTest, VerifyGoldenRanden32Default) {
#ifdef ABSL_IS_LITTLE_ENDIAN
  constexpr uint64_t kGolden[2 * kNumGoldenOutputs] = {
      0x4e433977, 0xc3c14f13, 0xd90410ee, 0xdda9f47c, 0x7fd8ca10, 0x887bf308,
      0x45c72912, 0xf0b780f5, 0x7696599f, 0x15dbb1d3, 0xff3c6d59, 0x30ec63ba,
      0x6f7f20a6, 0xb29f7360, 0x6f49a54c, 0x02808a31, 0xd5c8e50e, 0x3b8feaf9,
      0x3fd9de8a, 0x9cbf605e, 0x78183bbb, 0xc970ae1a, 0x56301ed5, 0xd8b2ffd3,
      0x0fc73c37, 0xf4b327fe, 0xeb8f9a19, 0xcdfd8d76, 0x91420c9d, 0xc3a506eb,
      0x3eff9556, 0xd5af05dd, 0x8f83c4a1, 0x48db1bb7, 0x0d6bfe8c, 0x7023920e,
      0x34956d42, 0x58d35758, 0x6b87b840, 0xed1ef4c2, 0x3e0b2df3, 0x8eef32a2,
      0x431154fc, 0x497cabf3, 0x70029a8b, 0x4e243705, 0xf090e5ea, 0xd88b5749,
      0xa970692f, 0xc651a582, 0xbb6342f5, 0x78fcec2c, 0x612f55db, 0x463cb745,
      0x1816afe3, 0x352ee4ad, 0xc101da7e, 0x026ff374, 0x1c3de851, 0x811ef082,
      0x04c4fa59, 0x6f7e6167, 0x992d58fc, 0xa0660379, 0xa3b795c7, 0x04b0a374,
      0x685da798, 0x915f3445, 0xc76571ce, 0x26802a8a, 0x33ce1882, 0x46633525,
      0xa24dc738, 0xb9fdefb4, 0x4d6e6c51, 0x5588ba3a, 0xd35f1956, 0xa2101a42,
      0xe200f5fd, 0x607195a5, 0xf3290764, 0x7e100308, 0x759c0709, 0xe1e5e03c,
      0x5da6606f, 0x082572cc, 0x99e432f1, 0xcbcf5853, 0x8335d8f1, 0xe8a2be4f,
      0xcbfee8f2, 0x0904469a, 0x6daecd51, 0xf08bd31b, 0xa69da69a, 0x08e8a1f1,
      0xad57bff5, 0x6542a20a, 0x053d6b46, 0x2e9705bb, 0x0713c391, 0xda2fc9db,
      0x213b6ffb, 0x78e3a810, 0xdd85f8a6, 0xdc16a59c, 0xcd55781f, 0xc0932718,
      0x2b20bfe5, 0xb9bfb29c, 0xbe0f2f9c, 0xb97289c1, 0x03a892d4, 0xc0a2a0e4,
      0x4771435b, 0x5524bb83, 0x39d1a750, 0x8265da3d, 0x8d1b78c5, 0xff4af3ab,
      0x4bcad77f, 0xf0ec5f42, 0x27495189, 0x66e455f6, 0xb57e3270, 0xc82d3120,
      0xc22596e3, 0x3424e47d, 0x9ccedcdd, 0xbc0c9512, 0xafc4dcbf, 0xc191c595,
      0x2bb70939, 0x120392bd, 0xa6cd6ab4, 0x7f90650e, 0x32695ad3, 0x72874918,
      0xa7917eb0, 0xa7c8fac5, 0x18be0361, 0xd088cb94, 0x9c7c1ce5, 0x7c1bf983,
      0x58e1e79e, 0xe2e991fa, 0xfd28c4ad, 0x78565cde, 0xf98bafad, 0x7351b9fe,
      0xb08c96bf, 0x2a9eac28, 0x96cb2225, 0x6c4f1796, 0x1bab87e0, 0x13a68586,
      0xa0501971, 0x64c6de5a, 0xcac70991, 0x30537425, 0xc6c532b7, 0x01590d9d,
      0x8ec720dc, 0x7e05e3aa, 0x54e3e63f, 0x74a07d9c, 0x8f3bc1d2, 0x73818438,
      0x67be3acb, 0x26ffdc50, 0x561f255f, 0x6bcdf185, 0xcf99b1c6, 0xa0eaf2e1,
      0x34f68604, 0x171df819, 0x65683e5a, 0x7ea5a216, 0x75ba1cea, 0x5d1cb020,
      0xd2123fdf, 0x957f38cb, 0xf80de02f, 0xba6364ef, 0x41d452ee, 0x606e0a0e,
      0xde82f7a2, 0x892d8317, 0x50f7b43e, 0xe707b1db, 0x766fcf5b, 0x4eb28826,
      0xe80a0951, 0x5a362d56, 0x16527d78, 0x6ee217df, 0xba6b23dd, 0xf6737962,
      0x7d4076ca, 0x443e6385, 0x048adfeb, 0x790d9a5f, 0x151ee94d, 0xd796b052,
      0x12b04a03, 0x033ed95c, 0x4893da5d, 0x8b833ff8, 0xbb15eab9, 0x3d6724b1,
      0x5061ca76, 0x9877c422, 0xadf74fb3, 0xd68d6810, 0xe30ce989, 0x42e5352f,
      0x7431fde7, 0x265b565a, 0x58df4b8b, 0x3cdbf7e3, 0x6d3e8779, 0x2922a47f,
      0x65b37f88, 0x52d2242f, 0x2958d6b5, 0x5d836d6e, 0x566d5e26, 0x29d40f00,
      0x124b14a0, 0x288db0e1, 0xb7d9c1b6, 0x6c056608, 0xb8f19d32, 0x0b9471bd,
      0x4faa6c9d, 0x8fb94650, 0x4540251c, 0x8943a946, 0x144a09e0, 0xfd1fe27d,
      0xda141bda, 0xea6ac458, 0x633fce36, 0x8048f217, 0xade74d31, 0xfeda1384,
      0x2ff7612f, 0x4334b8b0, 0x5227e216, 0xdbc8441f, 0x3605c85b, 0x096d119a,
      0x21b7d7d0, 0x2b72b31c};
#else
  constexpr uint64_t kGolden[2 * kNumGoldenOutputs] = {
      0xdda9f47c, 0xd90410ee, 0xc3c14f13, 0x4e433977, 0xf0b780f5, 0x45c72912,
      0x887bf308, 0x7fd8ca10, 0x30ec63ba, 0xff3c6d59, 0x15dbb1d3, 0x7696599f,
      0x2808a31,  0x6f49a54c, 0xb29f7360, 0x6f7f20a6, 0x9cbf605e, 0x3fd9de8a,
      0x3b8feaf9, 0xd5c8e50e, 0xd8b2ffd3, 0x56301ed5, 0xc970ae1a, 0x78183bbb,
      0xcdfd8d76, 0xeb8f9a19, 0xf4b327fe, 0xfc73c37,  0xd5af05dd, 0x3eff9556,
      0xc3a506eb, 0x91420c9d, 0x7023920e, 0xd6bfe8c,  0x48db1bb7, 0x8f83c4a1,
      0xed1ef4c2, 0x6b87b840, 0x58d35758, 0x34956d42, 0x497cabf3, 0x431154fc,
      0x8eef32a2, 0x3e0b2df3, 0xd88b5749, 0xf090e5ea, 0x4e243705, 0x70029a8b,
      0x78fcec2c, 0xbb6342f5, 0xc651a582, 0xa970692f, 0x352ee4ad, 0x1816afe3,
      0x463cb745, 0x612f55db, 0x811ef082, 0x1c3de851, 0x26ff374,  0xc101da7e,
      0xa0660379, 0x992d58fc, 0x6f7e6167, 0x4c4fa59,  0x915f3445, 0x685da798,
      0x4b0a374,  0xa3b795c7, 0x46633525, 0x33ce1882, 0x26802a8a, 0xc76571ce,
      0x5588ba3a, 0x4d6e6c51, 0xb9fdefb4, 0xa24dc738, 0x607195a5, 0xe200f5fd,
      0xa2101a42, 0xd35f1956, 0xe1e5e03c, 0x759c0709, 0x7e100308, 0xf3290764,
      0xcbcf5853, 0x99e432f1, 0x82572cc,  0x5da6606f, 0x904469a,  0xcbfee8f2,
      0xe8a2be4f, 0x8335d8f1, 0x8e8a1f1,  0xa69da69a, 0xf08bd31b, 0x6daecd51,
      0x2e9705bb, 0x53d6b46,  0x6542a20a, 0xad57bff5, 0x78e3a810, 0x213b6ffb,
      0xda2fc9db, 0x713c391,  0xc0932718, 0xcd55781f, 0xdc16a59c, 0xdd85f8a6,
      0xb97289c1, 0xbe0f2f9c, 0xb9bfb29c, 0x2b20bfe5, 0x5524bb83, 0x4771435b,
      0xc0a2a0e4, 0x3a892d4,  0xff4af3ab, 0x8d1b78c5, 0x8265da3d, 0x39d1a750,
      0x66e455f6, 0x27495189, 0xf0ec5f42, 0x4bcad77f, 0x3424e47d, 0xc22596e3,
      0xc82d3120, 0xb57e3270, 0xc191c595, 0xafc4dcbf, 0xbc0c9512, 0x9ccedcdd,
      0x7f90650e, 0xa6cd6ab4, 0x120392bd, 0x2bb70939, 0xa7c8fac5, 0xa7917eb0,
      0x72874918, 0x32695ad3, 0x7c1bf983, 0x9c7c1ce5, 0xd088cb94, 0x18be0361,
      0x78565cde, 0xfd28c4ad, 0xe2e991fa, 0x58e1e79e, 0x2a9eac28, 0xb08c96bf,
      0x7351b9fe, 0xf98bafad, 0x13a68586, 0x1bab87e0, 0x6c4f1796, 0x96cb2225,
      0x30537425, 0xcac70991, 0x64c6de5a, 0xa0501971, 0x7e05e3aa, 0x8ec720dc,
      0x1590d9d,  0xc6c532b7, 0x73818438, 0x8f3bc1d2, 0x74a07d9c, 0x54e3e63f,
      0x6bcdf185, 0x561f255f, 0x26ffdc50, 0x67be3acb, 0x171df819, 0x34f68604,
      0xa0eaf2e1, 0xcf99b1c6, 0x5d1cb020, 0x75ba1cea, 0x7ea5a216, 0x65683e5a,
      0xba6364ef, 0xf80de02f, 0x957f38cb, 0xd2123fdf, 0x892d8317, 0xde82f7a2,
      0x606e0a0e, 0x41d452ee, 0x4eb28826, 0x766fcf5b, 0xe707b1db, 0x50f7b43e,
      0x6ee217df, 0x16527d78, 0x5a362d56, 0xe80a0951, 0x443e6385, 0x7d4076ca,
      0xf6737962, 0xba6b23dd, 0xd796b052, 0x151ee94d, 0x790d9a5f, 0x48adfeb,
      0x8b833ff8, 0x4893da5d, 0x33ed95c,  0x12b04a03, 0x9877c422, 0x5061ca76,
      0x3d6724b1, 0xbb15eab9, 0x42e5352f, 0xe30ce989, 0xd68d6810, 0xadf74fb3,
      0x3cdbf7e3, 0x58df4b8b, 0x265b565a, 0x7431fde7, 0x52d2242f, 0x65b37f88,
      0x2922a47f, 0x6d3e8779, 0x29d40f00, 0x566d5e26, 0x5d836d6e, 0x2958d6b5,
      0x6c056608, 0xb7d9c1b6, 0x288db0e1, 0x124b14a0, 0x8fb94650, 0x4faa6c9d,
      0xb9471bd,  0xb8f19d32, 0xfd1fe27d, 0x144a09e0, 0x8943a946, 0x4540251c,
      0x8048f217, 0x633fce36, 0xea6ac458, 0xda141bda, 0x4334b8b0, 0x2ff7612f,
      0xfeda1384, 0xade74d31, 0x96d119a,  0x3605c85b, 0xdbc8441f, 0x5227e216,
      0x541ad7ef, 0xa6ddc1d3};
#endif

  randen_u32 engine;
#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  for (size_t i = 0; i < 2 * kNumGoldenOutputs; ++i) {
    printf("0x%08x, ", engine());
    if (i % 6 == 5) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
  engine.seed();
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

TEST(RandenTest, VerifyGoldenRanden32Seeded) {
#ifdef ABSL_IS_LITTLE_ENDIAN
  constexpr uint64_t kGolden[2 * kNumGoldenOutputs] = {
      0x94d3dcd5, 0x83a9e58f, 0xd97949fb, 0x70bbdff3, 0x7471c1b4, 0x0438481f,
      0xe5fb5930, 0x34fdc58e, 0x2a937d17, 0xceee4f2d, 0xe432aea9, 0xb5a26a68,
      0x3fb51740, 0x8b64774a, 0x74249c74, 0xd89ac1fc, 0x23fc3fdf, 0x03910d1d,
      0x78aa897f, 0xd38f6308, 0x615f7e44, 0x0ee8f0f5, 0xf8279d52, 0x98f5a53d,
      0x25938d0e, 0xb403f52c, 0x6ea6e838, 0x24007299, 0x6190fa61, 0xd3a79124,
      0x7a7b4f80, 0xaaedd3df, 0x05deaf6e, 0xc6eacabe, 0x790edf4d, 0xb7967dd8,
      0xe049d279, 0x9a0a8e67, 0xaebc23e7, 0x0494f606, 0x7bc3e0ee, 0x598dcd68,
      0x02d452a1, 0x010ac818, 0x60aa2842, 0x6407c871, 0x486f93a0, 0x5a56e276,
      0xd46a8f02, 0xc887a399, 0xfe93b740, 0x9e1e6100, 0x0f8901f6, 0x12d02e33,
      0x47e790b7, 0xc39ca52b, 0x11e82e61, 0xb0b0a2fa, 0xa303806a, 0x1542d841,
      0x7d6e9d86, 0x1fe659fd, 0x746541ac, 0xb8c90d80, 0x669ddc94, 0x239d56a5,
      0x8123d13c, 0xd40db57c, 0x153a0db0, 0x3abc2414, 0x30cb8d61, 0x9bad6656,
      0xee3f4bbc, 0x0bd1fb90, 0x079b4e42, 0x8f0b4d7e, 0xee59e793, 0xfa0fb0e0,
      0x3e071100, 0x51080b28, 0x5081cc15, 0x2c4b9e71, 0xde4941df, 0xbe10ed49,
      0x4b1b0d37, 0xf8eaac9d, 0x4605e139, 0x4bcce4b5, 0x6765dda6, 0xa64722b7,
      0x8ca28ab5, 0xb9377d73, 0xa8ccc1af, 0x779fad81, 0x1ffd3ba7, 0x65cb3ee6,
      0x7862836f, 0xd74e7908, 0x4c3f25bf, 0xd05b9c58, 0x93579827, 0x2ba93a46,
      0xf05420ce, 0xd81530af, 0x15478621, 0xec06cea2, 0x796d65ad, 0x4b1798a6,
      0x3a6f6fa6, 0xf142f3fb, 0xe237b560, 0x002b7bf7, 0xef65b4f8, 0xf47f2605,
      0x17effc18, 0x9804ec55, 0xb7d481cd, 0xaed3d7f8, 0x1ce338d1, 0x5651c24c,
      0x8bf0a3c6, 0x3e7a3820, 0x14534aed, 0x6796a7b6, 0x8358460f, 0x0d0f3b84,
      0x00b19524, 0x0fa5fe76, 0x53faaedc, 0x2b0cf382, 0x233a9fd6, 0x10df9188,
      0x80138b59, 0x3a100338, 0x3948e80f, 0x5fb0b0d2, 0x2fbf5350, 0x9e76f7b0,
      0x04b1a985, 0x08160523, 0xb41fd218, 0x30c9880d, 0x65e20f28, 0x14aa399b,
      0xace787b4, 0xe1454a8c, 0xb6c6f0f5, 0x325ac971, 0x784f3d36, 0x716b1aa2,
      0xccfd144f, 0x3d5ce14a, 0x0f651792, 0x6c0c9771, 0xfb333532, 0xbc5b0f59,
      0x140470bc, 0x2a90a7d2, 0x5c1e1c8d, 0x8da269f5, 0x895792ca, 0xcfc37143,
      0xf30b238f, 0xbe21eab1, 0xee4d65fd, 0x8c47229d, 0xd1ed7d54, 0x5743614e,
      0x9e9c476e, 0x351372a9, 0xe5db085f, 0x2bd5ea15, 0x6e0af4ca, 0x6925fde4,
      0xdc1f45bd, 0xed3eda2b, 0xd460fa6e, 0xdef68c68, 0x6253e2b5, 0xe42a0de7,
      0xbc29c305, 0x4e5176dc, 0x9f810f6e, 0xbfd85fba, 0xbeb815c6, 0x76a5a2a9,
      0xceaf414c, 0x01edc4dd, 0xb4bb3b4b, 0xa4e98904, 0x7d2f1ddd, 0x00bd63ac,
      0xe998ddbb, 0xb8491fe6, 0x3dda6800, 0xb386a346, 0x88871619, 0x00818876,
      0x344e9a38, 0x33d394b3, 0xa3a8baf9, 0x815dba65, 0x02c2fd1a, 0x4232f6ec,
      0xedd20834, 0xb5cff603, 0x3f687663, 0x58018924, 0xdc27fe99, 0xa8d5a2cb,
      0x93fa0131, 0x725d8816, 0xdb2c7ac5, 0xa2be2c13, 0xb509fd78, 0x7b6a9614,
      0x1e717636, 0xb6b136d7, 0xaff046ea, 0x660f1a71, 0x46c8ec9e, 0x0ba10ae3,
      0xe3145b41, 0xe66dde53, 0x88c26be6, 0x3b18288c, 0xf02db933, 0x4d9d9d2f,
      0x70f46e8a, 0x4167da8c, 0x8c6318b4, 0xf183beef, 0x71eeeef1, 0x4d889e1e,
      0xd6689b6b, 0x7175c71a, 0xacd1b7dd, 0xfb9e42be, 0xb29b5e0d, 0xc33d0e91,
      0x1ce47922, 0xd39b8329, 0x8493d12e, 0xc4d570fb, 0x4f424ae6, 0x23d5a572,
      0x876b6616, 0x5245f161, 0x21ab578d, 0x38d77dbd, 0x1f4ecbfe, 0x9c342331,
      0x9bacd9d5, 0x76fe3138,
  };
#else
  constexpr uint64_t kGolden[2 * kNumGoldenOutputs] = {
      0xc09ebe48, 0x20855e97, 0xfe6d8e10, 0xf8237f98, 0xb118c04d, 0xdbdecd97,
      0x2af13429, 0x587cc557, 0x715bb51e, 0x4b7b2dee, 0x21c25d4a, 0xa7ad64bc,
      0x8550d11a, 0xaa0584c2, 0xc54a90f6, 0x56dac0e6, 0xb330ca09, 0x32bdb916,
      0x32fecae3, 0xac265555, 0x154832ce, 0x173833b3, 0x93d6b7ec, 0x48f4da12,
      0xe1b2a960, 0x334e9dad, 0x5963a625, 0x8b1188b4, 0x92f36aff, 0x5affb5d5,
      0x594b0ff2, 0x55df357c, 0x3eb13a42, 0xbfda0b95, 0x4711d216, 0xa2ba4c78,
      0x72aa2498, 0x2234501a, 0xe4191edf, 0x78b2af39, 0x49bbab56, 0x5c77a159,
      0x65090c79, 0x77922016, 0xf939b071, 0x119a93a8, 0x6e1d2382, 0x1035a828,
      0xecd5cfdd, 0x69445b09, 0x511acf08, 0x2b829975, 0xfe237138, 0x4c3f8400,
      0x4f58d3fd, 0x268d9c4a, 0x276bf979, 0x5c014d,   0xce7843c3, 0x75f260d5,
      0x96a4ddd7, 0xd20401dc, 0x40a10a62, 0x346d709f, 0x4ff1fef0, 0x8693c44a,
      0xcb2cc2ee, 0x374b3ed,  0x68468395, 0xccc2f005, 0x519662d6, 0x916206b7,
      0xc848436d, 0x6b1f74c4, 0x83270055, 0xfca47d39, 0x5762f825, 0x62b7208c,
      0x37135e9d, 0x6c809d8,  0x124ebd48, 0x7c750336, 0x65819924, 0x508480d0,
      0xcdb6eff7, 0xa78fc001, 0xb1c9859c, 0x81671970, 0x9a13f65d, 0x6a7f1cc5,
      0x8c19db11, 0x6d9a08ad, 0x910f2803, 0x871c40a0, 0x8e848711, 0x411a619c,
      0xe5407f51, 0x8de8751,  0x4ac734f6, 0xe0fef8ad, 0xc7ac42cb, 0x37c18e46,
      0x6d807d7,  0x69560aca, 0x9594d50a, 0xa649776d, 0x2d7ba7c0, 0x3faa3445,
      0x797b7667, 0x27eef114, 0x19b6451e, 0x86362e44, 0xbe39b056, 0x2c160a22,
      0xff639de,  0x9f104b20, 0xbe3888cf, 0x3e7029e2, 0x534f2ad7, 0x9acd2608,
      0xd6991e4c, 0x4d39fdf4, 0x7a24578b, 0x14b23b80, 0x5499c40f, 0x364d03a8,
      0x3018421d, 0x4618640c, 0xc4444c95, 0xbaa5dc49, 0x425e8c43, 0xf86d2c98,
      0x9799ae00, 0x1c432a45, 0x5d7a09c0, 0xf7707c0d, 0x4519c7c7, 0xc5e9216e,
      0x29887cc0, 0xb1629ac8, 0xc3b05674, 0xc37068b5, 0xc78e9ac7, 0x86f724fe,
      0x98697e45, 0x32f9304d, 0x9fe224c3, 0x678a4fa8, 0x1326fce0, 0x436288dd,
      0x29078f1a, 0xf4d02a88, 0xd1102148, 0xf48a1a4c, 0x48a61017, 0x74733de7,
      0x32988e28, 0x998f4b01, 0x350aaac0, 0x434430b1, 0xf01e1dcc, 0x4f3d81a4,
      0x4e453e6d, 0xc53152b9, 0xa60093ef, 0xd24cef18, 0x2738dc8a, 0xd7d403b4,
      0x6a540c9a, 0x5a656beb, 0x348e9a1f, 0x81e9f4f6, 0xe4a9553a, 0x4956fcf1,
      0x2ea7715d, 0x2f35372,  0xb11b11ef, 0x5ed86f01, 0x1ce4c3ce, 0x10b4fdb1,
      0x478f3f9,  0x63ddce14, 0xadab37b4, 0xb846683b, 0xf67f9108, 0x9fb9331,
      0x191d1433, 0xbefc9902, 0x362a78bf, 0xa13afe50, 0x998360c0, 0x31072c66,
      0x4c549728, 0xa153a412, 0x1444957e, 0xed100fff, 0x1ba997c5, 0x67f0ccc5,
      0x3807365,  0x14a20db0, 0x58cef4d9, 0x46c03562, 0xa967a606, 0x8662775,
      0x699c0e61, 0x9f294be,  0xb24f5172, 0xe716cf04, 0x3036ea52, 0x29560a3b,
      0x6b340994, 0x70653f08, 0x3c9d025a, 0x6c3f5990, 0xe2ca9247, 0x86559a50,
      0xb675ba86, 0xd07bab9b, 0x3071b26e, 0x9e804cca, 0x809394b8, 0xf68ed56a,
      0x2def7697, 0x8cbc3bef, 0x51dee763, 0xdaee1e39, 0x455b41fb, 0xcf947d0b,
      0xb8d63d24, 0x16b07af0, 0x81444a3f, 0x5d8323ed, 0x8d350669, 0x125fc45,
      0xb29099c0, 0x631cadc1, 0xebf50c72, 0xe025db42, 0x22cc4e90, 0x39a9b527,
      0x412e75a6, 0x18a74ff,  0x64c17eaf, 0x8dda3d2b, 0x804f908e, 0xae5cbcbe,
      0x466a10e3, 0x75d53918, 0x9bbe00bb, 0x73df5ed7, 0xd5199150, 0x3872d538,
      0xac53fd15, 0x3f650dff};
#endif

  ExplicitSeedSeq seed_sequence{12, 34, 56};
  randen_u32 engine(seed_sequence);
#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  for (size_t i = 0; i < 2 * kNumGoldenOutputs; ++i) {
    printf("0x%08x, ", engine());
    if (i % 6 == 5) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
  engine.seed(seed_sequence);
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

TEST(RandenTest, VerifyGoldenFromDeserializedEngine) {
#ifdef ABSL_IS_LITTLE_ENDIAN
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0x067f9f9ab919657a, 0x0534605912988583, 0x8a303f72feaa673f,
      0x77b7fd747909185c, 0xd9af90403c56d891, 0xd939c6cb204d14b5,
      0x7fbe6b954a47b483, 0x8b31a47cc34c768d, 0x3a9e546da2701a9c,
      0x5246539046253e71, 0x417191ffb2a848a1, 0x7b1c7bf5a5001d09,
      0x9489b15d194f2361, 0xfcebdeea3bcd2461, 0xd643027c854cec97,
      0x5885397f91e0d21c, 0x53173b0efae30d58, 0x1c9c71168449fac1,
      0xe358202b711ed8aa, 0x94e3918ed1d8227c, 0x5bb4e251450144cf,
      0xb5c7a519b489af3b, 0x6f8b560b1f7b3469, 0xfde11dd4a1c74eef,
      0x33383d2f76457dcf, 0x3060c0ec6db9fce1, 0x18f451fcddeec766,
      0xe73c5d6b9f26da2a, 0x8d4cc566671b32a4, 0xb8189b73776bc9ff,
      0x497a70f9caf0bc23, 0x23afcc509791dcea, 0x18af70dc4b27d306,
      0xd3853f955a0ce5b9, 0x441db6c01a0afb17, 0xd0136c3fb8e1f13f,
      0x5e4fd6fc2f33783c, 0xe0d24548adb5da51, 0x0f4d8362a7d3485a,
      0x9f572d68270fa563, 0x6351fbc823024393, 0xa66dbfc61810e9ab,
      0x0ff17fc14b651af8, 0xd74c55dafb99e623, 0x36303bc1ad85c6c2,
      0x4920cd6a2af7e897, 0x0b8848addc30fecd, 0x9e1562eda6488e93,
      0x197553807d607828, 0xbef5eaeda5e21235, 0x18d91d2616aca527,
      0xb7821937f5c873cd, 0x2cd4ae5650dbeefc, 0xb35a64376f75ffdf,
      0x9226d414d647fe07, 0x663f3db455bbb35e, 0xa829eead6ae93247,
      0x7fd69c204dd0d25f, 0xbe1411f891c9acb1, 0xd476f34a506d5f11,
      0xf423d2831649c5ca, 0x1e503962951abd75, 0xeccc9e8b1e34b537,
      0xb11a147294044854, 0xc4cf27f0abf4929d, 0xe9193abf6fa24c8c,
      0xa94a259e3aba8808, 0x21dc414197deffa3, 0xa2ae211d1ff622ae,
      0xfe3995c46be5a4f4, 0xe9984c284bf11128, 0xcb1ce9d2f0851a80,
      0x42fee17971d87cd8, 0xac76a98d177adc88, 0xa0973b3dedc4af6f,
      0xdf56d6bbcb1b8e86, 0xf1e6485f407b11c9, 0x2c63de4deccb15c0,
      0x6fe69db32ed4fad7, 0xaa51a65f84bca1f1, 0x242f2ee81d608afc,
      0x8eb88b2b69fc153b, 0x22c20098baf73fd1, 0x57759466f576488c,
      0x075ca562cea1be9d, 0x9a74814d73d28891, 0x73d1555fc02f4d3d,
      0xc17f8f210ee89337, 0x46cca7999eaeafd4, 0x5db8d6a327a0d8ac,
      0xb79b4f93c738d7a1, 0x9994512f0036ded1, 0xd3883026f38747f4,
      0xf31f7458078d097c, 0x736ce4d480680669, 0x7a496f4c7e1033e3,
      0xecf85bf297fbc68c, 0x9e37e1d0f24f3c4e, 0x15b6e067ca0746fc,
      0xdd4a39905c5db81c, 0xb5dfafa7bcfdf7da, 0xca6646fb6f92a276,
      0x1c6b35f363ef0efd, 0x6a33d06037ad9f76, 0x45544241afd8f80f,
      0x83f8d83f859c90c5, 0x22aea9c5365e8c19, 0xfac35b11f20b6a6a,
      0xd1acf49d1a27dd2f, 0xf281cd09c4fed405, 0x076000a42cd38e4f,
      0x6ace300565070445, 0x463a62781bddc4db, 0x1477126b46b569ac,
      0x127f2bb15035fbb8, 0xdfa30946049c04a8, 0x89072a586ba8dd3e,
      0x62c809582bb7e74d, 0x22c0c3641406c28b, 0x9b66e36c47ff004d,
      0xb9cd2c7519653330, 0x18608d79cd7a598d, 0x92c0bd1323e53e32,
      0x887ff00de8524aa5, 0xa074410b787abd10, 0x18ab41b8057a2063,
      0x1560abf26bc5f987};
#else
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0xbb5b5c74b6132ca9, 0xcc0209bcc0053553, 0xca8f9b35c0ab8e01,
      0x78624b694c9d84b6, 0xfd20d8cccdc1f97f, 0xc33380b615040476,
      0xdafb45761ec51c7f, 0x1f4e459ae16b0316, 0xa9a7936ba4f4a497,
      0x47eb97e4bb98d66f, 0xb8097f8e9c2bf4f3, 0x963b1052980daa03,
      0x89b41894715479e7, 0x364a061e5f4054fc, 0x9e094ec344beb5ce,
      0xd68bdc9ea174d1ae, 0xbb61115b9a067d01, 0x5ecb3fe0e75713ad,
      0x8b362b82083e17a1, 0xe3ba1ad9683257b8, 0xf4699779c32409c6,
      0x25a2e905fab261b4, 0xf0a78b8e50551cf1, 0xd15eccfc47096fdd,
      0xd8891209516a6c46, 0xb3c7b8bbb3b3ff8e, 0x4f735a59ec325ae3,
      0x873afa811c8d483b, 0x8df30b76065c84d8, 0xad6295dec4f68a62,
      0xca46d4ec3480a19f, 0xc4d90389fb874cea, 0x650b60a2a77f97e,
      0xab74a35028d970e9, 0xd0f9688f494537cf, 0x84db3b594eabd8dc,
      0x88b0983f8f12240d, 0xf8a5452fc900192e, 0xb50129b76905e2c6,
      0x5aefaa7235b6652a, 0xd20619460e8ee201, 0x20c73f36dee0b906,
      0x9141ba1b6f4db855, 0xb553cf86f0ef094d, 0xfa74eb5393d3a88e,
      0xe21a407d72558c2a, 0xf91d513091e521a2, 0x96c41daf1046fa0a,
      0x8f10c9d82da2b672, 0xde42b0a867f38a8a, 0xac5a58d95b0db4d6,
      0x6690e470e8f8fd42, 0x410f86ab0a4a49d7, 0x20ac29ab1a072950,
      0xc67669cc4694c9a7, 0x73045b3abd0ea86c, 0xba3d662718e6e4e3,
      0xd2035d5d918bcab6, 0xd28958a3ed88fbb7, 0x3d954e7e5d692082,
      0xd5e8e1ea6ccff52b, 0x715f41bb275a5d7c, 0x52b1e38bbfd40189,
      0x9452316308899a1a, 0x8d76fc6faec3c3fb, 0xe0270c20c6276fbb,
      0x6a311898d934a6d3, 0x83a07703177b8e79, 0x6ac421157af16982,
      0xbf18896e13cdfcfc, 0xf42fb47fb1616984, 0xa762e05e182c4fd1,
      0xf1d9b5d760521ee1, 0x48fabf3ba7830580, 0x31af11713583f61b,
      0x241726d302f93101, 0xc6856df8ace488d9, 0x380de799ec55f826,
      0x8e0b0152906fa032, 0xd965988a6572ec1c, 0xd3bac15d1e98edc8,
      0x15ee5db4e8f6bf6f, 0x6a0c3d4ec28db8c5, 0xb3037f91e9e61e,
      0xee9ac4d82f30e2e2, 0xa4dc12f3b64dcb54, 0x4d3bc07fe683fe4c,
      0x1c09397c5bccbdb5, 0x275c031214e031f9, 0x3b24f2c03b85f802,
      0x26dbab8b2fe1e207, 0xf70369e7423e8fee, 0xe3d99fcca25280cb,
      0x7226bd1fca4e273a, 0xcdaae1be32ed8e07, 0x60090825e5ad94a8,
      0xa790dfd4c0773fef, 0x22337eacc924127b, 0x2319ff3cd1f48bd5,
      0x33f6804d78657c25, 0x5597f711be9bdd35, 0xbb9c23c588c0f407,
      0x76201ca915eeb9b0, 0x220c873d4fdd0c6e, 0x23361f70e00b4a64,
      0x7791cd37ca8da79d, 0xff7d9a76050a69bd, 0x344f4cabc813fb7d,
      0x5e4286ca063aeee9, 0x7ab6bbd01f50f2c4, 0x435325c4d3b7091d,
      0x413d981e9732ef0d, 0xcccd4b17c70f7160, 0x15cdda2afa809ef8,
      0xcf271671d734233c, 0xe8ddc5604d13f78d, 0x7f6d6acef2eb6125,
      0x8754385cc6575486, 0x3fbcc834bbe350d1, 0xdef8544b125f13df,
      0xd7e4347d02ecc444, 0xd855cc9471360ac2, 0x29ab82a1711fa404,
      0x56adc17ccc285882, 0x23dbda38140b9914, 0xb990e1e853ac540e,
      0x6f90693462fe5a4d};
#endif

#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  std::seed_seq seed_sequence{1, 2, 3, 4, 5};
  randen_u64 engine(seed_sequence);
  std::ostringstream stream;
  stream << engine;
  auto str = stream.str();
  printf("%s\n\n", str.c_str());
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    printf("0x%016lx, ", engine());
    if (i % 3 == 2) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  randen_u64 engine;
  std::istringstream stream(
      "0 0 9824501439887287479 3242284395352394785 243836530774933777 "
      "4047941804708365596 17165468127298385802 949276103645889255 "
      "10659970394998657921 1657570836810929787 11697746266668051452 "
      "9967209969299905230 14140390331161524430 7383014124183271684 "
      "13146719127702337852 13983155220295807171 11121125587542359264 "
      "195757810993252695 17138580243103178492 11326030747260920501 "
      "8585097322474965590 18342582839328350995 15052982824209724634 "
      "7321861343874683609 1806786911778767826 10100850842665572955 "
      "9249328950653985078 13600624835326909759 11137960060943860251 "
      "10208781341792329629 9282723971471525577 16373271619486811032 32");
  stream >> engine;
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

TEST(RandenTest, IsFastOrSlow) {
  // randen_engine typically costs ~5ns per value for the optimized code paths,
  // and the ~1000ns per value for slow code paths.  However when running under
  // msan, asan, etc. it can take much longer.
  //
  // The estimated operation time is something like:
  //
  // linux, optimized ~5ns
  // ppc, optimized ~7ns
  // nacl (slow), ~1100ns
  //
  // `kCount` is chosen below so that, in debug builds and without hardware
  // acceleration, the test (assuming ~1us per call) should finish in ~0.1s
  static constexpr size_t kCount = 100000;
  randen_u64 engine;
  randen_u64::result_type sum = 0;
  auto start = absl::GetCurrentTimeNanos();
  for (int i = 0; i < kCount; i++) {
    sum += engine();
  }
  auto duration = absl::GetCurrentTimeNanos() - start;

  ABSL_INTERNAL_LOG(INFO, absl::StrCat(static_cast<double>(duration) /
                                           static_cast<double>(kCount),
                                       "ns"));

  EXPECT_GT(sum, 0);
  EXPECT_GE(duration, kCount);  // Should be slower than 1ns per call.
}

}  // namespace
