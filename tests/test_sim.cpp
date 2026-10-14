#include <random>
#include <string>

#include "sim/map.hpp"
#include "sim/player.hpp"
#include "sim/world.hpp"
#include "test.hpp"

using namespace ledang;

namespace {

// A flat 60x16 room with a floor at row 14.
Map flat_room() {
    std::string s;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 60; ++x) s += (y == 0 || y >= 14 || x == 0 || x == 59) ? '#' : '.';
        s += '\n';
    }
    return Map::parse(s);
}

PlayerState standing(const Map& map, int tx) {
    PlayerState p;
    p.x = tx * kTileFx;
    p.y = 14 * kTileFx - tuning::kHeight;
    step_player(p, 0, map);
    return p;
}

/// Highest the player's feet rise (in tiles) with the given number of jumps.
double max_jump_height(uint8_t abilities, bool double_jump) {
    const Map map = flat_room();
    PlayerState p = standing(map, 10);
    p.abilities = abilities;
    const fx floor_y = p.y;
    fx best = 0;
    for (int t = 0; t < 200; ++t) {
        uint8_t b = BtnJump;
        if (double_jump && t > 0 && p.vy >= 0 && p.air_jumps > 0) b = 0;  // release, then press again at the apex
        if (double_jump && t > 0 && p.vy >= 0 && (p.prev_buttons & BtnJump) == 0) b = BtnJump;
        step_player(p, b, map);
        best = std::max(best, floor_y - p.y);
    }
    return static_cast<double>(best) / kTileFx;
}

/// How far (in tiles) a running jump carries the player before landing back at floor level.
double running_jump_distance(bool dash) {
    const Map map = flat_room();
    PlayerState p = standing(map, 2);
    p.abilities = dash ? AbilityDash : 0;
    for (int t = 0; t < 60; ++t) step_player(p, BtnRight, map);  // reach full speed
    const fx takeoff = p.x;
    bool airborne = false;
    for (int t = 0; t < 300; ++t) {
        uint8_t b = BtnRight | BtnJump;
        if (dash && airborne && p.vy > -200 && t < 60) b |= BtnDash;  // dash near the apex
        step_player(p, b, map);
        if (!p.has(FlagOnGround)) airborne = true;
        if (airborne && p.has(FlagOnGround)) break;
    }
    return static_cast<double>(p.x - takeoff) / kTileFx;
}

}  // namespace

TEST(map_world_parses) {
    const Map& m = Map::world();
    CHECK_EQ(m.width(), 100);
    CHECK_EQ(m.height(), 40);
    int spawns = 0, lanterns = 0, shrines = 0, goals = 0, enemies = 0;
    for (const Marker& k : m.markers()) {
        switch (k.kind) {
            case MarkerKind::PlayerSpawn: ++spawns; break;
            case MarkerKind::Lantern: ++lanterns; break;
            case MarkerKind::ShrineDash:
            case MarkerKind::ShrineDoubleJump:
            case MarkerKind::ShrineWallJump: ++shrines; break;
            case MarkerKind::Goal: ++goals; break;
            case MarkerKind::Penanggal:
            case MarkerKind::Pocong: ++enemies; break;
            default: break;
        }
        CHECK(!m.solid(k.tx, k.ty));
    }
    CHECK_EQ(spawns, 4);
    CHECK_EQ(lanterns, 4);
    CHECK_EQ(shrines, 3);
    CHECK_EQ(goals, 1);
    CHECK_EQ(enemies, 8);
    CHECK(m.solid(-1, 5) && m.solid(100, 5));  // outside the map is solid
}

