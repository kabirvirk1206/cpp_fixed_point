// tests for converting doubles into fixed point
#include <random>

#include "check.hpp"
#include "fxp/fixed.hpp"
#include "fxp/golden.hpp"

using namespace fxp;

TEST(quantize_exact_values) {
    using T = Q<8, 8>;
    CHECK_EQ(T::from_double(1.5).raw(), 384);
    CHECK_EQ(T::from_double(1.0 / 256).raw(), 1);
    CHECK_EQ(T::from_double(-1.5).raw(), -384);
    CHECK_EQ(T::from_double(-1.5).bits(), 0xFE80u);  // should be masked to 16 bits
    CHECK_EQ(T::from_double(0.0).raw(), 0);
    CHECK_EQ(Q<1, 15>::from_double(-1.0).raw(), -32768);
}

TEST(quantize_saturates) {
    using T = Q<8, 8>;
    bool ovf = false;
    CHECK(T::from_double(1000.0, &ovf) == T::max());
    CHECK(ovf);
    CHECK(T::from_double(-1000.0) == T::min());
    CHECK_EQ(T::max().raw(), 32767);
    CHECK_EQ(T::min().raw(), -32768);
    // Q1.15 cant actually hold 1.0
    CHECK_EQ(Q<1, 15>::from_double(1.0, &ovf).raw(), 32767);
    CHECK(ovf);
    // unsigned clamps negative to 0
    CHECK_EQ(UQ<8, 8>::from_double(-3.0).raw(), 0);
    CHECK_EQ(UQ<8, 8>::from_double(300.0).raw(), 65535);
    // max + 0.6 LSB rounds up past max, should still saturate
    CHECK(T::from_double(T::max().to_double() + 0.6 / 256) == T::max());
}

TEST(quantize_wraps) {
    using T = Q<8, 8, Round::HalfUp, Overflow::Wrap>;
    bool ovf = false;
    CHECK_EQ(T::from_double(128.0, &ovf).raw(), -32768);
    CHECK(ovf);
    CHECK_EQ(T::from_double(-128.0 - 1.0 / 256).raw(), 32767);
    CHECK_EQ(T::from_double(256.0).raw(), 0);
    using U = UQ<4, 4, Round::HalfUp, Overflow::Wrap>;
    CHECK_EQ(U::from_double(16.0 + 0.25).raw(), 4);
    CHECK_EQ(U::from_double(-1.0 / 16).raw(), 255);
}

TEST(quantize_nan_is_zero) {
    CHECK_EQ(Q<8, 8>::from_double(std::nan("")).raw(), 0);
}

// check +-2.5 and +-3.5 LSB for each rounding mode
template <Round R>
void check_ties(int p25, int n25, int p35, int n35) {
    using T = Q<8, 8, R>;
    CHECK_EQ(T::from_double(2.5 / 256).raw(), p25);
    CHECK_EQ(T::from_double(-2.5 / 256).raw(), n25);
    CHECK_EQ(T::from_double(3.5 / 256).raw(), p35);
    CHECK_EQ(T::from_double(-3.5 / 256).raw(), n35);
}

TEST(quantize_rounding_ties) {
    check_ties<Round::Floor>(2, -3, 3, -4);
    check_ties<Round::TowardZero>(2, -2, 3, -3);
    check_ties<Round::HalfUp>(3, -2, 4, -3);
    check_ties<Round::HalfEven>(2, -2, 4, -4);
    check_ties<Round::HalfAway>(3, -3, 4, -4);
}

TEST(quantize_non_ties) {
    using F = Q<8, 8, Round::Floor>;
    using H = Q<8, 8, Round::HalfUp>;
    CHECK_EQ(F::from_double(2.9 / 256).raw(), 2);
    CHECK_EQ(F::from_double(-2.1 / 256).raw(), -3);
    CHECK_EQ(H::from_double(2.49 / 256).raw(), 2);
    CHECK_EQ(H::from_double(-2.51 / 256).raw(), -3);
}

template <class T>
double max_quant_error_lsb(std::mt19937_64& rng) {
    std::uniform_real_distribution<double> d(T::min().to_double(), T::max().to_double());
    double worst = 0;
    for (int i = 0; i < 200000; ++i) {
        const double e = quantization_error<T>(d(rng)) / T::resolution();
        if (e > worst) worst = e;
    }
    return worst;
}

TEST(quantize_error_bounds) {
    std::mt19937_64 rng(1);
    CHECK(max_quant_error_lsb<Q<8, 8>>(rng) <= 0.5);
    CHECK(max_quant_error_lsb<Q<1, 15, Round::HalfEven>>(rng) <= 0.5);
    CHECK(max_quant_error_lsb<Q<16, 16>>(rng) <= 0.5);
    const double floor_err = max_quant_error_lsb<Q<8, 8, Round::Floor>>(rng);
    CHECK(floor_err < 1.0);
    CHECK(floor_err > 0.9);  // floor should be close to a full LSB
}

TEST(format_names) {
    CHECK(Q<8, 8>::format_name() == "Q8.8");
    CHECK(Q<1, 15>::format_name() == "Q1.15");
    CHECK(UQ<10, 6>::format_name() == "UQ10.6");
}

// make sure it works at compile time too
static_assert(Q<8, 8>::from_raw(256) + Q<8, 8>::from_raw(128) == Q<8, 8>::from_raw(384));
static_assert(Q<8, 8>::max() + Q<8, 8>::lsb() == Q<8, 8>::max());
