// tests for add, sub, mul, div and resize
#include <random>

#include "check.hpp"
#include "fxp/fixed.hpp"

using namespace fxp;

TEST(add_saturates_and_wraps) {
    using S = Q<8, 8>;
    CHECK(S::max() + S::lsb() == S::max());
    CHECK(S::min() - S::lsb() == S::min());
    CHECK(-S::min() == S::max());  // -min doesnt fit so it saturates
    using W = Q<8, 8, Round::HalfUp, Overflow::Wrap>;
    CHECK(W::max() + W::lsb() == W::min());
    CHECK(W::min() - W::lsb() == W::max());
    CHECK(-W::min() == W::min());
    using U = UQ<8, 8>;
    CHECK(U::from_double(1.0) - U::from_double(2.0) == U::min());
}

TEST(add_mixed_formats) {
    const auto a = Q<8, 8>::from_double(3.25);
    const auto b = Q<1, 15>::from_double(-0.125);
    CHECK(add<Q<16, 16>>(a, b).to_double() == 3.125);
    CHECK(sub<Q<16, 16>>(a, b).to_double() == 3.375);
    bool ovf = false;
    const auto r = add<Q<1, 15>>(a, b, &ovf);
    CHECK(ovf);
    CHECK(r == Q<1, 15>::max());
}

TEST(mul_exact_and_corners) {
    using T = Q<1, 15>;
    CHECK((T::from_double(0.5) * T::from_double(0.5)).to_double() == 0.25);
    CHECK((T::from_double(-0.5) * T::from_double(0.5)).to_double() == -0.25);
    // -1 * -1 = 1 which Q1.15 cant hold
    CHECK(T::min() * T::min() == T::max());
    // if the output is wide enough nothing is lost
    CHECK(mul<Q<2, 30>>(T::min(), T::min()).to_double() == 1.0);
    using U = Q<8, 8>;
    CHECK(U::from_double(100.0) * U::from_double(2.0) == U::max());
    CHECK(U::from_double(-100.0) * U::from_double(2.0) == U::min());
}

// raw * 0.5 lands right on a half LSB so this checks every rouding mode
template <Round R>
void check_mul_round(int r15, int r25, int rn15, int rn25) {
    using T = Q<8, 8, R>;
    const auto half = T::from_double(0.5);
    CHECK_EQ((T::from_raw(3) * half).raw(), r15);
    CHECK_EQ((T::from_raw(5) * half).raw(), r25);
    CHECK_EQ((T::from_raw(-3) * half).raw(), rn15);
    CHECK_EQ((T::from_raw(-5) * half).raw(), rn25);
}

TEST(mul_rounding) {
    check_mul_round<Round::Floor>(1, 2, -2, -3);
    check_mul_round<Round::TowardZero>(1, 2, -1, -2);
    check_mul_round<Round::HalfUp>(2, 3, -1, -2);
    check_mul_round<Round::HalfEven>(2, 2, -2, -2);
    check_mul_round<Round::HalfAway>(2, 3, -2, -3);
}

TEST(mul_error_within_half_lsb) {
    using T = Q<4, 12>;
    std::mt19937_64 rng(2);
    std::uniform_int_distribution<int64_t> d(-(1 << 13), 1 << 13);  // so a*b stays under 4
    double worst = 0;
    for (int i = 0; i < 200000; ++i) {
        const auto a = T::from_raw(d(rng));
        const auto b = T::from_raw(d(rng));
        const double exact = a.to_double() * b.to_double();
        const double e = std::fabs(exact - (a * b).to_double()) / T::resolution();
        if (e > worst) worst = e;
    }
    CHECK(worst <= 0.5);
}

TEST(div_rounding_and_sign) {
    using F = Q<8, 8, Round::Floor>;
    using Z = Q<8, 8, Round::TowardZero>;
    using H = Q<8, 8, Round::HalfUp>;
    const auto one = Q<8, 8>::from_double(1.0);
    const auto three = Q<8, 8>::from_double(3.0);
    const auto m_one = Q<8, 8>::from_double(-1.0);
    // 1/3 = 85.33 LSB, -1/3 = -85.33 LSB
    CHECK_EQ(div<F>(one, three).raw(), 85);
    CHECK_EQ(div<H>(one, three).raw(), 85);
    CHECK_EQ(div<F>(m_one, three).raw(), -86);
    CHECK_EQ(div<Z>(m_one, three).raw(), -85);
    CHECK_EQ(div<Z>(one, -three).raw(), -85);
    // 2/3 = 170.67 LSB
    CHECK_EQ(div<H>(one + one, three).raw(), 171);
    CHECK_EQ(div<F>(one + one, three).raw(), 170);
}