TEST(map_rejects_bad_input) {
    bool threw = false;
    try {
        Map::parse("##\n#\n");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        Map::parse("#?#\n");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(player_is_deterministic) {
    const Map& map = Map::world();
    std::mt19937 rng(99);
    std::vector<uint8_t> inputs(3000);
    for (auto& b : inputs) b = static_cast<uint8_t>(rng() & 0x7F);
    PlayerState start;
    start.x = 5 * kTileFx;
    start.y = 33 * kTileFx - tuning::kHeight;
    start.abilities = AbilityDash | AbilityDoubleJump | AbilityWallJump;

    PlayerState a = start, b = start;
    PlayerState checkpoint;
    for (size_t i = 0; i < inputs.size(); ++i) {
        step_player(a, inputs[i], map);
        if (i == 1499) checkpoint = a;
    }
    for (size_t i = 0; i < inputs.size(); ++i) step_player(b, inputs[i], map);
    CHECK(a == b);
    // Replaying from a mid-point (what reconciliation does) gives the same result.
    PlayerState c = checkpoint;
    for (size_t i = 1500; i < inputs.size(); ++i) step_player(c, inputs[i], map);
    CHECK(c == a);
    // Never inside a solid tile.
    PlayerState d = start;
    for (uint8_t in : inputs) {
        step_player(d, in, map);
        const Aabb box = player_box(d);
        for (int ty = box.y >> kTileShift; ty <= (box.y + box.h - 1) >> kTileShift; ++ty) {
            for (int tx = box.x >> kTileShift; tx <= (box.x + box.w - 1) >> kTileShift; ++tx) {
                CHECK(!map.solid(tx, ty));
            }
        }
    }
}

TEST(abilities_gate_the_world) {
    // The spike ravine is 7 tiles wide; the plateau is 5 tiles above the pillar.
    const double plain = running_jump_distance(false);
    const double dashed = running_jump_distance(true);
    std::printf("  running jump %.1f tiles, with dash %.1f tiles\n", plain, dashed);
    CHECK(plain < 7.0);
    CHECK(dashed > 7.5);

    const double single = max_jump_height(0, false);
    const double twice = max_jump_height(AbilityDoubleJump, true);
    std::printf("  jump height %.1f tiles, with double jump %.1f tiles\n", single, twice);
    CHECK(single < 4.5);
    CHECK(twice > 5.5);
}

TEST(wall_jump_climbs_a_shaft) {
    // A 4-tile wide, 20-tile tall shaft: only wall jumps get out.
    std::string s;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 8; ++x) s += (y == 0 || y == 23 || x <= 1 || x >= 6) ? '#' : '.';
        s += '\n';
    }
    const Map map = Map::parse(s);
    auto climb = [&](uint8_t abilities) {
        PlayerState p;
        p.x = 2 * kTileFx;
        p.y = 23 * kTileFx - tuning::kHeight;
        p.abilities = abilities;
        fx best = p.y;
        int dir = -1;
        for (int t = 0; t < 1200; ++t) {
            uint8_t b = dir < 0 ? BtnLeft : BtnRight;
            if ((p.has(FlagWallLeft) && dir < 0) || (p.has(FlagWallRight) && dir > 0)) {
                if (!(p.prev_buttons & BtnJump)) {
                    b |= BtnJump;
                    dir = -dir;
                }
            } else if (p.has(FlagOnGround)) {
                b |= (t % 2) ? BtnJump : 0;
            } else if (p.vy < 0) {
                b |= BtnJump;
            }
            step_player(p, b, map);
            best = std::min(best, p.y);
        }
        return static_cast<double>(23 * kTileFx - tuning::kHeight - best) / kTileFx;
    };
    const double without = climb(0);
    const double with = climb(AbilityWallJump);
    std::printf("  climbed %.1f tiles without wall jump, %.1f with\n", without, with);
    CHECK(without < 5);
    CHECK(with > 15);
}

TEST(one_way_platforms) {
    const Map map = Map::parse(
        "##########\n"
        "#........#\n"
        "#........#\n"
        "#..====..#\n"
        "#........#\n"
        "#........#\n"
        "##########\n");
    PlayerState p;
    p.x = 4 * kTileFx;
    p.y = 6 * kTileFx - tuning::kHeight;
    step_player(p, 0, map);
    CHECK(p.has(FlagOnGround));
    for (int t = 0; t < 40; ++t) step_player(p, BtnJump, map);  // jump up through the platform
    for (int t = 0; t < 60; ++t) step_player(p, 0, map);
    CHECK(p.has(FlagOnGround));
    CHECK_EQ(p.y, 3 * kTileFx - tuning::kHeight);  // standing on it
    for (int t = 0; t < 40; ++t) step_player(p, BtnDown, map);  // drop through
    CHECK_EQ(p.y, 6 * kTileFx - tuning::kHeight);
}

TEST(world_shrines_lanterns_and_spikes) {
    const Map map = Map::parse(
        "############\n"
        "#..........#\n"
        "#P.D.L^^^^^#\n"
        "############\n");
    World w(map);
    CHECK(w.add_player(0));
    CHECK(!w.add_player(0));
    CHECK_EQ(w.abilities(), uint8_t{0});
    for (int t = 0; t < 80 && w.abilities() == 0; ++t) {
        w.apply_command(0, BtnRight, w.tick() << 8);
        w.step();
    }
    CHECK(w.abilities() & AbilityDash);
    CHECK(w.players()[0].state.abilities & AbilityDash);
    for (int t = 0; t < 120 && w.lanterns_lit() == 0; ++t) {
        w.apply_command(0, BtnRight, w.tick() << 8);
        w.step();
    }
    CHECK_EQ(w.lanterns_lit(), uint16_t{1});
    CHECK_EQ(w.players()[0].checkpoint, uint8_t{0});

    // Walk into the spikes: hurt, invulnerable for a while, eventually dead and respawned at the lantern.
    bool hurt = false, died = false, respawned = false;
    for (int t = 0; t < 2000 && !respawned; ++t) {
        w.apply_command(0, BtnRight, w.tick() << 8);
        w.step();
        for (const Event& e : w.take_events()) {
            if (e.type == EventType::PlayerHurt) hurt = true;
            if (e.type == EventType::PlayerDied) died = true;
            if (e.type == EventType::PlayerRespawned) respawned = true;
        }
    }
    CHECK(hurt && died && respawned);
    const PlayerState& p = w.players()[0].state;
    CHECK_EQ(p.hp, tuning::kMaxHp);
    CHECK(!p.has(FlagDead));
    CHECK_EQ(p.x >> kTileShift, 5);  // back at the lantern
}

