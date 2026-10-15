#pragma once
// The mountain's spirits.
//   Penanggal: a flying head trailing its entrails. Ignores walls and glides toward the nearest player.
//   Pocong:    a shrouded corpse that can only hop, and does so toward anyone who comes close.

#include <cstdint>
#include <span>

#include "sim/map.hpp"
#include "sim/player.hpp"
#include "sim/types.hpp"

namespace ledang {

enum class EnemyKind : uint8_t { Penanggal = 0, Pocong = 1 };

enum EnemyFlag : uint8_t {
    EnemyAlive = 1 << 0,
    EnemyFacingLeft = 1 << 1,
    EnemyOnGround = 1 << 2,
};

struct EnemyState {
    EnemyKind kind = EnemyKind::Penanggal;
    uint8_t flags = 0;
    fx x = 0, y = 0;
    fx vx = 0, vy = 0;
    fx home_x = 0, home_y = 0;
    uint8_t hp = 0;
    uint8_t hurt_ticks = 0;   // flash and stagger after being cut
    uint16_t timer = 0;       // hop cooldown / recoil / respawn countdown

    bool operator==(const EnemyState&) const = default;
    bool alive() const { return (flags & EnemyAlive) != 0; }
};

namespace enemy_tuning {
constexpr fx kPenanggalSize = px(12);
constexpr fx kPocongW = px(10);
constexpr fx kPocongH = px(16);
constexpr uint8_t kPenanggalHp = 2;
constexpr uint8_t kPocongHp = 3;
constexpr fx kPenanggalSpeed = 330;      // ~1.3 px/tick, slower than a running player
constexpr fx kPenanggalAccel = 14;
constexpr fx kPenanggalSight = 9 * kTileFx;
constexpr fx kPenanggalLeash = 14 * kTileFx;
constexpr fx kPocongSight = 8 * kTileFx;
constexpr fx kPocongHopVx = 330;
constexpr fx kPocongHopVy = -1150;
constexpr uint16_t kPocongHopInterval = 42;
constexpr uint16_t kRespawnTicks = 60 * 12;
constexpr uint16_t kRecoilTicks = 40;
constexpr uint8_t kHurtTicks = 14;
}  // namespace enemy_tuning

EnemyState make_enemy(EnemyKind kind, fx x, fx y);
Aabb enemy_box(const EnemyState& e);
Aabb enemy_box_at(EnemyKind kind, fx x, fx y);

/// A player as enemies see them (only living players can be targeted).
struct Target {
    fx cx, cy;  // centre
    bool valid;
};

/// Advances one enemy by one tick. Deterministic: depends only on its inputs.
void step_enemy(EnemyState& e, std::span<const Target> targets, const Map& map, uint32_t tick);

/// Applies a sword hit from a player at horizontal centre `from_cx`. Returns true if it killed the enemy.
bool hit_enemy(EnemyState& e, fx from_cx);

}  // namespace ledang
