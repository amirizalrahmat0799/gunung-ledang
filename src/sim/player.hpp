#pragma once
// Player movement: the one piece of simulation both the client (prediction) and server run.
// step_player is a pure function of (state, buttons, map), so replaying the same inputs always gives the
// same result, which is what makes client-side prediction and server reconciliation work.

#include "sim/map.hpp"
#include "sim/types.hpp"

namespace ledang {

namespace tuning {
// Units: 1/256 px per tick, at 60 ticks per second.
constexpr fx kWidth = px(10);
constexpr fx kHeight = px(14);
constexpr fx kRunMax = 576;          // 2.25 px/tick = 135 px/s
constexpr fx kGroundAccel = 90;
constexpr fx kAirAccel = 56;
constexpr fx kFriction = 115;
constexpr fx kGravity = 72;          // 0.28 px/tick^2
constexpr fx kMaxFall = 1408;        // 5.5 px/tick
constexpr fx kJumpVel = -1434;       // about 3.5 tiles high
constexpr fx kDoubleJumpVel = -1280; // about 2.8 tiles more
constexpr fx kJumpCutVel = -512;     // releasing jump early cuts the rise
constexpr fx kWallSlideMax = 384;
constexpr fx kWallJumpVx = 819;
constexpr fx kWallJumpVy = -1331;
constexpr fx kDashSpeed = 1536;      // 6 px/tick
constexpr uint8_t kDashTicks = 10;
constexpr uint8_t kDashCooldown = 30;
constexpr uint8_t kCoyoteTicks = 6;
constexpr uint8_t kJumpBufferTicks = 6;
constexpr uint8_t kWallJumpLock = 8;
constexpr uint8_t kAttackTicks = 18;
constexpr uint8_t kAttackHitTick = 15;  // attack_ticks value on the frame the blade connects
constexpr uint8_t kMaxHp = 5;
}  // namespace tuning

enum PlayerFlag : uint8_t {
    FlagOnGround = 1 << 0,
    FlagFacingLeft = 1 << 1,
    FlagWallLeft = 1 << 2,
    FlagWallRight = 1 << 3,
    FlagDead = 1 << 4,
};

struct PlayerState {
    fx x = 0, y = 0;      // top-left of the 10x14 px body
    fx vx = 0, vy = 0;
    uint8_t flags = 0;
    uint8_t air_jumps = 0;
    uint8_t air_dashed = 0;
    uint8_t coyote = 0;
    uint8_t jump_buffer = 0;
    uint8_t dash_ticks = 0;
    uint8_t dash_cooldown = 0;
    uint8_t attack_ticks = 0;
    uint8_t control_lock = 0;
    uint8_t prev_buttons = 0;
    uint8_t hp = tuning::kMaxHp;
    uint8_t hurt_ticks = 0;   // invulnerability after a hit
    uint8_t abilities = 0;

    bool operator==(const PlayerState&) const = default;
    bool has(PlayerFlag f) const { return (flags & f) != 0; }
    int facing() const { return has(FlagFacingLeft) ? -1 : 1; }
};

/// Advances one player by one tick.
void step_player(PlayerState& p, uint8_t buttons, const Map& map);

Aabb player_box(const PlayerState& p);
/// The blade's hitbox on the frame the attack connects (in front of the player).
Aabb attack_box(const PlayerState& p);
bool attack_connects_this_tick(const PlayerState& p);

/// Moves a body through the map, resolving collisions axis by axis. Returns flags (ground/wall contact).
/// `drop_through` ignores one-way platforms.
uint8_t move_body(fx& x, fx& y, fx& vx, fx& vy, fx w, fx h, const Map& map, bool drop_through);

bool touches_tile(const Aabb& box, const Map& map, Tile tile);

}  // namespace ledang
