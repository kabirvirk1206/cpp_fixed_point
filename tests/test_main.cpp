#include <cstdio>

#include "check.hpp"

int main() {
    for (const auto& c : check::registry()) {
        const int before = check::failures();
        c.fn();
        std::printf("%s %s\n", check::failures() == before ? "[ OK ]" : "[FAIL]", c.name);
    }
    std::printf("\n%zu tests, %d failed checks\n", check::registry().size(), check::failures());
    return check::failures() == 0 ? 0 : 1;
}
