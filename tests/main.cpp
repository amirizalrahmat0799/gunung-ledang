// ledang_tests [filter] [--exclude filter]: runs every test whose name contains the filter.
#include <cstring>

#include "net/socket.hpp"
#include "test.hpp"

int main(int argc, char** argv) {
    const char* include = nullptr;
    const char* exclude = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--exclude") == 0 && i + 1 < argc) {
            exclude = argv[++i];
        } else {
            include = argv[i];
        }
    }
    ledang::init_sockets();
    int ran = 0, failed_cases = 0;
    for (const auto& c : test::registry()) {
        if (include && !std::strstr(c.name, include)) continue;
        if (exclude && std::strstr(c.name, exclude)) continue;
        const int before = test::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        std::fflush(stdout);
        c.fn();
        const bool ok = test::failures() == before;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", c.name);
        ++ran;
        if (!ok) ++failed_cases;
    }
    ledang::shutdown_sockets();
    std::printf("\n%d test(s), %d failed\n", ran, failed_cases);
    return failed_cases == 0 && ran > 0 ? 0 : 1;
}
