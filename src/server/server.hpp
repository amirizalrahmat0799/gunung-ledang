#pragma once
// The dedicated server: owns the authoritative World, runs it at 60 Hz, and talks to up to four clients.
//
// Per tick:
//   1. run each client's queued user commands (at most kMaxCmdsPerTick, and on average one per tick, so a
//      client can't move faster by sending more commands: a speed-hack guard);
//   2. advance enemies and world rules;
//   3. send game events as reliable messages;
//   4. every kSnapshotInterval ticks, send each client a snapshot delta-compressed against the newest
//      snapshot that client has acknowledged.

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include "net/connection.hpp"
#include "net/link_conditioner.hpp"
#include "net/socket.hpp"
#include "proto/protocol.hpp"
#include "sim/world.hpp"

namespace ledang {

constexpr uint16_t kDefaultPort = 27015;
constexpr double kTimeoutSeconds = 5.0;
constexpr int kMaxCmdsPerTick = 4;
constexpr int kMaxQueuedCmds = 12;
constexpr int kSnapshotRing = 32;  // snapshots kept per client as delta baselines (~1 s)

struct ServerStats {
    uint64_t ticks = 0;
    uint64_t snapshots_sent = 0;
    uint64_t snapshot_bytes = 0;
    uint64_t delta_snapshots = 0;
    uint64_t cmds_run = 0;
    uint64_t cmds_dropped = 0;   // overflowed the queue (client running fast) or arrived too late
    uint64_t cmd_gaps = 0;       // commands that never arrived (every redundant copy lost)
    uint64_t bad_packets = 0;
};

class Server {
public:
    explicit Server(const Map& map = Map::world());

    /// Opens the UDP port (0 picks a free one).
    bool start(uint16_t port);
    uint16_t port() const { return socket_.local_port(); }

    /// Call often (every millisecond or so): receives packets and runs any simulation ticks that are due.
    void update(double now);

    void set_link_profile(const LinkProfile& p) { conditioner_.set_profile(p); }
    void set_log(bool enabled) { log_ = enabled; }
    /// Development: see World::debug_unlock. Call before players join.
    void debug_unlock(uint8_t abilities, int lantern) { world_.debug_unlock(abilities, lantern); }

    const World& world() const { return world_; }
    const ServerStats& stats() const { return stats_; }
    int client_count() const;
    /// Connection statistics for a slot, if a client is there.
    const ConnectionStats* client_stats(int slot) const;
    uint32_t last_cmd(int slot) const;

private:
    struct Client {
        Address address;
        uint64_t salt = 0;
        Connection conn;
        std::deque<UserCmd> queue;
        uint32_t last_cmd = 0;   // newest command run
        bool any_cmd = false;
        double budget = 1.0;
        std::array<Snapshot, kSnapshotRing> sent{};
        std::optional<uint32_t> acked_tick;  // newest snapshot the client is known to have
    };
    struct Pending {
        uint64_t client_salt;
        uint64_t server_salt;
        double created;
    };

    void receive(double now);
    void handle_packet(const Address& from, std::span<const uint8_t> packet, double now);
    void tick(double now);
    void run_commands(int slot, Client& c);
    void send_snapshot(int slot, Client& c, double now);
    void drop(int slot, const char* reason);
    int find_client(const Address& a) const;
    void send(const Address& to, std::span<const uint8_t> data, double now);
    void log(const std::string& line) const;

    World world_;
    UdpSocket socket_;
    LinkConditioner conditioner_;
    std::array<std::unique_ptr<Client>, kMaxPlayers> clients_{};
    std::map<uint64_t, Pending> pending_;  // keyed by packed address
    std::mt19937_64 rng_;
    double next_tick_ = -1;
    ServerStats stats_;
    bool log_ = false;
};

}  // namespace ledang
