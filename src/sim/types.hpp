#pragma once
// Shared simulation types. All positions and velocities are fixed-point integers (1/256 pixel), so the
// simulation produces bit-identical results on any compiler or CPU.

#include <cstdint>

namespace ledang {

using fx = int32_t;                 // 1/256 pixel
constexpr fx kFx = 256;
constexpr fx px(int pixels) { return pixels * kFx; }
constexpr int to_px(fx v) { return v >> 8; }  // floor

constexpr int kTile = 16;            // pixels
constexpr fx kTileFx = kTile * kFx;  // 4096 = 1 << 12
constexpr int kTileShift = 12;

constexpr int kTickRate = 60;        // simulation steps per second

enum Button : uint8_t {
    BtnLeft = 1 << 0,
    BtnRight = 1 << 1,
    BtnUp = 1 << 2,
    BtnDown = 1 << 3,
    BtnJump = 1 << 4,
    BtnDash = 1 << 5,
    BtnAttack = 1 << 6,
};

enum Ability : uint8_t {
    AbilityDash = 1 << 0,        // Keramat of the Wind
    AbilityDoubleJump = 1 << 1,  // Keramat of the Crane
    AbilityWallJump = 1 << 2,    // Keramat of the Tiger
};

struct Aabb {
    fx x = 0, y = 0, w = 0, h = 0;
    bool overlaps(const Aabb& o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
};

}  // namespace ledang
