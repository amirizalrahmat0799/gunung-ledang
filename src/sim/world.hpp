#pragma once
// The game world: the player, the spirits, and the rules that tie them together (damage, shrines, lanterns,
// the summit). update() advances everything by one 60 Hz tick from one set of buttons. Everything is integer
// math, so the same inputs always produce the same world.

#include <cstdint>
#include <vector>

#include "sim/enemy.hpp"
#include "sim/map.hpp"
#include "sim/player.hpp"
#include "sim/save.hpp"

namespace ledang {

enum class EventType : uint8_t {
    AbilityGained = 0,  // a = ability bit
    LanternLit,         // a = lantern index (first time)
    Rested,             // a = lantern index (touched a lantern: healed, checkpoint set)
    PlayerHurt,         // a = hp left
    PlayerDied,
    PlayerRespawned,
    EnemyHit,           // a = enemy index
    EnemyKilled,        // a = enemy index
    Pogo,               // a downward strike bounced
    Victory,
};

struct Event {
    EventType type;
    uint8_t a = 0;
    int16_t x = 0, y = 0;  // world pixels, for effects
    bool operator==(const Event&) const = default;
};

struct Lantern {
    int tx, ty;
};

constexpr uint16_t kPlayerRespawnTicks = 150;
constexpr uint8_t kHurtInvulnTicks = 60;

class World {
public:
    explicit World(const Map& map);

    const Map& map() const { return *map_; }
    uint32_t tick() const { return tick_; }

    /// Advances the world by one tick with the player holding `buttons`.
    void update(uint8_t buttons);

    std::vector<Event> take_events();

    const PlayerState& player() const { return player_; }
    uint8_t checkpoint() const { return checkpoint_; }
    uint16_t respawn_ticks() const { return respawn_ticks_; }
    const std::vector<EnemyState>& enemies() const { return enemies_; }
    const std::vector<Lantern>& lanterns() const { return lanterns_; }
    uint8_t abilities() const { return abilities_; }
    uint16_t lanterns_lit() const { return lanterns_lit_; }
    bool victory() const { return victory_; }
    uint32_t deaths() const { return deaths_; }
    /// Ticks played, including earlier sessions restored from a save.
    uint32_t play_ticks() const { return play_ticks_; }

    /// Progress to write to the save file.
    SaveData save_data() const;
    /// Continues from a save: abilities, lanterns and counters restored, player at their last lantern.
    void restore(const SaveData& save);

    /// Development shortcut (--warp): grants every listed ability and starts the player at a lantern.
    void debug_unlock(uint8_t abilities, int lantern);
    /// Tests: freezes every spirit in place.
    void set_spirits_active(bool active) { spirits_active_ = active; }

private:
    fx spawn_x() const;
    fx spawn_y() const;
    void respawn();
    void hurt(fx from_cx);
    void strike();
    void interact();
    void step_enemies();
    void emit(EventType t, uint8_t a, fx x, fx y);

    const Map* map_;
    uint32_t tick_ = 0;
    PlayerState player_;
    uint8_t checkpoint_ = kNoCheckpoint;
    uint16_t respawn_ticks_ = 0;
    uint8_t touching_lantern_ = kNoCheckpoint;  // so resting fires once per visit, not every tick
    fx safe_x_ = 0, safe_y_ = 0;  // last solid ground away from stakes, where a stake hit puts you back
    std::vector<EnemyState> enemies_;
    std::vector<Lantern> lanterns_;
    struct Shrine {
        int tx, ty;
        uint8_t ability;
    };
    std::vector<Shrine> shrines_;
    Marker spawn_{MarkerKind::PlayerSpawn, 2, 1};
    Marker goal_{MarkerKind::Goal, -100, -100};
    uint8_t abilities_ = 0;
    uint16_t lanterns_lit_ = 0;
    bool victory_ = false;
    bool spirits_active_ = true;
    uint32_t deaths_ = 0;
    uint32_t play_ticks_ = 0;
    std::vector<Event> events_;
};

}  // namespace ledang
