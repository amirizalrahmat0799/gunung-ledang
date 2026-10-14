#include "core/clock.hpp"

#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace ledang {

void sleep_until_seconds(double target) {
    for (;;) {
        const double remaining = target - now_seconds();
        if (remaining <= 0) return;
        if (remaining > 0.002) {
            std::this_thread::sleep_for(std::chrono::duration<double>(remaining - 0.0015));
        } else {
            std::this_thread::yield();
        }
    }
}

void enable_precise_timers() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
}

}  // namespace ledang
