#pragma once
// A minimal test framework: TEST(name) { CHECK(cond); CHECK_EQ(a, b); }

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}
inline int& failures() {
    static int n = 0;
    return n;
}

struct Register {
    Register(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

template <typename T>
std::string show(const T& v) {
    if constexpr (requires(std::ostream& o) { o << v; }) {
        std::ostringstream s;
        s << +v;
        return s.str();
    } else {
        return "<value>";
    }
}

}  // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                           \
    static void TEST_CAT(test_fn_, name)();                                  \
    static test::Register TEST_CAT(test_reg_, name)(#name, &TEST_CAT(test_fn_, name)); \
    static void TEST_CAT(test_fn_, name)()

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            ++test::failures();                                                           \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                                    \
    do {                                                                                                  \
        const auto va_ = (a);                                                                            \
        const auto vb_ = (b);                                                                            \
        if (!(va_ == vb_)) {                                                                              \
            ++test::failures();                                                                           \
            std::printf("  FAILED %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__, #a, #b,              \
                        test::show(va_).c_str(), test::show(vb_).c_str());                                \
        }                                                                                                 \
    } while (0)
