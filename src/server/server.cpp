#include "server/server.hpp"

#include <algorithm>
#include <cstdio>

namespace ledang {

namespace {

uint64_t address_key(const Address& a) { return (static_cast<uint64_t>(a.ip) << 16) | a.port; }

constexpr double kTickSeconds = 1.0 / kTickRate;
constexpr double kPendingTimeout = 5.0;

}  // namespace

Server::Server(const Map& map) : world_(map), rng_(std::random_device{}()) {}

bool Server::start(uint16_t port) {
    if (!socket_.open(port)) return false;
    log("listening on UDP port " + std::to_string(socket_.local_port()));
    return true;
}

void Server::log(const std::string& line) const {
    if (log_) std::printf("[server] %s\n", line.c_str());
}

int Server::client_count() const {
    return static_cast<int>(std::count_if(clients_.begin(), clients_.end(), [](const auto& c) { return c != nullptr; }));
}

const ConnectionStats* Server::client_stats(int slot) const {
    const auto& c = clients_[static_cast<size_t>(slot)];
    return c ? &c->conn.stats() : nullptr;
}

uint32_t Server::last_cmd(int slot) const {
    const auto& c = clients_[static_cast<size_t>(slot)];
    return c ? c->last_cmd : 0;
}

int Server::find_client(const Address& a) const {
    for (int i = 0; i < kMaxPlayers; ++i) {
        const auto& c = clients_[static_cast<size_t>(i)];
        if (c && c->address == a) return i;
    }
    return -1;
}

void Server::send(const Address& to, std::span<const uint8_t> data, double now) {
    conditioner_.send(socket_, to, data, now);
}

void Server::update(double now) {
    if (next_tick_ < 0) next_tick_ = now;
    receive(now);
    int ran = 0;
    while (now >= next_tick_) {
        tick(now);
        next_tick_ += kTickSeconds;
        // After a long stall (debugger, sleep) skip ahead instead of running hundreds of ticks at once.
        if (++ran >= 8) {
            next_tick_ = std::max(next_tick_, now);
            break;
        }
    }
    conditioner_.flush(socket_, now);
}

void Server::receive(double now) {
    std::array<uint8_t, 1500> buffer{};
    Address from;
    while (size_t n = socket_.receive(from, buffer)) {
        handle_packet(from, std::span<const uint8_t>(buffer.data(), n), now);
    }
}

void Server::handle_packet(const Address& from, std::span<const uint8_t> packet, double now) {
    const auto type = read_packet_prefix(packet);
    if (!type) {
        ++stats_.bad_packets;
        return;
    }
    const int slot = find_client(from);

    switch (*type) {
        case PacketType::ConnectRequest: {
            if (slot >= 0 || packet.size() < kConnectRequestSize) return;
            const auto client_salt = read_salt(packet);
            if (!client_salt) return;
            if (client_count() >= kMaxPlayers) {
                send(from, make_denied(DenyReason::ServerFull), now);
                return;
            }
            auto it = pending_.find(address_key(from));
            if (it == pending_.end() || it->second.client_salt != *client_salt) {
                it = pending_.insert_or_assign(address_key(from), Pending{*client_salt, rng_(), now}).first;
            }
            send(from, make_challenge(it->second.client_salt, it->second.server_salt), now);
            return;
        }
        case PacketType::ChallengeResponse: {
            if (slot >= 0 || packet.size() < kConnectRequestSize) return;
            const auto it = pending_.find(address_key(from));
            const auto combined = read_salt(packet);
            if (it == pending_.end() || !combined || *combined != (it->second.client_salt ^ it->second.server_salt)) {
                return;
            }
            int free_slot = -1;
            for (int i = 0; i < kMaxPlayers && free_slot < 0; ++i) {
                if (!clients_[static_cast<size_t>(i)]) free_slot = i;
            }
            if (free_slot < 0) {
                send(from, make_denied(DenyReason::ServerFull), now);
                return;
            }
            auto c = std::make_unique<Client>();
            c->address = from;
            c->salt = *combined;
            c->conn.start(now);
            pending_.erase(it);
            world_.add_player(free_slot);
            c->conn.send_reliable(encode_welcome(Welcome{static_cast<uint8_t>(free_slot), world_.tick()}));
            clients_[static_cast<size_t>(free_slot)] = std::move(c);
            log(from.to_string() + " joined as player " + std::to_string(free_slot + 1));
            return;
        }
        case PacketType::Payload: {
            if (slot < 0) return;
            Client& c = *clients_[static_cast<size_t>(slot)];
            std::span<const uint8_t> body;
            if (!c.conn.process_packet(packet, now, body)) return;
            for (uint32_t tag : c.conn.take_acked_tags()) {
                if (!c.acked_tick || tag > *c.acked_tick) c.acked_tick = tag;
            }
            while (c.conn.next_message()) {
                // Clients send no reliable messages yet; ignore anything that arrives.
            }
            if (body.empty()) return;
            std::vector<UserCmd> cmds;
            if (!decode_commands(body, cmds)) {
                ++stats_.bad_packets;
                return;
            }
            // Keep the queue sorted and free of duplicates: packets can arrive out of order, and every command
            // arrives up to kCmdRedundancy times.
            for (const UserCmd& cmd : cmds) {
                if (c.any_cmd && cmd.number <= c.last_cmd) continue;
                auto pos = std::lower_bound(c.queue.begin(), c.queue.end(), cmd.number,
                                            [](const UserCmd& q, uint32_t n) { return q.number < n; });
                if (pos != c.queue.end() && pos->number == cmd.number) continue;
                c.queue.insert(pos, cmd);
            }
            while (c.queue.size() > static_cast<size_t>(kMaxQueuedCmds)) {
                c.queue.pop_front();
                ++stats_.cmds_dropped;
            }
            return;
        }
        case PacketType::Disconnect: {
            const auto salt = read_salt(packet);
            if (slot >= 0 && salt && *salt == clients_[static_cast<size_t>(slot)]->salt) drop(slot, "disconnected");
            return;
        }
        case PacketType::Challenge:
        case PacketType::Denied:
            return;
    }
}

void Server::drop(int slot, const char* reason) {
    log("player " + std::to_string(slot + 1) + " " + reason);
    clients_[static_cast<size_t>(slot)].reset();
    world_.remove_player(slot);
}

void Server::run_commands(int slot, Client& c) {
    c.budget = std::min(c.budget + 1.0, static_cast<double>(kMaxCmdsPerTick));
    int ran = 0;
    while (!c.queue.empty() && c.budget >= 1.0 && ran < kMaxCmdsPerTick) {
        const UserCmd cmd = c.queue.front();
        c.queue.pop_front();
        if (c.any_cmd && cmd.number != c.last_cmd + 1) stats_.cmd_gaps += cmd.number - c.last_cmd - 1;
        world_.apply_command(slot, cmd.buttons, cmd.view_tick_q8);
        c.last_cmd = cmd.number;
        c.any_cmd = true;
        c.budget -= 1.0;
        ++ran;
        ++stats_.cmds_run;
    }
}

void Server::tick(double now) {
    ++stats_.ticks;

    for (auto it = pending_.begin(); it != pending_.end();) {
        it = now - it->second.created > kPendingTimeout ? pending_.erase(it) : std::next(it);
    }
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        auto& c = clients_[static_cast<size_t>(slot)];
        if (c && now - c->conn.last_receive_time() > kTimeoutSeconds) drop(slot, "timed out");
    }

    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        if (auto& c = clients_[static_cast<size_t>(slot)]) run_commands(slot, *c);
    }
    world_.step();

    for (const Event& e : world_.take_events()) {
        const auto msg = encode_event(e);
        for (auto& c : clients_) {
            if (c) c->conn.send_reliable(msg);
        }
    }

    if (world_.tick() % kSnapshotInterval == 0) {
        for (int slot = 0; slot < kMaxPlayers; ++slot) {
            if (auto& c = clients_[static_cast<size_t>(slot)]) send_snapshot(slot, *c, now);
        }
    }
}

void Server::send_snapshot(int slot, Client& c, double now) {
    (void)slot;
    Snapshot snap = capture(world_, c.last_cmd);
    const Snapshot* baseline = nullptr;
    if (c.acked_tick) {
        const Snapshot& b = c.sent[(*c.acked_tick / kSnapshotInterval) % kSnapshotRing];
        const uint32_t age = snap.tick - *c.acked_tick;
        if (b.tick == *c.acked_tick && age > 0 && age < static_cast<uint32_t>(kSnapshotRing * kSnapshotInterval)) {
            baseline = &b;
        }
    }
    const std::vector<uint8_t> body = encode_snapshot(snap, baseline);
    const std::vector<uint8_t> packet = c.conn.build_packet(body, snap.tick, now);
    send(c.address, packet, now);
    c.conn.update_stats(now);
    ++stats_.snapshots_sent;
    stats_.snapshot_bytes += body.size();
    if (baseline) ++stats_.delta_snapshots;
    c.sent[(snap.tick / kSnapshotInterval) % kSnapshotRing] = std::move(snap);
}

}  // namespace ledang
