#pragma once
// The authoritative game world, run by the server.
//
// Players move only when their user commands are applied (apply_command), one command per simulated tick of
// that player, Valve style. Everything else (enemies, contact damage, respawns) advances in step().
// Attacks are lag compensated: a command carries the tick the player was looking at, and the blade is tested
// against where the enemies were at that moment, not where they are now on the server.

#include <array>
#include <cstdint>
#include <vector>

#include "sim/enemy.hpp"
#include "sim/map.hpp"
#include "sim/player.hpp"

namespace ledang {

enum class EventType : uint8_t {
    PlayerJoined = 0,
    PlayerLeft,
    AbilityGained,  // a = ability bit
    LanternLit,     // a = lantern index
    PlayerHurt,     // a = slot
    PlayerDied,     // a = slot
    PlayerRespawned,
    EnemyHit,       // a = enemy index
    EnemyKilled,    // a = enemy index, b = slot of the killer
    Victory,        // a = slot that reached the shrine
};

struct Event {
    EventType type;
    uint8_t a = 0;
    uint8_t b = 0;
    int16_t x = 0, y = 0;  // world pixels, for effects
    bool operator==(const Event&) const = default;
};

struct PlayerSlot {
    bool active = false;
    PlayerState state;
    uint8_t checkpoint = 0xFF;   // lantern index, 0xFF = start
    uint16_t respawn_ticks = 0;
    bool operator==(const PlayerSlot&) const = default;
};

struct Lantern {
    int tx, ty;
};

constexpr uint8_t kNoCheckpoint = 0xFF;
constexpr uint16_t kPlayerRespawnTicks = 150;
constexpr uint8_t kHurtInvulnTicks = 60;
constexpr int kHistoryTicks = 128;         // enemy position history kept for lag compensation
constexpr int kMaxRewindTicks = 30;        // 500 ms: a player can't hit things they saw longer ago than this

class World {
public:
    explicit World(const Map& map);

    const Map& map() const { return *map_; }
    uint32_t tick() const { return tick_; }

    /// Puts a player into the world at its spawn point. Returns false if the slot is taken.
    bool add_player(int slot);
    void remove_player(int slot);

    /// Runs one user command for one player. `view_tick_q8` is the (fractional, 1/256) tick the player's client
    /// was rendering when the command was made; it is clamped to the rewind window.
    void apply_command(int slot, uint8_t buttons, uint32_t view_tick_q8);

    /// Advances everything except player movement by one tick and records enemy history.
    void step();

    std::vector<Event> take_events();

    /// Development shortcut (ledang_server --warp, gunung_ledang --warp): grants abilities and lights a
    /// lantern so new players start there.
    void debug_unlock(uint8_t abilities, int lantern);
    /// Tests: freezes every spirit in place (no movement, no bites), so players can stand still undisturbed.
    void set_spirits_active(bool active) { spirits_active_ = active; }

    const std::array<PlayerSlot, kMaxPlayers>& players() const { return players_; }
    const std::vector<EnemyState>& enemies() const { return enemies_; }
    const std::vector<Lantern>& lanterns() const { return lanterns_; }
    uint8_t abilities() const { return abilities_; }
    uint16_t lanterns_lit() const { return lanterns_lit_; }
    bool victory() const { return victory_; }
    /// Number of enemy hits that were decided using a rewound position (for the debug overlay / tests).
    uint64_t rewound_hits() const { return rewound_hits_; }

    /// Enemy box as it was at a fractional past tick (Q8). Used by lag compensation and exposed for tests.
    Aabb enemy_box_in_past(size_t enemy, uint32_t tick_q8) const;
    bool enemy_alive_in_past(size_t enemy, uint32_t tick) const;

private:
    struct HistoryEntry {
        uint32_t tick = 0xFFFFFFFF;
        std::vector<EnemyState> enemies;
    };

    fx spawn_x(int slot) const;
    fx spawn_y(int slot) const;
    void respawn(int slot);
    void hurt(int slot, fx from_cx);
    void interact(int slot);
    void record_history();
    void emit(EventType t, uint8_t a, uint8_t b, fx x, fx y);

    const Map* map_;
    uint32_t tick_ = 0;
    std::array<PlayerSlot, kMaxPlayers> players_{};
    std::vector<EnemyState> enemies_;
    std::vector<Lantern> lanterns_;
    struct Shrine {
        int tx, ty;
        uint8_t ability;
    };
    std::vector<Shrine> shrines_;
    std::vector<Marker> spawns_;
    Marker goal_{MarkerKind::Goal, -100, -100};
    uint8_t abilities_ = 0;
    uint16_t lanterns_lit_ = 0;
    uint8_t last_lit_ = kNoCheckpoint;
    bool victory_ = false;
    bool spirits_active_ = true;
    std::vector<Event> events_;
    std::array<HistoryEntry, kHistoryTicks> history_{};
    uint64_t rewound_hits_ = 0;
};

}  // namespace ledang
