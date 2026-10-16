// really small test framework so i dont need gtest
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}
inline int& failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::printf("  FAIL %s:%d: %s\n", file, line, msg.c_str());
}

}  // namespace check

#define TEST(name)                                          \
    static void name();                                     \
    static check::Registrar name##_registrar(#name, name);  \
    static void name()

template <class A, class B>
void check_eq(const char* file, int line, const char* expr, const A& a, const B& b) {
    if (!(a == b))
        check::fail(file, line, std::string(expr) + "  (" + std::to_string(a) + " vs " +
                                    std::to_string(b) + ")");
}

// has to be variadic becuase of the comma in Q<8, 8>
#define CHECK(...) \
    do { if (!(__VA_ARGS__)) check::fail(__FILE__, __LINE__, #__VA_ARGS__); } while (0)
#define CHECK_EQ(...) check_eq(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