TEST(world_enemies_hunt_and_can_be_cut_down) {
    const Map map = Map::parse(
        "####################\n"
        "#..................#\n"
        "#..................#\n"
        "#..................#\n"
        "#P.....p...........#\n"
        "####################\n");
    World w(map);
    w.add_player(0);
    CHECK_EQ(w.enemies().size(), size_t{1});
    const fx start_x = w.enemies()[0].x;
    // Stand still: the pocong hops over to us.
    for (int t = 0; t < 600; ++t) {
        w.apply_command(0, 0, w.tick() << 8);
        w.step();
    }
    CHECK(w.enemies()[0].x < start_x);
    // Keep swinging toward it until it dies (re-pressing attack every 20 ticks).
    bool killed = false;
    for (int t = 0; t < 1200 && !killed; ++t) {
        const bool face_left = w.enemies()[0].x < w.players()[0].state.x;
        uint8_t b = (t % 20 == 0) ? BtnAttack : 0;
        if (t % 20 == 10) b |= face_left ? BtnLeft : BtnRight;
        w.apply_command(0, b, w.tick() << 8);
        w.step();
        for (const Event& e : w.take_events()) killed = killed || e.type == EventType::EnemyKilled;
    }
    CHECK(killed);
    CHECK(!w.enemies()[0].alive());
}

TEST(lag_compensation_hits_what_the_player_saw) {
    // A penanggal swoops in from the right, bites the player and recoils. At every moment we ask: if the
    // player swings now, is there a recent tick at which the head was inside the blade even though it isn't
    // any more? A client rendering that tick (it was lagging behind) must get the hit; one rendering the
    // present must not.
    const Map map = Map::parse(
        "##############################\n"
        "#............................#\n"
        "#............................#\n"
        "#............h...............#\n"
        "#....P.......................#\n"
        "##############################\n");
    World w(map);
    w.add_player(0);
    int rewound_cases = 0;
    for (int t = 0; t < 400; ++t) {
        World c = w;
        c.apply_command(0, BtnAttack, c.tick() << 8);
        c.step();
        for (int k = 0; k < 2; ++k) {
            c.apply_command(0, 0, c.tick() << 8);
            c.step();
        }
        PlayerState next = c.players()[0].state;
        step_player(next, 0, map);
        if (!attack_connects_this_tick(next) || !c.enemies()[0].alive() || c.enemies()[0].hurt_ticks > 0) {
            w.apply_command(0, 0, w.tick() << 8);
            w.step();
            continue;
        }
        const Aabb blade = attack_box(next);
        const bool hit_now = blade.overlaps(enemy_box(c.enemies()[0]));
        for (uint32_t back = 4; back <= static_cast<uint32_t>(kMaxRewindTicks) && !hit_now; back += 2) {
            const uint32_t past = c.tick() - back;
            if (!blade.overlaps(c.enemy_box_in_past(0, past << 8))) continue;
            World lagged = c, present = c;
            lagged.apply_command(0, 0, past << 8);
            present.apply_command(0, 0, present.tick() << 8);
            CHECK(lagged.enemies()[0].hp < c.enemies()[0].hp);
            CHECK_EQ(present.enemies()[0].hp, c.enemies()[0].hp);
            CHECK_EQ(lagged.rewound_hits(), c.rewound_hits() + 1);
            ++rewound_cases;
            break;
        }
        // Beyond the rewind window nothing counts, whatever the client claims.
        World cheater = c;
        cheater.apply_command(0, 0, 0);
        if (!hit_now && cheater.tick() > 40) {
            const Aabb oldest = c.enemy_box_in_past(0, (c.tick() - kMaxRewindTicks) << 8);
            if (!blade.overlaps(oldest)) CHECK_EQ(cheater.enemies()[0].hp, c.enemies()[0].hp);
        }
        w.apply_command(0, 0, w.tick() << 8);
        w.step();
    }
    std::printf("  %d moments where only the rewound world registers the hit\n", rewound_cases);
    CHECK(rewound_cases > 0);
}
