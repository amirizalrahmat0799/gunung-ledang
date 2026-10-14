// End to end: a real server and real clients talking over loopback UDP, in fast virtual time.
#include <cmath>
#include <memory>

#include "bot/bot_brain.hpp"
#include "client/client_core.hpp"
#include "core/clock.hpp"
#include "server/server.hpp"
#include "test.hpp"

using namespace ledang;

namespace {

struct Game {
    Server server;
    std::vector<std::unique_ptr<ClientCore>> clients;
    std::vector<BotBrain> brains;
    double now = 0;

    explicit Game(int bots, LinkProfile link = {}) {
        server.start(0);
        server.set_link_profile(link);
        const Address addr{0x7F000001, server.port()};
        for (int i = 0; i < bots; ++i) {
            clients.push_back(std::make_unique<ClientCore>());
            clients.back()->set_link_profile(link);
            clients.back()->connect(addr, now);
            brains.emplace_back(static_cast<uint32_t>(77 + i));
        }
    }

    /// Advances virtual time in 4 ms frames. Loopback delivery is immediate, so a short real pause per
    /// frame is enough for packets to land.
    void run(double seconds, bool think = true) {
        const double end = now + seconds;
        double next_think = now;
        std::vector<uint8_t> buttons(clients.size(), 0);
        while (now < end) {
            now += 0.004;
            if (think && now >= next_think) {
                next_think += 1.0 / kTickRate;
                for (size_t i = 0; i < clients.size(); ++i) buttons[i] = brains[i].think(*clients[i]);
            }
            server.update(now);
            for (size_t i = 0; i < clients.size(); ++i) clients[i]->update(now, think ? buttons[i] : 0);
            spin();
        }
    }

    static void spin() {
        // Let the kernel move datagrams; ~50 us is plenty on loopback.
        const double until = now_seconds() + 0.00005;
        while (now_seconds() < until) {
        }
    }
};

}  // namespace

TEST(netgame_connect_and_play) {
    Game g(2);
    g.run(1.0);
    CHECK_EQ(g.server.client_count(), 2);
    for (auto& c : g.clients) {
        CHECK(c->status() == ClientStatus::Connected);
        CHECK(c->in_game());
        CHECK(c->slot() >= 0);
    }
    CHECK(g.clients[0]->slot() != g.clients[1]->slot());
    g.run(5.0);
    // Each client sees the other one.
    const int other = g.clients[1]->slot();
    CHECK(g.clients[0]->views()[static_cast<size_t>(kEntityFirstPlayer + other)].present);
    // Delta compression is in use.
    CHECK(g.server.stats().delta_snapshots * 10 > g.server.stats().snapshots_sent * 9);

    // Stop moving; prediction must settle exactly on the server's state.
    for (auto& b : g.brains) b.set_idle(true);
    g.run(1.5);
    for (auto& c : g.clients) {
        CHECK(c->acked_cmd() <= c->newest_cmd());
        const PlayerState server_state = c->server_player();
        if (server_state.hurt_ticks > 0 || server_state.has(FlagDead)) continue;  // knockback still in flight
        CHECK_EQ(c->local_player().x, server_state.x);
        CHECK_EQ(c->local_player().y, server_state.y);
    }
    g.clients[0]->disconnect(g.now);
    g.run(0.3);
    CHECK_EQ(g.server.client_count(), 1);
}

TEST(netgame_server_full) {
    Game g(kMaxPlayers + 1);
    g.run(1.5);
    int connected = 0, denied = 0;
    for (auto& c : g.clients) {
        if (c->status() == ClientStatus::Connected) ++connected;
        if (c->status() == ClientStatus::Denied) ++denied;
    }
    CHECK_EQ(connected, kMaxPlayers);
    CHECK_EQ(denied, 1);
}

// The soak test: three bots for two minutes of game time on a bad network (150 ms each way +-20 ms,
// 5% loss in both directions).
TEST(soak_three_bots_on_a_bad_network) {
    Game g(3, LinkProfile{75, 20, 0.05, 0.01});
    g.run(120.0);

    const ServerStats& s = g.server.stats();
    std::printf("  server: %llu ticks, %llu cmds run, %llu gaps, %llu dropped, avg snapshot %.0f bytes, %.0f%% delta\n",
                static_cast<unsigned long long>(s.ticks), static_cast<unsigned long long>(s.cmds_run),
                static_cast<unsigned long long>(s.cmd_gaps), static_cast<unsigned long long>(s.cmds_dropped),
                static_cast<double>(s.snapshot_bytes) / static_cast<double>(s.snapshots_sent),
                100.0 * static_cast<double>(s.delta_snapshots) / static_cast<double>(s.snapshots_sent));
    for (auto& c : g.clients) {
        const auto& n = c->net_stats();
        const auto& st = c->stats();
        std::printf("  client %d: rtt %.0f ms, loss %.1f%%, in %.1f kbps, out %.1f kbps, corrections %llu/%llu, "
                    "interp %.1f ticks, starved frames %llu\n",
                    c->slot() + 1, n.rtt_ms, n.loss * 100, n.kbps_in, n.kbps_out,
                    static_cast<unsigned long long>(st.corrections), static_cast<unsigned long long>(st.predictions),
                    st.interp_delay_ticks, static_cast<unsigned long long>(st.starved_frames));
        CHECK(c->status() == ClientStatus::Connected);
        CHECK_EQ(st.decode_failures, uint64_t{0});
        CHECK_EQ(st.missing_baselines, uint64_t{0});
        CHECK(n.rtt_ms > 130 && n.rtt_ms < 230);
        CHECK(n.kbps_in < 64);
        CHECK(n.kbps_out < 64);
        CHECK(st.interp_delay_ticks >= 4 && st.interp_delay_ticks < 16);
    }
    // With 8x command redundancy, 5% loss almost never loses an input.
    CHECK(s.cmd_gaps * 1000 < s.cmds_run);

    for (auto& b : g.brains) b.set_idle(true);
    g.run(2.0, false);
    // Standing still, prediction must have converged on exactly the server's position. (Spirits keep
    // hunting idle players, so anyone hit in the last second is skipped: their knockback is still in flight.)
    int settled = 0;
    for (auto& c : g.clients) {
        const PlayerState server_state = c->server_player();
        if (server_state.hurt_ticks > 0 || server_state.has(FlagDead)) continue;
        ++settled;
        CHECK_EQ(c->local_player().x, server_state.x);
        CHECK_EQ(c->local_player().y, server_state.y);
        CHECK(std::abs(c->local_draw_x() - c->local_player().x / 256.0) < 0.5);  // smoothing has faded
    }
    CHECK(settled > 0);
}
