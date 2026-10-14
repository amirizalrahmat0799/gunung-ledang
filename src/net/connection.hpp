#pragma once
// Reliability over UDP, in the style of most action games (and Valve's own netchannel):
//   - every packet carries a sequence number plus an ack of the newest packet received and a 32-bit
//     bitfield acking the 32 before it, so acks survive the loss of individual packets;
//   - acked packets give round-trip time and loss estimates, and tell the game which snapshots arrived
//     (used for delta compression);
//   - reliable messages ride along in ordinary packets and are re-sent until a packet carrying them is acked,
//     then delivered to the receiver in order, exactly once.

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace ledang {

constexpr uint32_t kProtocolId = 0x4C45'4401;  // "LED" + protocol version 1
constexpr size_t kMaxPacketSize = 1200;          // stays under typical MTUs

enum class PacketType : uint8_t {
    ConnectRequest = 1,
    Challenge = 2,
    ChallengeResponse = 3,
    Denied = 4,
    Payload = 5,
    Disconnect = 6,
};

/// Writes the 5-byte prefix every packet starts with (protocol id, type).
void write_packet_prefix(std::vector<uint8_t>& out, PacketType type);
/// Validates the prefix; returns the packet type or nullopt for foreign/garbage packets.
std::optional<PacketType> read_packet_prefix(std::span<const uint8_t> packet);
constexpr size_t kPrefixSize = 5;

struct ConnectionStats {
    double rtt_ms = 0;
    double loss = 0;            // fraction of our packets the peer never acked (recent window)
    double kbps_out = 0;
    double kbps_in = 0;
    uint64_t packets_sent = 0;
    uint64_t packets_received = 0;
    uint64_t duplicates_dropped = 0;
    uint64_t reliable_resends = 0;
};

class Connection {
public:
    /// Queues a reliable message (max 1000 bytes); delivered in order exactly once.
    void send_reliable(std::vector<uint8_t> message);

    /// Builds a Payload packet: header, any reliable messages due (new or due for resend), then the
    /// unreliable body. The tag is reported back via take_acked_tags() if the peer acks this packet.
    std::vector<uint8_t> build_packet(std::span<const uint8_t> unreliable, uint32_t tag, double now);

    /// Processes a received Payload packet (including the prefix). Returns false if malformed or a duplicate.
    /// On success, `unreliable` points at the unreliable body inside `packet`.
    bool process_packet(std::span<const uint8_t> packet, double now, std::span<const uint8_t>& unreliable);

    /// Next reliable message ready for the game, in order.
    std::optional<std::vector<uint8_t>> next_message();
    /// Tags of our packets newly acked by the peer since the last call.
    std::vector<uint32_t> take_acked_tags();

    /// Marks the connection as live from `now` (timeouts and rate windows count from here).
    void start(double now) {
        last_receive_ = now;
        window_start_ = now;
    }
    double last_receive_time() const { return last_receive_; }
    const ConnectionStats& stats() const { return stats_; }
    void update_stats(double now);
    size_t pending_reliable() const { return outgoing_.size(); }

private:
    struct SentPacket {
        uint16_t seq = 0;
        bool valid = false;
        bool acked = false;
        double sent_at = 0;
        uint32_t tag = 0;
        std::vector<uint16_t> message_ids;
    };
    struct OutgoingMessage {
        uint16_t id;
        std::vector<uint8_t> data;
        double last_sent = -1;
    };
    static constexpr size_t kWindow = 1024;

    void on_acked(uint16_t seq, double now);

    uint16_t next_seq_ = 0;
    std::array<SentPacket, kWindow> sent_{};

    bool received_any_ = false;
    uint16_t remote_seq_ = 0;     // newest packet received from the peer
    uint32_t remote_bits_ = 0;    // which of the 32 before it were received
    std::array<int32_t, kWindow> seen_{};  // seq + 1 of the packet stored in each slot (0 = none)

    uint16_t next_message_id_ = 0;
    std::deque<OutgoingMessage> outgoing_;
    uint16_t expected_message_id_ = 0;
    std::map<uint16_t, std::vector<uint8_t>> early_messages_;
    std::deque<std::vector<uint8_t>> ready_messages_;
    std::vector<uint32_t> acked_tags_;

    ConnectionStats stats_;
    double last_receive_ = 0;
    double window_start_ = 0;
    uint64_t window_bytes_out_ = 0;
    uint64_t window_bytes_in_ = 0;
};

}  // namespace ledang
