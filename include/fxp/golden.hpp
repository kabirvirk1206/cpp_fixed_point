// compare fixed point results against a double "golden" model
#pragma once

#include <cmath>
#include <cstddef>
#include <string>

namespace fxp {

// keeps track of |golden - fixed|, reported in LSBs of the output
struct ErrorStats {
    std::string name;
    double lsb = 1.0;
    std::size_t samples = 0;
    std::size_t saturated = 0;  // these dont count in the error
    double max_abs = 0.0;
    double sum_abs = 0.0;
    double sum_sq = 0.0;

    void record(double golden, double fixed) {
        const double e = std::fabs(golden - fixed);
        ++samples;
        sum_abs += e;
        sum_sq += e * e;
        if (e > max_abs) max_abs = e;
    }
    void record_saturation() { ++saturated; }

    double max_lsb() const { return max_abs / lsb; }
    double mean_lsb() const { return samples ? sum_abs / samples / lsb : 0.0; }
    double rms_lsb() const { return samples ? std::sqrt(sum_sq / samples) / lsb : 0.0; }
};

template <class T>
ErrorStats make_stats(std::string name) {
    ErrorStats s;
    s.name = std::move(name);
    s.lsb = T::resolution();
    return s;
}

// error = |x - x_fixed / 2^F|
template <class T>
double quantization_error(double x) {
    return std::fabs(x - T::from_double(x).to_double());
}

}  // namespace fxp
