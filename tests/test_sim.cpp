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

namespace {

std::vector<Event> run(World& w, uint8_t buttons, int ticks) {
    std::vector<Event> all;
    for (int t = 0; t < ticks; ++t) {
        w.update(buttons);
        for (const Event& e : w.take_events()) all.push_back(e);
    }
    return all;
}

bool has(const std::vector<Event>& events, EventType type) {
    return std::any_of(events.begin(), events.end(), [type](const Event& e) { return e.type == type; });
}

}  // namespace

TEST(world_shrines_lanterns_and_stakes) {
    const Map map = Map::parse(
        "############\n"
        "#..........#\n"
        "#P.D.L^^^^^#\n"
        "############\n");
    World w(map);
    CHECK_EQ(w.abilities(), uint8_t{0});
    auto events = run(w, BtnRight, 80);
    CHECK(w.abilities() & AbilityDash);
    CHECK(w.player().abilities & AbilityDash);
    CHECK(has(events, EventType::AbilityGained));
    CHECK(has(events, EventType::LanternLit));
    CHECK(has(events, EventType::Rested));
    CHECK_EQ(w.lanterns_lit(), uint16_t{1});
    CHECK_EQ(w.checkpoint(), uint8_t{0});

    // Walk into the stakes: you're hurt and put back on safe ground, and the lantern you pass heals you again.
    events = run(w, BtnRight, 400);
    CHECK(has(events, EventType::PlayerHurt));
    CHECK(!has(events, EventType::PlayerDied));
    CHECK(w.player().x >> kTileShift <= 5);
}

TEST(death_returns_you_to_the_last_lantern) {
    const Map map = Map::parse(
        "##############\n"
        "#............#\n"
        "#P..L....^^^^#\n"
        "##############\n");
    World w(map);
    auto events = run(w, BtnRight, 3000);  // the stakes take one heart per visit; the fifth visit kills
    const auto hurts = std::count_if(events.begin(), events.end(), [](const Event& e) { return e.type == EventType::PlayerHurt; });
    CHECK(hurts >= 4);
    CHECK(has(events, EventType::PlayerDied));
    CHECK(has(events, EventType::PlayerRespawned));
    CHECK(w.deaths() >= 1u);
}

TEST(stake_hit_returns_to_safe_ground) {
    const Map map = Map::parse(
        "##############\n"
        "#............#\n"
        "#............#\n"
        "#P...........#\n"
        "#####^^^^#####\n"
        "##############\n");
    World w(map);
    run(w, BtnRight, 40);  // walk off the edge into the pit
    const PlayerState& p = w.player();
    CHECK_EQ(p.hp, uint8_t{tuning::kMaxHp - 1});
    CHECK(p.x >> kTileShift < 4);  // back on the ledge, left of the pit (and of the 1-tile margin)
    CHECK(p.has(FlagOnGround) || p.vy >= 0);
}

TEST(downward_strike_pogos_off_stakes) {
    // A long stake pit with no floor to stand on: bouncing on the stakes is the only way across.
    const Map map = Map::parse(
        "####################\n"
        "#..................#\n"
        "#..................#\n"
        "#..................#\n"
        "#..................#\n"
        "#P.................#\n"
        "###^^^^^^^^^^^^^^###\n"
        "####################\n");
    World w(map);
    int pogos = 0;
    bool hurt = false;
    // Run right; in the air, keep striking downward.
    for (int t = 0; t < 600 && (w.player().x >> kTileShift) < 17; ++t) {
        const PlayerState& p = w.player();
        uint8_t b = BtnRight;
        if (t < 3) b |= BtnJump;
        const fx feet = p.y + tuning::kHeight, stakes = 6 * kTileFx;
        if (!p.has(FlagOnGround) && p.vy > 0 && p.attack_ticks == 0 && stakes - feet < px(30)) b |= BtnDown | BtnAttack;
        if (!p.has(FlagOnGround) && p.vy > 0) b |= BtnDown;
        w.update(b);
        for (const Event& e : w.take_events()) {
            if (e.type == EventType::Pogo) ++pogos;
            if (e.type == EventType::PlayerHurt || e.type == EventType::PlayerDied) hurt = true;
        }
    }
    std::printf("  crossed with %d bounces\n", pogos);
    CHECK(pogos >= 3);
    CHECK(!hurt);
    CHECK((w.player().x >> kTileShift) >= 17);
}

TEST(strikes_hit_in_three_directions) {
    // A penanggal hovering right above the player: only an upward strike reaches it.
    const Map map = Map::parse(
        "##########\n"
        "#........#\n"
        "#........#\n"
        "#........#\n"
        "#...h....#\n"
        "#...P....#\n"
        "##########\n");
    auto first_hit = [&](uint8_t held) {
        World w(map);
        w.set_spirits_active(false);
        run(w, 0, 2);
        const auto ev = run(w, static_cast<uint8_t>(held | BtnAttack), 1);
        const auto later = run(w, held, 20);
        return has(ev, EventType::EnemyHit) || has(later, EventType::EnemyHit);
    };
    CHECK(first_hit(BtnUp));
    CHECK(!first_hit(0));
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
    CHECK_EQ(w.enemies().size(), size_t{1});
    const fx start_x = w.enemies()[0].x;
    run(w, 0, 600);  // stand still: the pocong hops over
    CHECK(w.enemies()[0].x < start_x);
    bool killed = false;
    for (int t = 0; t < 1200 && !killed; ++t) {
        const bool face_left = w.enemies()[0].x < w.player().x;
        uint8_t b = (t % 20 == 0) ? BtnAttack : 0;
        if (t % 20 == 10) b |= face_left ? BtnLeft : BtnRight;
        w.update(b);
        killed = has(w.take_events(), EventType::EnemyKilled) || killed;
    }
    CHECK(killed);
    CHECK(!w.enemies()[0].alive());
}

TEST(whole_world_is_deterministic) {
    // Two worlds fed the same random inputs stay identical, spirits included: the basis for replays (and for
    // bringing co-op back one day).
    std::mt19937 rng(2026);
    World a(Map::world()), b(Map::world());
    a.debug_unlock(AbilityDash | AbilityDoubleJump | AbilityWallJump, 3);
    b.debug_unlock(AbilityDash | AbilityDoubleJump | AbilityWallJump, 3);
    bool same = true;
    for (int t = 0; t < 6000 && same; ++t) {
        const auto buttons = static_cast<uint8_t>(rng() & 0x7F);
        a.update(buttons);
        b.update(buttons);
        same = a.player() == b.player() && a.enemies() == b.enemies() && a.take_events() == b.take_events();
    }
    CHECK(same);
}
