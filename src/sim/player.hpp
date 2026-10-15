#pragma once
// Player movement. step_player is a pure function of (state, buttons, map) on integers, so the same inputs always
// give exactly the same result: replays, tests and any future co-op mode can rely on that.

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
constexpr uint8_t kAttackTicks = 16;
constexpr uint8_t kAttackActiveFirst = 13;  // the blade can hit while attack_ticks is in [kAttackActiveLast, kAttackActiveFirst]
constexpr uint8_t kAttackActiveLast = 10;
constexpr fx kPogoVel = -1240;       // bounce off whatever a downward strike hits
constexpr fx kRecoilVx = 340;        // pushed back a little when a sideways strike lands
constexpr uint8_t kMaxHp = 5;
}  // namespace tuning

enum PlayerFlag : uint8_t {
    FlagOnGround = 1 << 0,
    FlagFacingLeft = 1 << 1,
    FlagWallLeft = 1 << 2,
    FlagWallRight = 1 << 3,
    FlagDead = 1 << 4,
};

enum class AttackDir : uint8_t { Side = 0, Up = 1, Down = 2 };

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
    AttackDir attack_dir = AttackDir::Side;
    uint8_t swing_landed = 0;  // the current swing already hit something (one pogo / recoil per swing)
    uint8_t jump_held = 0;     // rising from a jump: releasing the button cuts it short (not after a bounce)

    bool operator==(const PlayerState&) const = default;
    bool has(PlayerFlag f) const { return (flags & f) != 0; }
    int facing() const { return has(FlagFacingLeft) ? -1 : 1; }
};

/// Advances one player by one tick.
void step_player(PlayerState& p, uint8_t buttons, const Map& map);

Aabb player_box(const PlayerState& p);
/// The blade's hitbox while the swing is active: in front, above, or (in the air) below the player.
Aabb attack_box(const PlayerState& p);
/// True on the ticks of a swing when the blade can hit.
bool attack_active(const PlayerState& p);
/// Called by the world when the blade lands: downward strikes bounce (pogo), sideways ones push back.
void attack_landed(PlayerState& p);

/// Moves a body through the map, resolving collisions axis by axis. Returns flags (ground/wall contact).
/// `drop_through` ignores one-way platforms.
uint8_t move_body(fx& x, fx& y, fx& vx, fx& vy, fx w, fx h, const Map& map, bool drop_through);

bool touches_tile(const Aabb& box, const Map& map, Tile tile);

}  // namespace ledang
