// fixed point type that behaves like an FPGA register
// raw integer of W bits, value = raw / 2^F
// Q format is ARM style so Q1.15 and Q8.8 are both 16 bits (sign bit counts as int bit)
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace fxp {

using i128 = __int128;

enum class Sign { Signed, Unsigned };

// what to do with the bits below the LSB
enum class Round {
    Floor,       // just drop them, same as truncating in RTL
    TowardZero,  // what a normal signed divider gives
    HalfUp,      // add half an LSB then truncate
    HalfEven,    // round to even on ties
    HalfAway,    // same as std::round
};

enum class Overflow { Saturate, Wrap };

namespace detail {

constexpr i128 pow2(int n) { return i128{1} << n; }

// n / d rounded to an integer, d has to be positive
constexpr i128 round_div(i128 n, i128 d, Round r) {
    i128 q = n / d;
    if (n % d != 0 && n < 0) --q;  // c++ divides toward zero, want floor
    const i128 rem = n - q * d;    // always 0 <= rem < d now
    if (rem == 0) return q;
    switch (r) {
        case Round::Floor: return q;
        case Round::TowardZero: return n < 0 ? q + 1 : q;
        case Round::HalfUp: return 2 * rem >= d ? q + 1 : q;
        case Round::HalfAway: return (2 * rem > d || (2 * rem == d && n >= 0)) ? q + 1 : q;
        case Round::HalfEven: return (2 * rem > d || (2 * rem == d && (q & 1))) ? q + 1 : q;
    }
    return q;
}

}  // namespace detail

template <int W, int F, Sign S = Sign::Signed, Round R = Round::HalfUp,
          Overflow O = Overflow::Saturate>
class Fixed {
    static_assert(W >= 1 && W <= 32, "width has to be 1 to 32 bits");
    static_assert(F >= 0 && F <= 32, "frac bits has to be 0 to 32");

public:
    static constexpr int width = W;
    static constexpr int frac_bits = F;
    static constexpr int int_bits = W - F;
    static constexpr bool is_signed = S == Sign::Signed;
    static constexpr Round rounding = R;
    static constexpr Overflow overflow = O;
    static constexpr int64_t raw_min = is_signed ? -(int64_t{1} << (W - 1)) : 0;
    static constexpr int64_t raw_max =
        is_signed ? (int64_t{1} << (W - 1)) - 1 : (int64_t{1} << W) - 1;
    static constexpr uint64_t mask = (uint64_t{1} << W) - 1;

    constexpr Fixed() = default;

    // load a raw value, if its out of range it saturates/wraps
    static constexpr Fixed from_raw(int64_t raw, bool* overflowed = nullptr) {
        return Fixed(fit(raw, overflowed));
    }

    // value is n / 2^frac, round it down to F bits then fit it
    static constexpr Fixed from_scaled(i128 n, int frac, bool* overflowed = nullptr) {
        const int shift = frac - F;
        if (shift > 0)
            n = detail::round_div(n, detail::pow2(shift), R);
        else
            n *= detail::pow2(-shift);
        return Fixed(fit(n, overflowed));
    }

    // x_fixed = round(x * 2^F) then saturate. NaN just gives 0
    static Fixed from_double(double x, bool* overflowed = nullptr) {
        if (std::isnan(x)) return Fixed{};
        constexpr double lim = 0x1p100;  // way bigger then any register, still fits in i128
        double v = std::ldexp(x, F);
        if (v > lim) v = lim;
        if (v < -lim) v = -lim;
        const double q = std::floor(v);
        const double rem = v - q;  // this is exact
        bool up = false;
        if (rem != 0.0) {
            switch (R) {
                case Round::Floor: up = false; break;
                case Round::TowardZero: up = v < 0; break;
                case Round::HalfUp: up = rem >= 0.5; break;
                case Round::HalfAway: up = rem > 0.5 || (rem == 0.5 && v >= 0); break;
                case Round::HalfEven:
                    up = rem > 0.5 || (rem == 0.5 && std::fmod(q, 2.0) != 0.0);
                    break;
            }
        }
        return Fixed(fit(static_cast<i128>(q) + (up ? 1 : 0), overflowed));
    }

    static constexpr Fixed max() { return Fixed(raw_max); }
    static constexpr Fixed min() { return Fixed(raw_min); }
    static constexpr Fixed lsb() { return from_raw(1); }
    static constexpr double resolution() { return 1.0 / static_cast<double>(uint64_t{1} << F); }

    constexpr int64_t raw() const { return raw_; }
    // bits like they would show up on the bus (negative numbers masked)
    constexpr uint64_t bits() const { return static_cast<uint64_t>(raw_) & mask; }
    double to_double() const { return std::ldexp(static_cast<double>(raw_), -F); }

    static std::string format_name() {
        return (is_signed ? "Q" : "UQ") + std::to_string(int_bits) + "." + std::to_string(F);
    }

    // same format ops, result goes back into the same size register
    friend constexpr Fixed operator+(Fixed a, Fixed b) {
        return from_scaled(i128{a.raw_} + b.raw_, F);
    }
    friend constexpr Fixed operator-(Fixed a, Fixed b) {
        return from_scaled(i128{a.raw_} - b.raw_, F);
    }
    friend constexpr Fixed operator*(Fixed a, Fixed b) {
        return from_scaled(i128{a.raw_} * b.raw_, 2 * F);
    }
    constexpr Fixed operator-() const { return from_scaled(-i128{raw_}, F); }
    constexpr Fixed& operator+=(Fixed b) { return *this = *this + b; }
    constexpr Fixed& operator-=(Fixed b) { return *this = *this - b; }
    constexpr Fixed& operator*=(Fixed b) { return *this = *this * b; }

    friend constexpr auto operator<=>(const Fixed&, const Fixed&) = default;

private:
    explicit constexpr Fixed(int64_t raw) : raw_(raw) {}

    static constexpr int64_t fit(i128 v, bool* overflowed) {
        const bool ovf = v < raw_min || v > raw_max;
        if (overflowed) *overflowed = ovf;
        if (!ovf) return static_cast<int64_t>(v);
        if constexpr (O == Overflow::Saturate) {
            return static_cast<int64_t>(v < raw_min ? raw_min : raw_max);
        } else {
            // keep the low W bits and sign extend
            int64_t low = static_cast<int64_t>(static_cast<uint64_t>(v) & mask);
            if (is_signed && (low >> (W - 1)) & 1) low -= int64_t{1} << W;
            return low;
        }
    }

    int64_t raw_ = 0;
};

// Q<8, 8> = Q8.8, UQ<10, 6> = UQ10.6
template <int M, int N, Round R = Round::HalfUp, Overflow O = Overflow::Saturate>
using Q = Fixed<M + N, N, Sign::Signed, R, O>;
template <int M, int N, Round R = Round::HalfUp, Overflow O = Overflow::Saturate>
using UQ = Fixed<M + N, N, Sign::Unsigned, R, O>;

template <class T>
concept FixedPoint = requires {
    T::frac_bits;
    T::width;
};

// ops accross different formats. Out is the output register and decides
// rounding + overflow, kind of like picking the output width in RTL

template <FixedPoint Out, FixedPoint A>
constexpr Out resize(A a, bool* overflowed = nullptr) {
    return Out::from_scaled(a.raw(), A::frac_bits, overflowed);
}

template <FixedPoint Out, FixedPoint A, FixedPoint B>
constexpr Out add(A a, B b, bool* overflowed = nullptr) {
    // line up the binary points first
    constexpr int f = A::frac_bits > B::frac_bits ? A::frac_bits : B::frac_bits;
    const i128 n = i128{a.raw()} * detail::pow2(f - A::frac_bits) +
                   i128{b.raw()} * detail::pow2(f - B::frac_bits);
    return Out::from_scaled(n, f, overflowed);
}

template <FixedPoint Out, FixedPoint A, FixedPoint B>
constexpr Out sub(A a, B b, bool* overflowed = nullptr) {
    constexpr int f = A::frac_bits > B::frac_bits ? A::frac_bits : B::frac_bits;
    const i128 n = i128{a.raw()} * detail::pow2(f - A::frac_bits) -
                   i128{b.raw()} * detail::pow2(f - B::frac_bits);
    return Out::from_scaled(n, f, overflowed);
}

template <FixedPoint Out, FixedPoint A, FixedPoint B>
constexpr Out mul(A a, B b, bool* overflowed = nullptr) {
    return Out::from_scaled(i128{a.raw()} * b.raw(), A::frac_bits + B::frac_bits, overflowed);
}

// a / b, only rounds once at the end so no extra error
// divide by zero saturates to max (or min if a is negative)
template <FixedPoint Out, FixedPoint A, FixedPoint B>
constexpr Out div(A a, B b, bool* overflowed = nullptr) {
    if (b.raw() == 0) {
        if (overflowed) *overflowed = true;
        return a.raw() < 0 ? Out::min() : Out::max();
    }
    // a/b * 2^Fo = ra * 2^(Fb + Fo) / (rb * 2^Fa)
    i128 num = i128{a.raw()} * detail::pow2(B::frac_bits + Out::frac_bits);
    i128 den = i128{b.raw()} * detail::pow2(A::frac_bits);
    if (den < 0) {
        num = -num;
        den = -den;
    }
    return Out::from_scaled(detail::round_div(num, den, Out::rounding), Out::frac_bits,
                            overflowed);
}

}  // namespace fxp
