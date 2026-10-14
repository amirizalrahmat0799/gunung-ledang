#include "sim/enemy.hpp"

#include <algorithm>
#include <cstdlib>

namespace ledang {

using namespace enemy_tuning;

namespace {

fx approach(fx value, fx target, fx step) {
    if (value < target) return std::min(value + step, target);
    if (value > target) return std::max(value - step, target);
    return value;
}

fx clamp_abs(fx v, fx limit) { return std::clamp(v, -limit, limit); }

/// Nearest valid target within `range` (Chebyshev distance), ties broken by lower index. Returns -1 if none.
int nearest(std::span<const Target> targets, fx cx, fx cy, fx range_x, fx range_y) {
    int best = -1;
    fx best_d = 0;
    for (size_t i = 0; i < targets.size(); ++i) {
        const Target& t = targets[i];
        if (!t.valid) continue;
        const fx dx = std::abs(t.cx - cx);
        const fx dy = std::abs(t.cy - cy);
        if (dx > range_x || dy > range_y) continue;
        const fx d = dx + dy;
        if (best < 0 || d < best_d) {
            best = static_cast<int>(i);
            best_d = d;
        }
    }
    return best;
}

void set_facing(EnemyState& e, fx dx) {
    if (dx < 0) e.flags |= EnemyFacingLeft;
    if (dx > 0) e.flags &= static_cast<uint8_t>(~EnemyFacingLeft);
}

/// Triangle wave in [-amp, amp] with the given period, used for the penanggal's idle bob.
fx triangle(uint32_t t, uint32_t period, fx amp) {
    const uint32_t phase = t % period;
    const uint32_t half = period / 2;
    const fx up = static_cast<fx>(phase < half ? phase : period - phase);  // 0..half
    return static_cast<fx>((up * 2 - static_cast<fx>(half)) * amp / static_cast<fx>(half));
}

void step_penanggal(EnemyState& e, std::span<const Target> targets, uint32_t tick) {
    const Aabb box = enemy_box(e);
    const fx cx = box.x + box.w / 2;
    const fx cy = box.y + box.h / 2;

    fx goal_x = e.home_x;
    fx goal_y = e.home_y + triangle(tick + static_cast<uint32_t>(e.home_x >> 8), 120, px(6));
    fx speed = kPenanggalSpeed / 2;

    const bool leashed = std::abs(cx - e.home_x) > kPenanggalLeash || std::abs(cy - e.home_y) > kPenanggalLeash;
    if (e.timer > 0) {
        --e.timer;  // recoiling after a bite: drift away
        speed = 0;
    } else if (!leashed) {
        const int t = nearest(targets, cx, cy, kPenanggalSight, kPenanggalSight);
        if (t >= 0) {
            goal_x = targets[static_cast<size_t>(t)].cx;
            goal_y = targets[static_cast<size_t>(t)].cy - px(4);
            speed = kPenanggalSpeed;
        }
    }

    if (speed > 0) {
        const fx dx = goal_x - cx;
        const fx dy = goal_y - cy;
        e.vx = approach(e.vx, clamp_abs(dx / 8, speed), kPenanggalAccel);
        e.vy = approach(e.vy, clamp_abs(dy / 8, speed), kPenanggalAccel);
        set_facing(e, dx);
    } else {
        e.vx = approach(e.vx, 0, kPenanggalAccel / 2);
        e.vy = approach(e.vy, 0, kPenanggalAccel / 2);
    }
    e.x += e.vx;
    e.y += e.vy;
}

void step_pocong(EnemyState& e, std::span<const Target> targets, const Map& map) {
    const Aabb box = enemy_box(e);
    const fx cx = box.x + box.w / 2;
    const fx cy = box.y + box.h / 2;
    const bool on_ground = (e.flags & EnemyOnGround) != 0;

    if (e.timer > 0) --e.timer;
    if (on_ground) {
        e.vx = approach(e.vx, 0, 60);
        if (e.timer == 0 && e.hurt_ticks == 0) {
            const int t = nearest(targets, cx, cy, kPocongSight, 4 * kTileFx);
            if (t >= 0) {
                const fx dx = targets[static_cast<size_t>(t)].cx - cx;
                set_facing(e, dx);
                e.vx = dx < 0 ? -kPocongHopVx : kPocongHopVx;
                e.vy = kPocongHopVy;
                e.timer = kPocongHopInterval;
            }
        }
    }
    e.vy = std::min(e.vy + tuning::kGravity, tuning::kMaxFall);
    const uint8_t contact = move_body(e.x, e.y, e.vx, e.vy, kPocongW, kPocongH, map, false);
    e.flags = static_cast<uint8_t>((e.flags & ~EnemyOnGround) | ((contact & FlagOnGround) ? EnemyOnGround : 0));
}

}  // namespace

Aabb enemy_box_at(EnemyKind kind, fx x, fx y) {
    if (kind == EnemyKind::Penanggal) return Aabb{x, y, kPenanggalSize, kPenanggalSize};
    return Aabb{x, y, kPocongW, kPocongH};
}

Aabb enemy_box(const EnemyState& e) { return enemy_box_at(e.kind, e.x, e.y); }

EnemyState make_enemy(EnemyKind kind, fx x, fx y) {
    EnemyState e;
    e.kind = kind;
    e.flags = EnemyAlive;
    e.x = x;
    e.y = y;
    const Aabb b = enemy_box(e);
    e.home_x = x + b.w / 2;
    e.home_y = y + b.h / 2;
    e.hp = kind == EnemyKind::Penanggal ? kPenanggalHp : kPocongHp;
    return e;
}

void step_enemy(EnemyState& e, std::span<const Target> targets, const Map& map, uint32_t tick) {
    if (!e.alive()) return;
    if (e.hurt_ticks > 0) --e.hurt_ticks;
    if (e.kind == EnemyKind::Penanggal) {
        step_penanggal(e, targets, tick);
    } else {
        step_pocong(e, targets, map);
    }
}

bool hit_enemy(EnemyState& e, fx from_cx) {
    if (!e.alive() || e.hurt_ticks > 0) return false;
    const Aabb b = enemy_box(e);
    const fx dir = (b.x + b.w / 2) < from_cx ? -1 : 1;
    e.hurt_ticks = kHurtTicks;
    e.vx = dir * 700;
    e.vy = e.kind == EnemyKind::Penanggal ? -200 : -500;
    if (e.kind == EnemyKind::Penanggal) e.timer = kRecoilTicks / 2;
    if (e.hp > 0) --e.hp;
    if (e.hp == 0) {
        e.flags &= static_cast<uint8_t>(~EnemyAlive);
        e.timer = kRespawnTicks;
        e.vx = e.vy = 0;
        return true;
    }
    return false;
}

}  // namespace ledang
