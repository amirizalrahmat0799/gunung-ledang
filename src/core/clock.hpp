#pragma once
#include <chrono>
#include <cstdint>

namespace ledang {

using Clock = std::chrono::steady_clock;

/// Seconds since an arbitrary fixed point, as a double.
inline double now_seconds() {
    using namespace std::chrono;
    static const auto start = Clock::now();
    return duration<double>(Clock::now() - start).count();
}

/// Sleeps until the given now_seconds() time (coarse sleep, then a short spin for precision).
void sleep_until_seconds(double target);

/// Asks the OS for 1 ms timer resolution where that matters (Windows); no-op elsewhere.
void enable_precise_timers();

}  // namespace ledang
