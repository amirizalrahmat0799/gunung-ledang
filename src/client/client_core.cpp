#include "client/client_core.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/clock.hpp"

namespace ledang {

namespace {

constexpr double kTickSeconds = 1.0 / kTickRate;
constexpr double kHandshakeInterval = 0.1;
constexpr double kConnectTimeout = 10.0;
constexpr double kTimeout = 5.0;
constexpr double kMaxSmoothPx = 64.0;   // bigger errors (respawns, teleports) snap instead of sliding
constexpr double kSmoothRate = 12.0;    // the smoothing offset shrinks by e every 1/12 s

double fx_to_px(int32_t v) { return static_cast<double>(v) / kFx; }

}  // namespace

const char* status_name(ClientStatus s) {
    switch (s) {
        case ClientStatus::Idle: return "idle";
        case ClientStatus::Connecting: return "connecting";
        case ClientStatus::Connected: return "connected";
        case ClientStatus::Denied: return "server full";
        case ClientStatus::TimedOut: return "timed out";
        case ClientStatus::Disconnected: return "disconnected";
    }
    return "?";
}

ClientCore::ClientCore(const Map& map) : map_(&map), conditioner_(std::random_device{}()) {}

ClientCore::~ClientCore() {
    if (status_ == ClientStatus::Connected || status_ == ClientStatus::Connecting) disconnect(now_seconds());
}

bool ClientCore::connect(const Address& server, double now) {
    if (!socket_.is_open() && !socket_.open(0)) return false;
    server_ = server;
    std::random_device rd;
    client_salt_ = (static_cast<uint64_t>(rd()) << 32) ^ rd();
    combined_salt_.reset();
    conn_ = Connection{};
    conn_.start(now);
    status_ = ClientStatus::Connecting;
    connect_started_ = now;
    last_handshake_send_ = -1;
    slot_ = -1;
    have_snapshot_ = false;
    return true;
}

void ClientCore::disconnect(double now) {
    (void)now;
    if (combined_salt_) {
        const auto packet = make_disconnect(*combined_salt_);
        for (int i = 0; i < 3; ++i) socket_.send(server_, packet);
    }
    status_ = ClientStatus::Disconnected;
    slot_ = -1;
}

void ClientCore::send_handshake(double now) {
    if (now - last_handshake_send_ < kHandshakeInterval) return;
    last_handshake_send_ = now;
    const auto packet = combined_salt_ ? make_challenge_response(*combined_salt_) : make_connect_request(client_salt_);
    conditioner_.send(socket_, server_, packet, now);
}

std::vector<Event> ClientCore::take_events() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

uint8_t ClientCore::world_abilities() const {
    return static_cast<uint8_t>(latest_.entities[kEntityWorld].f[field::WAbilities]);
}

PlayerState ClientCore::server_player() const {
    if (slot_ < 0) return {};
    return player_from_record(latest_.entities[static_cast<size_t>(kEntityFirstPlayer + slot_)]);
}

double ClientCore::local_draw_x() const { return fx_to_px(predicted_.x) + (smoothing_ ? smooth_x_ : 0.0); }
double ClientCore::local_draw_y() const { return fx_to_px(predicted_.y) + (smoothing_ ? smooth_y_ : 0.0); }

void ClientCore::update(double now, uint8_t buttons) {
    if (!socket_.is_open()) return;
    const double dt = last_frame_ < 0 ? 0.0 : std::clamp(now - last_frame_, 0.0, 0.25);
    last_frame_ = now;

    receive(now);

    if (status_ == ClientStatus::Connecting) {
        send_handshake(now);
        if (now - connect_started_ > kConnectTimeout) status_ = ClientStatus::TimedOut;
    } else if (status_ == ClientStatus::Connected && now - conn_.last_receive_time() > kTimeout) {
        status_ = ClientStatus::TimedOut;
    }

    if (status_ == ClientStatus::Connected && slot_ >= 0) {
        if (next_tick_time_ < 0) next_tick_time_ = now;
        int ran = 0;
        while (now >= next_tick_time_ && ran < 8) {
            client_tick(buttons, now);
            next_tick_time_ += kTickSeconds;
            ++ran;
        }
        if (ran == 8) next_tick_time_ = std::max(next_tick_time_, now);
    }

    // Interpolation clock: runs in real time, gently steered toward the snapshot stream.
    if (have_snapshot_) {
        clock_tick_ += dt * kTickRate;
        const double loss_margin = conn_.stats().loss > 0.02 ? kSnapshotInterval : 0.0;
        const double target = std::clamp(2.0 * kSnapshotInterval + 2.5 * stats_.jitter_ticks + loss_margin, 3.0, 24.0);
        const double max_step = dt * 2.0;  // change the delay by at most 2 ticks per second: no visible jumps
        interp_delay_ += std::clamp(target - interp_delay_, -max_step, max_step);
        stats_.interp_delay_ticks = interp_delay_;
        render_tick_ = std::max(0.0, clock_tick_ - interp_delay_);
        update_views();
    }

    const double decay = std::exp(-dt * kSmoothRate);
    smooth_x_ *= decay;
    smooth_y_ *= decay;

    conditioner_.flush(socket_, now);
}

void ClientCore::receive(double now) {
    std::array<uint8_t, 1500> buffer{};
    Address from;
    while (size_t n = socket_.receive(from, buffer)) {
        if (!(from == server_)) continue;
        const std::span<const uint8_t> packet(buffer.data(), n);
        const auto type = read_packet_prefix(packet);
        if (!type) continue;
        switch (*type) {
            case PacketType::Challenge: {
                const auto echoed = read_salt(packet, 0);
                const auto server_salt = read_salt(packet, 1);
                if (status_ == ClientStatus::Connecting && echoed && server_salt && *echoed == client_salt_) {
                    combined_salt_ = client_salt_ ^ *server_salt;
                    last_handshake_send_ = -1;
                    send_handshake(now);
                }
                break;
            }
            case PacketType::Denied:
                if (status_ == ClientStatus::Connecting) status_ = ClientStatus::Denied;
                break;
            case PacketType::Payload:
                if (combined_salt_ && (status_ == ClientStatus::Connecting || status_ == ClientStatus::Connected)) {
                    handle_payload(packet, now);
                }
                break;
            case PacketType::Disconnect:
                status_ = ClientStatus::Disconnected;
                break;
            case PacketType::ConnectRequest:
            case PacketType::ChallengeResponse:
                break;
        }
    }
}

void ClientCore::handle_payload(std::span<const uint8_t> packet, double now) {
    std::span<const uint8_t> body;
    if (!conn_.process_packet(packet, now, body)) return;
    conn_.take_acked_tags();

    while (auto msg = conn_.next_message()) {
        const auto type = reliable_type(*msg);
        if (type == ReliableType::Welcome) {
            if (const auto w = decode_welcome(*msg)) {
                slot_ = w->slot;
                status_ = ClientStatus::Connected;
                next_tick_time_ = -1;
            }
        } else if (type == ReliableType::GameEvent) {
            if (const auto e = decode_event(*msg)) events_.push_back(*e);
        }
    }
    if (!body.empty()) handle_snapshot(body, now);
    if (reconcile_pending_ && slot_ >= 0) {
        reconcile();
        reconcile_pending_ = false;
    }
    conn_.update_stats(now);
}

void ClientCore::handle_snapshot(std::span<const uint8_t> body, double now) {
    SnapshotHeader header;
    if (!decode_snapshot_header(body, header)) {
        ++stats_.decode_failures;
        return;
    }
    const Snapshot* baseline = nullptr;
    if (header.baseline_tick) {
        const Snapshot& b = ring_[(*header.baseline_tick / kSnapshotInterval) % kRing];
        if (b.tick != *header.baseline_tick) {
            ++stats_.missing_baselines;
            return;
        }
        baseline = &b;
    }
    Snapshot snap;
    if (!decode_snapshot(body, baseline, snap)) {
        ++stats_.decode_failures;
        return;
    }
    ++stats_.snapshots;
    stats_.snapshot_bytes += body.size();
    const uint32_t tick = snap.tick;
    ring_[(tick / kSnapshotInterval) % kRing] = snap;
    if (have_snapshot_ && tick <= latest_.tick) return;  // late (reordered) snapshot: kept only as a baseline

    if (!have_snapshot_) {
        clock_tick_ = tick;
    } else {
        // Jitter: how far the arrival spacing differs from the tick spacing.
        const double arrived = (now - last_snapshot_arrival_) * kTickRate;
        const double expected = static_cast<double>(tick - last_snapshot_tick_);
        stats_.jitter_ticks = stats_.jitter_ticks * 0.95 + std::abs(arrived - expected) * 0.05;
        const double error = static_cast<double>(tick) - clock_tick_;
        if (std::abs(error) > 30) {
            clock_tick_ = tick;  // far off (stall, first packets after a hitch): jump
        } else {
            clock_tick_ += error * 0.1;
        }
    }
    last_snapshot_arrival_ = now;
    last_snapshot_tick_ = tick;
    latest_ = std::move(snap);
    have_snapshot_ = true;
    reconcile_pending_ = true;
}

void ClientCore::client_tick(uint8_t buttons, double now) {
    UserCmd cmd;
    cmd.number = next_cmd_++;
    cmd.buttons = buttons;
    cmd.view_tick_q8 = static_cast<uint32_t>(render_tick_ * 256.0);
    cmd_history_[cmd.number % kCmdHistory] = cmd;

    if (prediction_ && have_snapshot_) {
        predicted_.abilities = world_abilities();
        step_player(predicted_, buttons, *map_);
    }

    const uint32_t first = cmd.number >= static_cast<uint32_t>(kCmdRedundancy) ? cmd.number - kCmdRedundancy + 1 : 1;
    std::vector<UserCmd> recent;
    for (uint32_t n = first; n <= cmd.number; ++n) recent.push_back(cmd_history_[n % kCmdHistory]);
    const auto body = encode_commands(recent);
    const auto packet = conn_.build_packet(body, cmd.number, now);
    conditioner_.send(socket_, server_, packet, now);
}

void ClientCore::reconcile() {
    const EntityRecord& rec = latest_.entities[static_cast<size_t>(kEntityFirstPlayer + slot_)];
    if (rec.type != EntityType::Player) return;
    const PlayerState server_state = player_from_record(rec);

    if (!prediction_) {
        predicted_ = server_state;
        smooth_x_ = smooth_y_ = 0;
        return;
    }

    // Start from the server's word and re-run every command it hasn't seen yet.
    PlayerState p = server_state;
    const uint32_t newest = next_cmd_ - 1;
    const uint32_t acked = latest_.last_cmd;
    if (newest > acked && newest - acked < kCmdHistory) {
        for (uint32_t n = acked + 1; n <= newest; ++n) {
            const UserCmd& c = cmd_history_[n % kCmdHistory];
            if (c.number != n) break;
            p.abilities = world_abilities();
            step_player(p, c.buttons, *map_);
        }
    }

    ++stats_.predictions;
    const double ex = fx_to_px(predicted_.x) - fx_to_px(p.x);
    const double ey = fx_to_px(predicted_.y) - fx_to_px(p.y);
    stats_.last_error_px = std::sqrt(ex * ex + ey * ey);
    if (!(predicted_ == p)) {
        ++stats_.corrections;
        if (std::abs(ex) < kMaxSmoothPx && std::abs(ey) < kMaxSmoothPx) {
            smooth_x_ += ex;
            smooth_y_ += ey;
        } else {
            smooth_x_ = smooth_y_ = 0;
        }
    }
    predicted_ = p;
}

void ClientCore::update_views() {
    // Find the two snapshots around render_tick_.
    const Snapshot* a = nullptr;
    const Snapshot* b = nullptr;
    for (const Snapshot& s : ring_) {
        if (s.entities[kEntityWorld].type == EntityType::None || s.tick > latest_.tick) continue;
        const double t = static_cast<double>(s.tick);
        if (t <= render_tick_ && (!a || s.tick > a->tick)) a = &s;
        if (t > render_tick_ && (!b || s.tick < b->tick)) b = &s;
    }
    double frac = 0;
    if (!b) {
        ++stats_.starved_frames;  // nothing newer yet: hold the newest state
        b = a;
    } else if (!a) {
        a = b;
    } else {
        frac = (render_tick_ - a->tick) / static_cast<double>(b->tick - a->tick);
    }
    if (!a) return;

    for (int id = 0; id < kMaxEntities; ++id) {
        const EntityRecord& rb = b->entities[static_cast<size_t>(id)];
        const EntityRecord& ra = a->entities[static_cast<size_t>(id)];
        EntityView& v = views_[static_cast<size_t>(id)];
        v.present = rb.type == EntityType::Player || rb.type == EntityType::Enemy;
        if (!v.present) continue;
        const int fxi = rb.type == EntityType::Player ? int{field::PX} : int{field::EX};
        const int fyi = rb.type == EntityType::Player ? int{field::PY} : int{field::EY};
        const double bx = fx_to_px(rb.f[static_cast<size_t>(fxi)]);
        const double by = fx_to_px(rb.f[static_cast<size_t>(fyi)]);
        double x = bx, y = by;
        if (ra.type == rb.type) {
            const double ax = fx_to_px(ra.f[static_cast<size_t>(fxi)]);
            const double ay = fx_to_px(ra.f[static_cast<size_t>(fyi)]);
            if (std::abs(bx - ax) < 3 * kTile && std::abs(by - ay) < 3 * kTile) {  // otherwise a teleport
                x = ax + (bx - ax) * frac;
                y = ay + (by - ay) * frac;
            }
        }
        v.x = x;
        v.y = y;
        v.record = (frac < 0.5 && ra.type == rb.type) ? ra : rb;
    }
}

}  // namespace ledang
