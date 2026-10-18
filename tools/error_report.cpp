// prints max error (in LSBs) for each format and op as a markdown table
// op  = vs exact math on the already quantized inputs (just what the op loses)
// e2e = vs double math on the original inputs (includes input quantization too)
// usage: error_report [samples] [seed]
#include <cstdio>
#include <cstdlib>
#include <random>

#include "fxp/fixed.hpp"
#include "fxp/golden.hpp"

using namespace fxp;

struct Row {
    ErrorStats op, e2e;
};

static void print_row(const std::string& fmt, const char* op, const Row& r) {
    std::printf("| %-7s | %-8s | %8.4f | %8.4f | %8.4f | %8.4f | %6zu |\n", fmt.c_str(), op,
                r.op.max_lsb(), r.op.rms_lsb(), r.e2e.max_lsb(), r.e2e.rms_lsb(), r.op.saturated);
}

template <class T>
void report(std::size_t n, std::mt19937_64& rng) {
    const double lo = T::min().to_double(), hi = T::max().to_double();
    auto uni = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    auto rows = [] { return Row{make_stats<T>("op"), make_stats<T>("e2e")}; };
    Row rq = rows(), ra = rows(), rm = rows(), rd = rows();

    // keep mul inputs small enough so the product fits
    const double mhi = std::sqrt(hi), mlo = T::is_signed ? -mhi : 0.0;

    for (std::size_t i = 0; i < n; ++i) {
        // quantize
        const double x = uni(lo, hi);
        rq.op.record(x, T::from_double(x).to_double());
        rq.e2e.record(x, T::from_double(x).to_double());

        // add, half range so the sum fits
        {
            const double a = uni(lo / 2, hi / 2), b = uni(lo / 2, hi / 2);
            const T fa = T::from_double(a), fb = T::from_double(b);
            bool ovf = false;
            const T r = add<T>(fa, fb, &ovf);
            if (ovf) { ra.op.record_saturation(); }
            else {
                ra.op.record(fa.to_double() + fb.to_double(), r.to_double());
                ra.e2e.record(a + b, r.to_double());
            }
        }
        // multiply
        {
            const double a = uni(mlo, mhi), b = uni(mlo, mhi);
            const T fa = T::from_double(a), fb = T::from_double(b);
            bool ovf = false;
            const T r = mul<T>(fa, fb, &ovf);
            if (ovf) { rm.op.record_saturation(); }
            else {
                rm.op.record(fa.to_double() * fb.to_double(), r.to_double());
                rm.e2e.record(a * b, r.to_double());
            }
        }
        // divide, pick the quotient first so it stays in range. small divisors blow up the e2e error so skip those
        {
            double a, b;
            do {
                b = uni(lo, hi);
                a = uni(lo / 2, hi / 2) * b;
            } while (std::fabs(b) < hi / 16 || a < lo || a > hi);
            const T fa = T::from_double(a), fb = T::from_double(b);
            bool ovf = false;
            const T r = div<T>(fa, fb, &ovf);
            if (ovf) { rd.op.record_saturation(); }
            else {
                rd.op.record(fa.to_double() / fb.to_double(), r.to_double());
                rd.e2e.record(a / b, r.to_double());
            }
        }
    }
    const std::string name = T::format_name();
    print_row(name, "quantize", rq);
    print_row(name, "add", ra);
    print_row(name, "mul", rm);
    print_row(name, "div", rd);
}

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 100000;
    std::mt19937_64 rng(argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1);

    std::printf("Error vs double golden model, %zu samples per op, round-half-up, saturating.\n\n", n);
    std::printf("| format  | op       | op max   | op rms   | e2e max  | e2e rms  | sat    |\n");
    std::printf("|---------|----------|----------|----------|----------|----------|--------|\n");
    report<Q<1, 15>>(n, rng);
    report<Q<4, 12>>(n, rng);
    report<Q<8, 8>>(n, rng);
    report<Q<16, 16>>(n, rng);
    report<UQ<8, 8>>(n, rng);
    report<UQ<10, 6>>(n, rng);
    std::printf("\nAll errors in LSBs of the output format.\n");
}
