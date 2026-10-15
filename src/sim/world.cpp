#include "sim/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <span>

namespace ledang {

namespace {

Aabb tile_box(int tx, int ty) { return Aabb{tx * kTileFx, ty * kTileFx, kTileFx, kTileFx}; }

fx centre_x(const Aabb& b) { return b.x + b.w / 2; }
fx centre_y(const Aabb& b) { return b.y + b.h / 2; }

int16_t to_px16(fx v) { return static_cast<int16_t>(to_px(v)); }

}  // namespace

World::World(const Map& map) : map_(&map) {
    bool have_spawn = false;
    for (const Marker& m : map.markers()) {
        const fx left = m.tx * kTileFx;
        const fx bottom = (m.ty + 1) * kTileFx;
        switch (m.kind) {
            case MarkerKind::PlayerSpawn:
                if (!have_spawn) spawn_ = m;  // the first P is the single-player start
                have_spawn = true;
                break;
            case MarkerKind::Lantern: lanterns_.push_back({m.tx, m.ty}); break;
            case MarkerKind::Penanggal:
                enemies_.push_back(make_enemy(EnemyKind::Penanggal, left + px(2), m.ty * kTileFx + px(2)));
                break;
            case MarkerKind::Pocong:
                enemies_.push_back(make_enemy(EnemyKind::Pocong, left + px(3), bottom - enemy_tuning::kPocongH));
                break;
            case MarkerKind::ShrineDash: shrines_.push_back({m.tx, m.ty, AbilityDash}); break;
            case MarkerKind::ShrineDoubleJump: shrines_.push_back({m.tx, m.ty, AbilityDoubleJump}); break;
            case MarkerKind::ShrineWallJump: shrines_.push_back({m.tx, m.ty, AbilityWallJump}); break;
            case MarkerKind::Goal: goal_ = m; break;
            case MarkerKind::Guardian: break;  // reserved
        }
    }
    respawn();
    player_.hurt_ticks = 0;
}

fx World::spawn_x() const {
    if (checkpoint_ != kNoCheckpoint && checkpoint_ < lanterns_.size()) return lanterns_[checkpoint_].tx * kTileFx + px(3);
    return spawn_.tx * kTileFx + px(3);
}

fx World::spawn_y() const {
    const int ty = checkpoint_ != kNoCheckpoint && checkpoint_ < lanterns_.size() ? lanterns_[checkpoint_].ty : spawn_.ty;
    return (ty + 1) * kTileFx - tuning::kHeight;
}

void World::respawn() {
    PlayerState p;
    p.x = spawn_x();
    p.y = spawn_y();
    p.abilities = abilities_;
    p.prev_buttons = player_.prev_buttons;
    p.hurt_ticks = kHurtInvulnTicks;
    player_ = p;
    respawn_ticks_ = 0;
    safe_x_ = p.x;
    safe_y_ = p.y;
    touching_lantern_ = checkpoint_;  // waking up at a lantern is not a fresh visit
}

void World::emit(EventType t, uint8_t a, fx x, fx y) { events_.push_back(Event{t, a, to_px16(x), to_px16(y)}); }

std::vector<Event> World::take_events() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

SaveData World::save_data() const {
    SaveData s;
    s.abilities = abilities_;
    s.lanterns_lit = lanterns_lit_;
    s.checkpoint = checkpoint_;
    s.deaths = deaths_;
    s.play_ticks = play_ticks_;
    s.victory = victory_;
    return s;
}

void World::restore(const SaveData& save) {
    abilities_ = save.abilities & (AbilityDash | AbilityDoubleJump | AbilityWallJump);
    lanterns_lit_ = save.lanterns_lit;
    checkpoint_ = save.checkpoint < lanterns_.size() ? save.checkpoint : kNoCheckpoint;
    deaths_ = save.deaths;
    play_ticks_ = save.play_ticks;
    victory_ = save.victory;
    respawn();
    player_.hurt_ticks = 0;
}

void World::debug_unlock(uint8_t abilities, int lantern) {
    abilities_ |= abilities;
    if (lantern >= 0 && lantern < static_cast<int>(lanterns_.size())) {
        lanterns_lit_ |= static_cast<uint16_t>(1u << lantern);
        checkpoint_ = static_cast<uint8_t>(lantern);
    }
    respawn();
    player_.hurt_ticks = 0;
}

void World::hurt(fx from_cx) {
    PlayerState& p = player_;
    if (p.has(FlagDead) || p.hurt_ticks > 0) return;
    const Aabb b = player_box(p);
    const int dir = centre_x(b) == from_cx ? -p.facing() : (centre_x(b) < from_cx ? -1 : 1);
    p.hurt_ticks = kHurtInvulnTicks;
    p.vx = dir * 640;
    p.vy = -900;
    p.jump_held = 0;
    p.control_lock = 14;
    p.dash_ticks = 0;
    p.attack_ticks = 0;
    if (p.hp > 0) --p.hp;
    if (p.hp == 0) {
        p.flags |= FlagDead;
        p.vx = p.vy = 0;
        respawn_ticks_ = kPlayerRespawnTicks;
        ++deaths_;
        emit(EventType::PlayerDied, 0, b.x, b.y);
    } else {
        emit(EventType::PlayerHurt, p.hp, b.x, b.y);
    }
}

void World::strike() {
    PlayerState& p = player_;
    if (p.has(FlagDead) || !attack_active(p)) return;
    const Aabb blade = attack_box(p);
    const fx from = centre_x(player_box(p));
    bool landed = false;
    for (size_t i = 0; i < enemies_.size(); ++i) {
        EnemyState& e = enemies_[i];
        if (!e.alive() || e.hurt_ticks > 0 || !blade.overlaps(enemy_box(e))) continue;
        const Aabb eb = enemy_box(e);
        if (p.attack_dir == AttackDir::Down) {
            // A downward strike knocks the spirit down instead of sideways.
            hit_enemy(e, from);
            e.vx /= 3;
            if (e.alive()) e.vy = 500;
        } else if (hit_enemy(e, from)) {
            // killed: handled below
        }
        if (e.alive()) {
            emit(EventType::EnemyHit, static_cast<uint8_t>(i), eb.x, eb.y);
        } else {
            emit(EventType::EnemyKilled, static_cast<uint8_t>(i), eb.x, eb.y);
        }
        landed = true;
    }
    // Bamboo stakes can be pogoed too: the classic way across a spike pit.
    if (!landed && p.attack_dir == AttackDir::Down && touches_tile(blade, *map_, Tile::Spikes)) landed = true;
    if (landed && !p.swing_landed) {
        attack_landed(p);
        if (p.attack_dir == AttackDir::Down) emit(EventType::Pogo, 0, p.x + tuning::kWidth / 2, p.y + tuning::kHeight);
    }
}

void World::interact() {
    PlayerState& p = player_;
    if (p.has(FlagDead)) return;
    const Aabb box = player_box(p);

    if (touches_tile(box, *map_, Tile::Spikes)) {
        // Like Hollow Knight: the stakes hurt (unless you're still invulnerable), and if you survive you are put
        // back on the last safe ground instead of being left to fall onto them again.
        hurt(centre_x(box));
        if (!p.has(FlagDead)) {
            p.x = safe_x_;
            p.y = safe_y_;
            p.vx = p.vy = 0;
            p.dash_ticks = 0;
        }
        return;
    }
    if (p.has(FlagOnGround)) {
        // Remember solid ground well away from any stakes as the place to return to.
        const Aabb wide{box.x - kTileFx, box.y, box.w + 2 * kTileFx, box.h + px(2)};
        if (!touches_tile(wide, *map_, Tile::Spikes)) {
            safe_x_ = p.x;
            safe_y_ = p.y;
        }
    }

    for (const Shrine& sh : shrines_) {
        if ((abilities_ & sh.ability) == 0 && box.overlaps(tile_box(sh.tx, sh.ty))) {
            abilities_ |= sh.ability;
            p.abilities = abilities_;
            emit(EventType::AbilityGained, sh.ability, sh.tx * kTileFx, sh.ty * kTileFx);
        }
    }

    uint8_t touching = kNoCheckpoint;
    for (size_t i = 0; i < lanterns_.size(); ++i) {
        const Lantern& l = lanterns_[i];
        if (!box.overlaps(tile_box(l.tx, l.ty))) continue;
        touching = static_cast<uint8_t>(i);
        if (touching_lantern_ == touching) break;  // still standing at it
        if (!(lanterns_lit_ & (1u << i))) {
            lanterns_lit_ |= static_cast<uint16_t>(1u << i);
            emit(EventType::LanternLit, touching, l.tx * kTileFx, l.ty * kTileFx);
        }
        // Resting at a lantern restores health and makes it your respawn point.
        checkpoint_ = touching;
        p.hp = tuning::kMaxHp;
        emit(EventType::Rested, touching, l.tx * kTileFx, l.ty * kTileFx);
        break;
    }
    touching_lantern_ = touching;

    if (!victory_ && box.overlaps(tile_box(goal_.tx, goal_.ty))) {
        victory_ = true;
        emit(EventType::Victory, 0, box.x, box.y);
    }
}

void World::step_enemies() {
    const Aabb pb = player_box(player_);
    const bool alive = !player_.has(FlagDead);
    const Target target{centre_x(pb), centre_y(pb), alive};
    const std::span<const Target> targets(&target, 1);

    for (size_t i = 0; i < enemies_.size(); ++i) {
        EnemyState& e = enemies_[i];
        if (!e.alive()) {
            if (e.timer > 0) --e.timer;
            if (e.timer == 0) {
                // Return only when the player isn't watching the spot.
                const bool watched = alive && std::abs(target.cx - e.home_x) < 13 * kTileFx &&
                                     std::abs(target.cy - e.home_y) < 8 * kTileFx;
                if (watched) {
                    e.timer = 30;
                } else {
                    const Aabb b = enemy_box(e);
                    e = make_enemy(e.kind, e.home_x - b.w / 2, e.home_y - b.h / 2);
                }
            }
            continue;
        }
        step_enemy(e, targets, *map_, tick_);

        const Aabb eb = enemy_box(e);
        if (!alive || player_.hurt_ticks > 0 || !eb.overlaps(player_box(player_))) continue;
        hurt(centre_x(eb));
        if (e.kind == EnemyKind::Penanggal) {
            e.timer = enemy_tuning::kRecoilTicks;
            e.vx = centre_x(eb) < centre_x(player_box(player_)) ? -400 : 400;
            e.vy = -300;
        }
    }
}

void World::update(uint8_t buttons) {
    ++tick_;
    if (!victory_) ++play_ticks_;

    if (player_.has(FlagDead) && respawn_ticks_ > 0 && --respawn_ticks_ == 0) {
        respawn();
        emit(EventType::PlayerRespawned, 0, player_.x, player_.y);
    }

    player_.abilities = abilities_;
    step_player(player_, buttons, *map_);
    strike();
    interact();
    if (spirits_active_) step_enemies();
}

}  // namespace ledang
