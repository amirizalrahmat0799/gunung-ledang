#include <random>

#include "proto/protocol.hpp"
#include "net/connection.hpp"
#include "test.hpp"

using namespace ledang;

TEST(protocol_commands_round_trip) {
    std::vector<UserCmd> cmds;
    for (uint32_t i = 0; i < 8; ++i) cmds.push_back({1000 + i, static_cast<uint8_t>(i * 13 & 0x7F), 5000u * 256 + i * 256 + 17});
    const auto body = encode_commands(cmds);
    std::vector<UserCmd> out;
    CHECK(decode_commands(body, out));
    CHECK(out == cmds);
    CHECK(body.size() < 30);  // 8 commands, with view ticks delta-coded
    std::vector<UserCmd> none;
    CHECK(decode_commands(encode_commands(none), out));
    CHECK(out.empty());
}

TEST(protocol_handshake_packets) {
    const auto req = make_connect_request(0x1122334455667788ull);
    CHECK_EQ(req.size(), kConnectRequestSize);
    CHECK(read_packet_prefix(req) == PacketType::ConnectRequest);
    CHECK_EQ(*read_salt(req), 0x1122334455667788ull);
    const auto ch = make_challenge(1, 2);
    CHECK(ch.size() < req.size());  // never amplifies
    CHECK_EQ(*read_salt(ch, 1), 2ull);
    CHECK(!read_salt(make_denied(DenyReason::ServerFull)).has_value());
}

TEST(protocol_snapshot_full_and_delta) {
    World w(Map::world());
    w.add_player(0);
    w.add_player(2);
    for (int t = 0; t < 30; ++t) {
        w.apply_command(0, BtnRight, w.tick() << 8);
        w.apply_command(2, BtnLeft | BtnJump, w.tick() << 8);
        w.step();
    }
    const Snapshot a = capture(w, 30);
    for (int t = 0; t < 4; ++t) {
        w.apply_command(0, BtnRight, w.tick() << 8);
        w.apply_command(2, 0, w.tick() << 8);
        w.step();
    }
    w.remove_player(2);
    w.add_player(3);
    const Snapshot b = capture(w, 34);

    const auto full = encode_snapshot(b, nullptr);
    Snapshot decoded;
    CHECK(decode_snapshot(full, nullptr, decoded));
    CHECK(decoded == b);

    const auto delta = encode_snapshot(b, &a);
    SnapshotHeader h;
    CHECK(decode_snapshot_header(delta, h));
    CHECK(h.baseline_tick == a.tick);
    Snapshot from_delta;
    CHECK(decode_snapshot(delta, &a, from_delta));
    CHECK(from_delta == b);
    CHECK(!decode_snapshot(delta, nullptr, from_delta));  // needs its baseline
    std::printf("  full snapshot %zu bytes, delta %zu bytes\n", full.size(), delta.size());
    CHECK(delta.size() * 2 < full.size());
    CHECK(full.size() < kMaxPacketSize - 100);

    // Nothing changed: a delta is just the header.
    CHECK(encode_snapshot(b, &b).size() <= 12);

    // Records round-trip into game state.
    CHECK(player_from_record(b.entities[kEntityFirstPlayer]) == w.players()[0].state);
    const EnemyState e = enemy_from_record(b.entities[kEntityFirstEnemy]);
    CHECK_EQ(e.x, w.enemies()[0].x);
    CHECK_EQ(e.hp, w.enemies()[0].hp);
}

TEST(protocol_reliable_messages) {
    const auto w = decode_welcome(encode_welcome(Welcome{3, 123456}));
    CHECK(w && w->slot == 3 && w->server_tick == 123456u);
    const Event ev{EventType::EnemyKilled, 7, 2, -5, 640};
    const auto e = decode_event(encode_event(ev));
    CHECK(e && *e == ev);
    CHECK(!decode_welcome(encode_event(ev)).has_value());
}

TEST(protocol_garbage_never_crashes) {
    std::mt19937 rng(3);
    World w(Map::world());
    w.add_player(1);
    const Snapshot base = capture(w, 0);
    const auto good = encode_snapshot(base, nullptr);
    int accepted = 0;
    for (int i = 0; i < 3000; ++i) {
        std::vector<uint8_t> junk = good;
        const int flips = 1 + static_cast<int>(rng() % 8);
        for (int k = 0; k < flips; ++k) junk[rng() % junk.size()] ^= static_cast<uint8_t>(1u << (rng() % 8));
        if (rng() % 4 == 0) junk.resize(rng() % junk.size());
        Snapshot out;
        if (decode_snapshot(junk, nullptr, out)) ++accepted;
        std::vector<UserCmd> cmds;
        decode_commands(junk, cmds);
        decode_event(junk);
    }
    CHECK(accepted < 3000);
}
