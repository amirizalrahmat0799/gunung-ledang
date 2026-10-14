#pragma once
// Everything a client does except drawing: connecting, sending user commands, predicting the local player,
// reconciling with the server, and interpolating everyone else. The SDL game and the headless bot both use it.
//
// Two timelines are shown on screen at once, as in Source games:
//   - the local player is *predicted*: drawn where the server will put them once it has run the commands
//     already sent, so movement responds instantly;
//   - remote players and enemies are *interpolated*: drawn slightly in the past, between two snapshots that
//     have both arrived, so they move smoothly despite 30 Hz snapshots, jitter and loss.

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "net/connection.hpp"
#include "net/link_conditioner.hpp"
#include "net/socket.hpp"
#include "proto/protocol.hpp"
#include "sim/map.hpp"

namespace ledang {

enum class ClientStatus { Idle, Connecting, Connected, Denied, TimedOut, Disconnected };

const char* status_name(ClientStatus s);

struct ClientStats {
    uint64_t snapshots = 0;
    uint64_t snapshot_bytes = 0;
    uint64_t decode_failures = 0;   // malformed bodies
    uint64_t missing_baselines = 0; // delta against a snapshot we no longer have (should stay 0)
    uint64_t predictions = 0;       // reconciliations run
    uint64_t corrections = 0;       // reconciliations where the prediction had been wrong
    double last_error_px = 0;
    double interp_delay_ticks = 0;
    double jitter_ticks = 0;
    uint64_t starved_frames = 0;    // frames where interpolation ran out of snapshots
};

/// Where to draw an interpolated entity, plus its newest known record.
struct EntityView {
    bool present = false;
    double x = 0, y = 0;    // pixels
    EntityRecord record;    // fields from the newer of the two snapshots
};

class ClientCore {
public:
    explicit ClientCore(const Map& map = Map::world());
    ~ClientCore();

    bool connect(const Address& server, double now);
    void disconnect(double now);

    /// Call every frame with the current input. Receives packets, runs the 60 Hz command loop, and advances
    /// the interpolation clock.
    void update(double now, uint8_t buttons);

    ClientStatus status() const { return status_; }
    int slot() const { return slot_; }
    bool in_game() const { return status_ == ClientStatus::Connected && slot_ >= 0 && have_snapshot_; }

    // ---- What to draw -----------------------------------------------------------------------------------
    /// The local player's state as predicted (or as last received when prediction is off).
    const PlayerState& local_player() const { return predicted_; }
    /// Where to draw the local player, including the smoothing offset that hides small corrections.
    double local_draw_x() const;
    double local_draw_y() const;
    const std::array<EntityView, kMaxEntities>& views() const { return views_; }
    const EntityRecord& world_record() const { return latest_.entities[kEntityWorld]; }
    uint32_t server_tick() const { return latest_.tick; }
    const Snapshot& latest_snapshot() const { return latest_; }
    double render_tick() const { return render_tick_; }
    std::vector<Event> take_events();

    // ---- Debug / demo switches ----------------------------------------------------------------------------
    void set_prediction(bool on) { prediction_ = on; }
    bool prediction() const { return prediction_; }
    void set_smoothing(bool on) { smoothing_ = on; }
    bool smoothing() const { return smoothing_; }
    void set_link_profile(const LinkProfile& p) { conditioner_.set_profile(p); }
    const LinkProfile& link_profile() const { return conditioner_.profile(); }

    const ConnectionStats& net_stats() const { return conn_.stats(); }
    const ClientStats& stats() const { return stats_; }
    uint32_t newest_cmd() const { return next_cmd_ - 1; }
    uint32_t acked_cmd() const { return latest_.last_cmd; }
    /// The server's state of the local player at the newest snapshot (for tests).
    PlayerState server_player() const;

private:
    void receive(double now);
    void handle_payload(std::span<const uint8_t> packet, double now);
    void handle_snapshot(std::span<const uint8_t> body, double now);
    void client_tick(uint8_t buttons, double now);
    void reconcile();
    void update_views();
    void send_handshake(double now);
    uint8_t world_abilities() const;

    const Map* map_;
    UdpSocket socket_;
    LinkConditioner conditioner_;
    Address server_;
    ClientStatus status_ = ClientStatus::Idle;
    uint64_t client_salt_ = 0;
    std::optional<uint64_t> combined_salt_;
    double connect_started_ = 0;
    double last_handshake_send_ = -1;
    Connection conn_;
    int slot_ = -1;

    // Command loop
    double next_tick_time_ = -1;
    uint32_t next_cmd_ = 1;
    static constexpr size_t kCmdHistory = 256;
    std::array<UserCmd, kCmdHistory> cmd_history_{};

    // Snapshots
    static constexpr size_t kRing = 32;
    std::array<Snapshot, kRing> ring_{};
    Snapshot latest_{};
    bool have_snapshot_ = false;
    bool reconcile_pending_ = false;

    // Prediction
    bool prediction_ = true;
    bool smoothing_ = true;
    PlayerState predicted_{};
    double smooth_x_ = 0, smooth_y_ = 0;  // pixels, decays toward 0

    // Interpolation clock
    double clock_tick_ = 0;        // estimate of the newest server tick, advanced in real time
    double last_frame_ = -1;
    double last_snapshot_arrival_ = -1;
    uint32_t last_snapshot_tick_ = 0;
    double interp_delay_ = 4;
    double render_tick_ = 0;
    std::array<EntityView, kMaxEntities> views_{};

    std::vector<Event> events_;
    ClientStats stats_;
};

}  // namespace ledang
