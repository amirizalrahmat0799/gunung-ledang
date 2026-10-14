#include "sim/player.hpp"

#include <algorithm>

namespace ledang {

using namespace tuning;

namespace {

fx approach(fx value, fx target, fx step) {
    if (value < target) return std::min(value + step, target);
    if (value > target) return std::max(value - step, target);
    return value;
}

int tile_of(fx v) { return v >> kTileShift; }  // arithmetic shift floors negatives too

bool column_blocked(const Map& map, int tx, fx top, fx bottom) {
    for (int ty = tile_of(top); ty <= tile_of(bottom); ++ty) {
        if (map.solid(tx, ty)) return true;
    }
    return false;
}

}  // namespace

uint8_t move_body(fx& x, fx& y, fx& vx, fx& vy, fx w, fx h, const Map& map, bool drop_through) {
    uint8_t contact = 0;

    // Horizontal
    x += vx;
    if (vx > 0) {
        const int tx = tile_of(x + w - 1);
        if (column_blocked(map, tx, y, y + h - 1)) {
            x = tx * kTileFx - w;
            vx = 0;
        }
    } else if (vx < 0) {
        const int tx = tile_of(x);
        if (column_blocked(map, tx, y, y + h - 1)) {
            x = (tx + 1) * kTileFx;
            vx = 0;
        }
    }

    // Vertical
    const fx old_bottom = y + h;
    y += vy;
    if (vy > 0) {
        const int ty = tile_of(y + h - 1);
        bool landed = false;
        for (int tx = tile_of(x); tx <= tile_of(x + w - 1); ++tx) {
            const Tile t = map.at(tx, ty);
            if (t == Tile::Solid || (t == Tile::Platform && !drop_through && old_bottom <= ty * kTileFx)) {
                landed = true;
                break;
            }
        }
        if (landed) {
            y = ty * kTileFx - h;
            vy = 0;
        }
    } else if (vy < 0) {
        const int ty = tile_of(y);
        for (int tx = tile_of(x); tx <= tile_of(x + w - 1); ++tx) {
            if (map.solid(tx, ty)) {
                y = (ty + 1) * kTileFx;
                vy = 0;
                break;
            }
        }
    }

    // Contacts (probe one pixel beyond each side)
    const fx below = y + h;
    for (int tx = tile_of(x); tx <= tile_of(x + w - 1); ++tx) {
        const Tile t = map.at(tx, tile_of(below));
        const bool on_platform = t == Tile::Platform && !drop_through && (below & (kTileFx - 1)) == 0;
        if (vy >= 0 && (t == Tile::Solid || on_platform)) contact |= FlagOnGround;
    }
    if (column_blocked(map, tile_of(x - 1), y, y + h - 1)) contact |= FlagWallLeft;
    if (column_blocked(map, tile_of(x + w), y, y + h - 1)) contact |= FlagWallRight;
    return contact;
}

void step_player(PlayerState& p, uint8_t buttons, const Map& map) {
    const uint8_t pressed = static_cast<uint8_t>(buttons & ~p.prev_buttons);
    p.prev_buttons = buttons;
    if (p.has(FlagDead)) return;

    if (p.dash_cooldown > 0) --p.dash_cooldown;
    if (p.attack_ticks > 0) --p.attack_ticks;
    if (p.hurt_ticks > 0) --p.hurt_ticks;
    if (p.control_lock > 0) --p.control_lock;

    const bool on_ground = p.has(FlagOnGround);
    const int dir = ((buttons & BtnRight) ? 1 : 0) - ((buttons & BtnLeft) ? 1 : 0);
    const int wall = !on_ground ? (p.has(FlagWallLeft) ? -1 : (p.has(FlagWallRight) ? 1 : 0)) : 0;

    if (on_ground) {
        p.coyote = kCoyoteTicks;
        p.air_jumps = 1;
        p.air_dashed = 0;
    } else if (p.coyote > 0) {
        --p.coyote;
    }
    if (pressed & BtnJump) {
        p.jump_buffer = kJumpBufferTicks;
    } else if (p.jump_buffer > 0) {
        --p.jump_buffer;
    }
    if (pressed & BtnAttack && p.attack_ticks == 0) p.attack_ticks = kAttackTicks;

    const bool can_dash = (p.abilities & AbilityDash) && p.dash_cooldown == 0 && (on_ground || !p.air_dashed);
    if (p.dash_ticks == 0 && (pressed & BtnDash) && can_dash) {
        if (dir != 0) p.flags = static_cast<uint8_t>(dir < 0 ? (p.flags | FlagFacingLeft) : (p.flags & ~FlagFacingLeft));
        p.dash_ticks = kDashTicks;
        p.dash_cooldown = kDashCooldown;
        if (!on_ground) p.air_dashed = 1;
    }

    if (p.dash_ticks > 0) {
        --p.dash_ticks;
        p.vx = kDashSpeed * p.facing();
        p.vy = 0;
    } else {
        if (p.control_lock == 0) {
            const fx target = dir * kRunMax;
            if (dir == 0 && on_ground) {
                p.vx = approach(p.vx, 0, kFriction);
            } else {
                p.vx = approach(p.vx, target, on_ground ? kGroundAccel : kAirAccel);
            }
            if (dir != 0 && p.attack_ticks == 0) {
                p.flags = static_cast<uint8_t>(dir < 0 ? (p.flags | FlagFacingLeft) : (p.flags & ~FlagFacingLeft));
            }
        }

        if (p.jump_buffer > 0) {
            if (p.coyote > 0) {
                p.vy = kJumpVel;
                p.coyote = 0;
                p.jump_buffer = 0;
            } else if (wall != 0 && (p.abilities & AbilityWallJump)) {
                p.vy = kWallJumpVy;
                p.vx = -wall * kWallJumpVx;
                p.flags = static_cast<uint8_t>(wall > 0 ? (p.flags | FlagFacingLeft) : (p.flags & ~FlagFacingLeft));
                p.control_lock = kWallJumpLock;
                p.air_dashed = 0;
                p.jump_buffer = 0;
            } else if ((p.abilities & AbilityDoubleJump) && p.air_jumps > 0) {
                p.vy = kDoubleJumpVel;
                --p.air_jumps;
                p.jump_buffer = 0;
            }
        }
        if (!(buttons & BtnJump) && p.vy < kJumpCutVel) p.vy = kJumpCutVel;

        p.vy += kGravity;
        const bool sliding = wall != 0 && dir == wall && p.vy > 0 && (p.abilities & AbilityWallJump);
        p.vy = std::min(p.vy, sliding ? kWallSlideMax : kMaxFall);
    }

    const uint8_t contact = move_body(p.x, p.y, p.vx, p.vy, kWidth, kHeight, map, (buttons & BtnDown) != 0);
    p.flags = static_cast<uint8_t>((p.flags & ~(FlagOnGround | FlagWallLeft | FlagWallRight)) | contact);
}

Aabb player_box(const PlayerState& p) { return Aabb{p.x, p.y, kWidth, kHeight}; }

Aabb attack_box(const PlayerState& p) {
    constexpr fx reach = px(20);
    const fx x = p.facing() > 0 ? p.x + kWidth - px(2) : p.x - reach + px(2);
    return Aabb{x, p.y - px(2), reach, kHeight + px(4)};
}

bool attack_connects_this_tick(const PlayerState& p) { return p.attack_ticks == kAttackHitTick; }

bool touches_tile(const Aabb& box, const Map& map, Tile tile) {
    for (int ty = tile_of(box.y); ty <= tile_of(box.y + box.h - 1); ++ty) {
        for (int tx = tile_of(box.x); tx <= tile_of(box.x + box.w - 1); ++tx) {
            if (map.at(tx, ty) == tile) return true;
        }
    }
    return false;
}

}  // namespace ledang
