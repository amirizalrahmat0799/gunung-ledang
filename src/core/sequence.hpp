#pragma once
#include <cstdint>

namespace ledang {

/// True if 16-bit sequence a is newer than b, correctly across wrap-around (65535 -> 0).
constexpr bool seq_newer(uint16_t a, uint16_t b) {
    return ((a > b) && (a - b <= 32768)) || ((a < b) && (b - a > 32768));
}

/// Signed distance a - b for wrapping 16-bit sequences.
constexpr int seq_diff(uint16_t a, uint16_t b) {
    return static_cast<int16_t>(static_cast<uint16_t>(a - b));
}

}  // namespace ledang
