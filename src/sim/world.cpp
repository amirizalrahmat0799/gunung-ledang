#include "sim/world.hpp"

#include <algorithm>
#include <cstdlib>

namespace ledang {

namespace {

Aabb tile_box(int tx, int ty) { return Aabb{tx * kTileFx, ty * kTileFx, kTileFx, kTileFx}; }

fx centre_x(const Aabb& b) { return b.x + b.w / 2; }
fx centre_y(const Aabb& b) { return b.y + b.h / 2; }

int16_t to_px16(fx v) { return static_cast<int16_t>(to_px(v)); }

}  // namespace

World::World(const Map& map) : map_(&map) {
    for (const Marker& m : map.markers()) {
        const fx left = m.tx * kTileFx;
        const fx bottom = (m.ty + 1) * kTileFx;
        switch (m.kind) {
            case MarkerKind::PlayerSpawn: spawns_.push_back(m); break;
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
    record_history();
}

fx World::spawn_x(int slot) const {
    const PlayerSlot& s = players_[static_cast<size_t>(slot)];
    if (s.checkpoint != kNoCheckpoint) {
        return lanterns_[s.checkpoint].tx * kTileFx + px(1) + px(3) * slot;
    }
    if (spawns_.empty()) return px(32);
    const Marker& m = spawns_[static_cast<size_t>(slot) % spawns_.size()];
    return m.tx * kTileFx + px(3);
}

fx World::spawn_y(int slot) const {
    const PlayerSlot& s = players_[static_cast<size_t>(slot)];
    const int ty = s.checkpoint != kNoCheckpoint ? lanterns_[s.checkpoint].ty
                   : spawns_.empty()             ? 1
                                                 : spawns_[static_cast<size_t>(slot) % spawns_.size()].ty;
    return (ty + 1) * kTileFx - tuning::kHeight;
}

bool World::add_player(int slot) {
    if (slot < 0 || slot >= kMaxPlayers || players_[static_cast<size_t>(slot)].active) return false;
    PlayerSlot& s = players_[static_cast<size_t>(slot)];
    s = PlayerSlot{};
    s.active = true;
    s.checkpoint = last_lit_;  // late joiners start at the newest lantern the team has lit
    respawn(slot);
    emit(EventType::PlayerJoined, static_cast<uint8_t>(slot), 0, s.state.x, s.state.y);
    return true;
}

void World::remove_player(int slot) {
    if (slot < 0 || slot >= kMaxPlayers || !players_[static_cast<size_t>(slot)].active) return;
    players_[static_cast<size_t>(slot)] = PlayerSlot{};
    emit(EventType::PlayerLeft, static_cast<uint8_t>(slot), 0, 0, 0);
}

void World::respawn(int slot) {
    PlayerSlot& s = players_[static_cast<size_t>(slot)];
    PlayerState p;
    p.x = spawn_x(slot);
    p.y = spawn_y(slot);
    p.abilities = abilities_;
    p.prev_buttons = s.state.prev_buttons;
    p.hurt_ticks = kHurtInvulnTicks;
    s.state = p;
    s.respawn_ticks = 0;
}

void World::debug_unlock(uint8_t abilities, int lantern) {
    abilities_ |= abilities;
    for (PlayerSlot& s : players_) s.state.abilities = abilities_;
    if (lantern >= 0 && lantern < static_cast<int>(lanterns_.size())) {
        lanterns_lit_ |= static_cast<uint16_t>(1u << lantern);
        last_lit_ = static_cast<uint8_t>(lantern);
    }
}

void World::emit(EventType t, uint8_t a, uint8_t b, fx x, fx y) {
    events_.push_back(Event{t, a, b, to_px16(x), to_px16(y)});
}

std::vector<Event> World::take_events() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

void World::hurt(int slot, fx from_cx) {
    PlayerState& p = players_[static_cast<size_t>(slot)].state;
    if (p.has(FlagDead) || p.hurt_ticks > 0) return;
    const Aabb b = player_box(p);
    const int dir = centre_x(b) == from_cx ? -p.facing() : (centre_x(b) < from_cx ? -1 : 1);
    p.hurt_ticks = kHurtInvulnTicks;
    p.vx = dir * 640;
    p.vy = -900;
    p.control_lock = 14;
    p.dash_ticks = 0;
    p.attack_ticks = 0;
    if (p.hp > 0) --p.hp;
    if (p.hp == 0) {
        p.flags |= FlagDead;
        p.vx = p.vy = 0;
        players_[static_cast<size_t>(slot)].respawn_ticks = kPlayerRespawnTicks;
        emit(EventType::PlayerDied, static_cast<uint8_t>(slot), 0, b.x, b.y);
    } else {
        emit(EventType::PlayerHurt, static_cast<uint8_t>(slot), p.hp, b.x, b.y);
    }
}

void World::interact(int slot) {
    PlayerSlot& s = players_[static_cast<size_t>(slot)];
    PlayerState& p = s.state;
    if (p.has(FlagDead)) return;
    const Aabb box = player_box(p);

    if (touches_tile(box, *map_, Tile::Spikes)) hurt(slot, centre_x(box));
    if (p.has(FlagDead)) return;

    for (const Shrine& sh : shrines_) {
        if ((abilities_ & sh.ability) == 0 && box.overlaps(tile_box(sh.tx, sh.ty))) {
            abilities_ |= sh.ability;
            for (PlayerSlot& other : players_) other.state.abilities = abilities_;
            emit(EventType::AbilityGained, sh.ability, static_cast<uint8_t>(slot), sh.tx * kTileFx, sh.ty * kTileFx);
        }
    }
    for (size_t i = 0; i < lanterns_.size(); ++i) {
        const Lantern& l = lanterns_[i];
        if (!box.overlaps(tile_box(l.tx, l.ty))) continue;
        if (!(lanterns_lit_ & (1u << i))) {
            lanterns_lit_ |= static_cast<uint16_t>(1u << i);
            last_lit_ = static_cast<uint8_t>(i);
            emit(EventType::LanternLit, static_cast<uint8_t>(i), static_cast<uint8_t>(slot), l.tx * kTileFx,
                 l.ty * kTileFx);
        }
        // Resting at a lantern restores health and makes it your respawn point.
        s.checkpoint = static_cast<uint8_t>(i);
        p.hp = tuning::kMaxHp;
    }
    if (!victory_ && box.overlaps(tile_box(goal_.tx, goal_.ty))) {
        victory_ = true;
        emit(EventType::Victory, static_cast<uint8_t>(slot), 0, box.x, box.y);
    }
}

void World::apply_command(int slot, uint8_t buttons, uint32_t view_tick_q8) {
    if (slot < 0 || slot >= kMaxPlayers || !players_[static_cast<size_t>(slot)].active) return;
    PlayerState& p = players_[static_cast<size_t>(slot)].state;
    p.abilities = abilities_;
    step_player(p, buttons, *map_);

    if (attack_connects_this_tick(p) && !p.has(FlagDead)) {
        // Lag compensation: judge the swing against the world the player saw.
        const uint32_t newest = tick_ << 8;
        const uint32_t oldest = tick_ > static_cast<uint32_t>(kMaxRewindTicks)
                                    ? (tick_ - static_cast<uint32_t>(kMaxRewindTicks)) << 8
                                    : 0;
        const uint32_t view = std::clamp(view_tick_q8, oldest, newest);
        const Aabb blade = attack_box(p);
        const fx from = centre_x(player_box(p));
        for (size_t i = 0; i < enemies_.size(); ++i) {
            EnemyState& e = enemies_[i];
            if (!e.alive() || !enemy_alive_in_past(i, view >> 8)) continue;
            if (!blade.overlaps(enemy_box_in_past(i, view))) continue;
            if (view != newest) ++rewound_hits_;
            const Aabb eb = enemy_box(e);
            if (hit_enemy(e, from)) {
                emit(EventType::EnemyKilled, static_cast<uint8_t>(i), static_cast<uint8_t>(slot), eb.x, eb.y);
            } else {
                emit(EventType::EnemyHit, static_cast<uint8_t>(i), static_cast<uint8_t>(slot), eb.x, eb.y);
            }
        }
    }
    interact(slot);
}

void World::step() {
    ++tick_;

    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        PlayerSlot& s = players_[static_cast<size_t>(slot)];
        if (!s.active || !s.state.has(FlagDead)) continue;
        if (s.respawn_ticks > 0 && --s.respawn_ticks == 0) {
            respawn(slot);
            emit(EventType::PlayerRespawned, static_cast<uint8_t>(slot), 0, s.state.x, s.state.y);
        }
    }

    std::array<Target, kMaxPlayers> targets{};
    for (size_t i = 0; i < players_.size(); ++i) {
        const PlayerSlot& s = players_[i];
        const Aabb b = player_box(s.state);
        targets[i] = Target{centre_x(b), centre_y(b), s.active && !s.state.has(FlagDead)};
    }

    for (size_t i = 0; i < enemies_.size() && spirits_active_; ++i) {
        EnemyState& e = enemies_[i];
        if (!e.alive()) {
            if (e.timer > 0) --e.timer;
            if (e.timer == 0) {
                // Return only when nobody is watching the spot.
                bool watched = false;
                for (const Target& t : targets) {
                    if (t.valid && std::abs(t.cx - e.home_x) < 7 * kTileFx && std::abs(t.cy - e.home_y) < 5 * kTileFx) {
                        watched = true;
                    }
                }
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
        for (int slot = 0; slot < kMaxPlayers; ++slot) {
            if (!targets[static_cast<size_t>(slot)].valid) continue;
            PlayerState& p = players_[static_cast<size_t>(slot)].state;
            if (p.hurt_ticks > 0 || !eb.overlaps(player_box(p))) continue;
            hurt(slot, centre_x(eb));
            if (e.kind == EnemyKind::Penanggal) {
                e.timer = enemy_tuning::kRecoilTicks;
                e.vx = centre_x(eb) < centre_x(player_box(p)) ? -400 : 400;
                e.vy = -300;
            }
        }
    }
    record_history();
}

void World::record_history() {
    HistoryEntry& h = history_[tick_ % kHistoryTicks];
    h.tick = tick_;
    h.enemies = enemies_;
}

bool World::enemy_alive_in_past(size_t enemy, uint32_t tick) const {
    const HistoryEntry& h = history_[tick % kHistoryTicks];
    if (h.tick != tick || enemy >= h.enemies.size()) return enemies_[enemy].alive();
    return h.enemies[enemy].alive();
}

Aabb World::enemy_box_in_past(size_t enemy, uint32_t tick_q8) const {
    const uint32_t t0 = std::min(tick_q8 >> 8, tick_);
    const fx frac = t0 == tick_ ? 0 : static_cast<fx>(tick_q8 & 0xFF);
    const HistoryEntry& h0 = history_[t0 % kHistoryTicks];
    if (h0.tick != t0 || enemy >= h0.enemies.size()) return enemy_box(enemies_[enemy]);
    const EnemyState& a = h0.enemies[enemy];
    const HistoryEntry& h1 = history_[(t0 + 1) % kHistoryTicks];
    if (frac == 0 || h1.tick != t0 + 1 || enemy >= h1.enemies.size()) return enemy_box(a);
    const EnemyState& b = h1.enemies[enemy];
    return enemy_box_at(a.kind, a.x + (b.x - a.x) * frac / 256, a.y + (b.y - a.y) * frac / 256);
}

}  // namespace ledang
