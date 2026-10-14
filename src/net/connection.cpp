#include "net/connection.hpp"

#include <algorithm>
#include <cstring>

#include "core/sequence.hpp"

namespace ledang {

namespace {

void put_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint16_t get_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

constexpr size_t kHeaderSize = kPrefixSize + 2 + 2 + 4 + 1;  // prefix, seq, ack, ack_bits, message count
constexpr size_t kMaxReliableBytes = 900;

}  // namespace

void write_packet_prefix(std::vector<uint8_t>& out, PacketType type) {
    put_u32(out, kProtocolId);
    out.push_back(static_cast<uint8_t>(type));
}

std::optional<PacketType> read_packet_prefix(std::span<const uint8_t> packet) {
    if (packet.size() < kPrefixSize || get_u32(packet.data()) != kProtocolId) return std::nullopt;
    const uint8_t t = packet[4];
    if (t < static_cast<uint8_t>(PacketType::ConnectRequest) || t > static_cast<uint8_t>(PacketType::Disconnect)) {
        return std::nullopt;
    }
    return static_cast<PacketType>(t);
}

void Connection::send_reliable(std::vector<uint8_t> message) {
    outgoing_.push_back(OutgoingMessage{next_message_id_++, std::move(message), -1});
}

std::vector<uint8_t> Connection::build_packet(std::span<const uint8_t> unreliable, uint32_t tag, double now) {
    const uint16_t seq = next_seq_++;
    SentPacket& record = sent_[seq % kWindow];
    record = SentPacket{seq, true, false, now, tag, {}};

    std::vector<uint8_t> out;
    out.reserve(kHeaderSize + unreliable.size() + 64);
    write_packet_prefix(out, PacketType::Payload);
    put_u16(out, seq);
    put_u16(out, remote_seq_);
    put_u32(out, received_any_ ? remote_bits_ : 0);
    const size_t count_pos = out.size();
    out.push_back(0);

    // Resend a message once a round trip (plus margin) has passed without an ack.
    const double resend_after = std::max(0.1, stats_.rtt_ms / 1000.0 * 1.25);
    uint8_t count = 0;
    size_t budget = kMaxReliableBytes;
    for (auto& m : outgoing_) {
        if (count == 255 || m.data.size() + 4 > budget) break;
        if (m.last_sent >= 0 && now - m.last_sent < resend_after) continue;
        if (m.last_sent >= 0) ++stats_.reliable_resends;
        put_u16(out, m.id);
        put_u16(out, static_cast<uint16_t>(m.data.size()));
        out.insert(out.end(), m.data.begin(), m.data.end());
        m.last_sent = now;
        record.message_ids.push_back(m.id);
        budget -= m.data.size() + 4;
        ++count;
    }
    out[count_pos] = count;
    out.insert(out.end(), unreliable.begin(), unreliable.end());

    ++stats_.packets_sent;
    window_bytes_out_ += out.size() + 28;  // + IPv4/UDP headers
    return out;
}

bool Connection::process_packet(std::span<const uint8_t> packet, double now, std::span<const uint8_t>& unreliable) {
    if (packet.size() < kHeaderSize) return false;
    const uint8_t* p = packet.data() + kPrefixSize;
    const uint16_t seq = get_u16(p);
    const uint16_t ack = get_u16(p + 2);
    const uint32_t ack_bits = get_u32(p + 4);
    const uint8_t count = p[8];

    // Duplicate or far too old?
    if (received_any_ && seq_diff(seq, remote_seq_) <= -static_cast<int>(kWindow) + 64) return false;
    if (seen_[seq % kWindow] == static_cast<int32_t>(seq) + 1) {
        ++stats_.duplicates_dropped;
        return false;
    }

    // Parse reliable messages before committing anything, so a malformed packet changes no state.
    size_t pos = kHeaderSize;
    struct Msg { uint16_t id; size_t off; uint16_t len; };
    std::vector<Msg> msgs;
    for (int i = 0; i < count; ++i) {
        if (pos + 4 > packet.size()) return false;
        const uint16_t id = get_u16(packet.data() + pos);
        const uint16_t len = get_u16(packet.data() + pos + 2);
        pos += 4;
        if (pos + len > packet.size()) return false;
        msgs.push_back({id, pos, len});
        pos += len;
    }

    seen_[seq % kWindow] = static_cast<int32_t>(seq) + 1;
    if (!received_any_) {
        received_any_ = true;
        remote_seq_ = seq;
        remote_bits_ = 0;
    } else if (seq_newer(seq, remote_seq_)) {
        const int shift = seq_diff(seq, remote_seq_);
        remote_bits_ = shift >= 32 ? 0 : (remote_bits_ << shift);
        if (shift <= 32) remote_bits_ |= 1u << (shift - 1);
        remote_seq_ = seq;
    } else {
        const int back = seq_diff(remote_seq_, seq);
        if (back >= 1 && back <= 32) remote_bits_ |= 1u << (back - 1);
    }

    // Acks: the newest packet the peer saw, and the 32 before it.
    on_acked(ack, now);
    for (int i = 0; i < 32; ++i) {
        if (ack_bits & (1u << i)) on_acked(static_cast<uint16_t>(ack - 1 - i), now);
    }

    for (const Msg& m : msgs) {
        const int ahead = seq_diff(m.id, expected_message_id_);
        if (ahead < 0) continue;  // already delivered
        if (ahead >= 4096) continue;
        early_messages_.emplace(m.id, std::vector<uint8_t>(packet.begin() + m.off, packet.begin() + m.off + m.len));
    }
    for (auto it = early_messages_.find(expected_message_id_); it != early_messages_.end();
         it = early_messages_.find(expected_message_id_)) {
        ready_messages_.push_back(std::move(it->second));
        early_messages_.erase(it);
        ++expected_message_id_;
    }

    unreliable = packet.subspan(pos);
    last_receive_ = now;
    ++stats_.packets_received;
    window_bytes_in_ += packet.size() + 28;
    return true;
}

void Connection::on_acked(uint16_t seq, double now) {
    SentPacket& r = sent_[seq % kWindow];
    if (!r.valid || r.seq != seq || r.acked) return;
    r.acked = true;
    const double sample_ms = (now - r.sent_at) * 1000.0;
    stats_.rtt_ms = stats_.rtt_ms == 0 ? sample_ms : stats_.rtt_ms + (sample_ms - stats_.rtt_ms) * 0.1;
    acked_tags_.push_back(r.tag);
    for (uint16_t id : r.message_ids) {
        auto it = std::find_if(outgoing_.begin(), outgoing_.end(), [id](const OutgoingMessage& m) { return m.id == id; });
        if (it != outgoing_.end()) outgoing_.erase(it);
    }
}

std::optional<std::vector<uint8_t>> Connection::next_message() {
    if (ready_messages_.empty()) return std::nullopt;
    auto m = std::move(ready_messages_.front());
    ready_messages_.pop_front();
    return m;
}

std::vector<uint32_t> Connection::take_acked_tags() {
    std::vector<uint32_t> out;
    out.swap(acked_tags_);
    return out;
}

void Connection::update_stats(double now) {
    const double elapsed = now - window_start_;
    if (elapsed < 1.0) return;
    stats_.kbps_out = static_cast<double>(window_bytes_out_) * 8.0 / 1000.0 / elapsed;
    stats_.kbps_in = static_cast<double>(window_bytes_in_) * 8.0 / 1000.0 / elapsed;
    window_bytes_out_ = window_bytes_in_ = 0;
    window_start_ = now;

    // Loss: of the packets sent between 2 s and 0.5 s + RTT ago, how many were never acked?
    const double newest = now - 0.5 - stats_.rtt_ms / 1000.0;
    const double oldest = now - 2.0;
    int sent = 0;
    int lost = 0;
    for (const SentPacket& r : sent_) {
        if (!r.valid || r.sent_at < oldest || r.sent_at > newest) continue;
        ++sent;
        if (!r.acked) ++lost;
    }
    if (sent > 0) stats_.loss = static_cast<double>(lost) / sent;
}

}  // namespace ledang