TEST(div_by_zero_saturates) {
    using T = Q<8, 8>;
    bool ovf = false;
    CHECK(div<T>(T::from_double(1.0), T{}, &ovf) == T::max());
    CHECK(ovf);
    CHECK(div<T>(T::from_double(-1.0), T{}) == T::min());
}

TEST(div_overflow_saturates) {
    using T = Q<8, 8>;
    bool ovf = false;
    CHECK(div<T>(T::from_double(100.0), T::from_double(0.25), &ovf) == T::max());
    CHECK(ovf);
}

TEST(div_error_bounds) {
    using T = Q<8, 8>;
    using TF = Q<8, 8, Round::Floor>;
    std::mt19937_64 rng(3);
    std::uniform_int_distribution<int64_t> d(T::raw_min, T::raw_max);
    double worst_h = 0, worst_f = 0;
    for (int i = 0; i < 200000; ++i) {
        const auto a = T::from_raw(d(rng));
        const auto b = T::from_raw(d(rng));
        if (b.raw() == 0) continue;
        const double exact = a.to_double() / b.to_double();
        if (exact >= T::max().to_double() || exact <= T::min().to_double()) continue;
        worst_h = std::max(worst_h, std::fabs(exact - div<T>(a, b).to_double()) / T::resolution());
        worst_f = std::max(worst_f, std::fabs(exact - div<TF>(a, b).to_double()) / T::resolution());
    }
    CHECK(worst_h <= 0.5);
    CHECK(worst_f < 1.0);
}

TEST(resize_rounds_and_saturates) {
    const auto x = Q<1, 15>::from_raw(3 << 6);  // 1.5 LSB of Q8.8
    CHECK_EQ(resize<Q<8, 8>>(x).raw(), 2);
    CHECK_EQ((resize<Q<8, 8, Round::Floor>>(x).raw()), 1);
    bool ovf = false;
    CHECK(resize<Q<1, 15>>(Q<8, 8>::from_double(2.0), &ovf) == Q<1, 15>::max());
    CHECK(ovf);
    CHECK(resize<Q<16, 16>>(Q<8, 8>::from_double(-7.75)).to_double() == -7.75);
}

// average = sum / count with integer inputs
TEST(div_integer_average) {
    using Sum = Fixed<32, 0, Sign::Unsigned>;
    using Count = Fixed<20, 0, Sign::Unsigned>;
    using Avg = UQ<10, 6, Round::Floor>;
    CHECK(div<Avg>(Sum::from_raw(10), Count::from_raw(4)).to_double() == 2.5);
    CHECK_EQ(div<Avg>(Sum::from_raw(10), Count::from_raw(3)).raw(), 213);  // 3.333 floors to 213/64
    CHECK(div<Avg>(Sum::from_raw(639), Count::from_raw(1)).to_double() == 639.0);
    bool ovf = false;
    CHECK(div<Avg>(Sum::from_raw(5000000), Count::from_raw(2), &ovf) == Avg::max());
    CHECK(ovf);
    CHECK(div<Avg>(Sum::from_raw(5), Count{}) == Avg::max());
}

// 1/2 LSB and 3/2 LSB results from a divide
TEST(div_ties) {
    using Int = Fixed<16, 0>;
    using E = Fixed<16, 0, Sign::Signed, Round::HalfEven>;
    using U = Fixed<16, 0, Sign::Signed, Round::HalfUp>;
    using A = Fixed<16, 0, Sign::Signed, Round::HalfAway>;
    const auto two = Int::from_raw(2);
    CHECK_EQ(div<E>(Int::from_raw(1), two).raw(), 0);
    CHECK_EQ(div<E>(Int::from_raw(3), two).raw(), 2);
    CHECK_EQ(div<U>(Int::from_raw(-1), two).raw(), 0);
    CHECK_EQ(div<A>(Int::from_raw(-1), two).raw(), -1);
    CHECK_EQ(div<A>(Int::from_raw(1), -two).raw(), -1);
}

TEST(mul_wraps) {
    using W = Q<8, 8, Round::HalfUp, Overflow::Wrap>;
    // 100 * 2 = 200, wraps around to 200 - 256 = -56
    CHECK((W::from_double(100.0) * W::from_double(2.0)).to_double() == -56.0);
}
