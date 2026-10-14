#include <array>
#include <algorithm>
#include <random>

#include "core/clock.hpp"
#include "net/connection.hpp"
#include "net/link_conditioner.hpp"
#include "net/socket.hpp"
#include "test.hpp"

using namespace ledang;

namespace {

// A lossy, reordering, duplicating pipe between two Connections, in virtual time.
struct Pipe {
    struct InFlight {
        double due;
        std::vector<uint8_t> data;
    };
    std::mt19937 rng{42};
    double latency = 0.05, jitter = 0.02, loss = 0.2, dup = 0.05;
    std::vector<InFlight> flight;

    void send(std::vector<uint8_t> p, double now) {
        std::uniform_real_distribution<double> u(0, 1);
        if (u(rng) < loss) return;
        const int copies = u(rng) < dup ? 2 : 1;
        for (int i = 0; i < copies; ++i) flight.push_back({now + latency + (u(rng) * 2 - 1) * jitter, p});
    }
    std::vector<std::vector<uint8_t>> deliver(double now) {
        std::vector<std::vector<uint8_t>> out;
        std::stable_sort(flight.begin(), flight.end(), [](auto& a, auto& b) { return a.due < b.due; });
        while (!flight.empty() && flight.front().due <= now) {
            out.push_back(std::move(flight.front().data));
            flight.erase(flight.begin());
        }
        return out;
    }
};

std::vector<uint8_t> message(uint32_t i) {
    std::vector<uint8_t> m(1 + i % 40);
    for (size_t k = 0; k < m.size(); ++k) m[k] = static_cast<uint8_t>(i + k);
    m[0] = static_cast<uint8_t>(i);
    return m;
}

}  // namespace

TEST(connection_reliable_in_order_over_bad_link) {
    Connection a, b;
    Pipe ab, ba;
    a.start(0);
    b.start(0);
    constexpr uint32_t kMessages = 500;
    uint32_t sent = 0, received = 0;
    bool in_order = true;
    std::vector<uint32_t> acked_tags;
    std::vector<uint32_t> sent_tags;

    for (int step = 0; step < 60 * 40 && received < kMessages; ++step) {
        const double now = step / 60.0;
        if (sent < kMessages && step % 2 == 0) {
            a.send_reliable(message(sent));
            a.send_reliable(message(sent + 1));
            sent += 2;
        }
        const uint8_t body[3] = {9, 9, 9};
        ab.send(a.build_packet(body, static_cast<uint32_t>(step), now), now);
        sent_tags.push_back(static_cast<uint32_t>(step));
        ba.send(b.build_packet({}, 0, now), now);

        for (auto& p : ab.deliver(now)) {
            std::span<const uint8_t> unreliable;
            if (b.process_packet(p, now, unreliable)) CHECK_EQ(unreliable.size(), size_t{3});
        }
        for (auto& p : ba.deliver(now)) {
            std::span<const uint8_t> unreliable;
            a.process_packet(p, now, unreliable);
        }
        while (auto m = b.next_message()) {
            if (*m != message(received)) in_order = false;
            ++received;
        }
        for (uint32_t t : a.take_acked_tags()) acked_tags.push_back(t);
        a.update_stats(now);
    }
    CHECK_EQ(received, kMessages);
    CHECK(in_order);
    CHECK(b.stats().duplicates_dropped > 0);
    CHECK(a.stats().reliable_resends > 0);
    // Roughly 80% of packets arrive, and every one that arrived should eventually be acked.
    CHECK(acked_tags.size() > sent_tags.size() * 6 / 10);
    std::sort(acked_tags.begin(), acked_tags.end());
    CHECK(std::adjacent_find(acked_tags.begin(), acked_tags.end()) == acked_tags.end());  // each tag once
    // RTT: 100 ms of latency plus up to a frame of waiting for the reply.
    CHECK(a.stats().rtt_ms > 70 && a.stats().rtt_ms < 160);
    CHECK(a.stats().loss > 0.1 && a.stats().loss < 0.4);
}

TEST(connection_rejects_garbage_and_duplicates) {
    Connection a, b;
    a.start(0);
    b.start(0);
    const auto p = a.build_packet({}, 1, 0);
    std::span<const uint8_t> u;
    CHECK(b.process_packet(p, 0, u));
    CHECK(!b.process_packet(p, 0, u));  // duplicate
    std::vector<uint8_t> truncated(p.begin(), p.begin() + 7);
    CHECK(!b.process_packet(truncated, 0, u));
    std::vector<uint8_t> junk(64, 0xAB);
    CHECK(!b.process_packet(junk, 0, u));
    CHECK(!read_packet_prefix(junk).has_value());

    // A message count that runs past the end must not crash or deliver anything.
    a.send_reliable({1, 2, 3});
    auto q = a.build_packet({}, 2, 0);
    q.resize(q.size() - 2);
    CHECK(!b.process_packet(q, 0, u));
    CHECK(!b.next_message().has_value());
}

TEST(sockets_and_link_conditioner) {
    UdpSocket s1, s2;
    CHECK(s1.open(0));
    CHECK(s2.open(0));
    const Address to{0x7F000001, s2.local_port()};
    LinkConditioner lc(5);
    lc.set_profile(LinkProfile{50, 0, 0, 0});
    const uint8_t data[4] = {1, 2, 3, 4};
    lc.send(s1, to, data, 0.0);
    lc.flush(s1, 0.02);
    sleep_until_seconds(now_seconds() + 0.02);
    Address from;
    std::array<uint8_t, 64> buf{};
    CHECK_EQ(s2.receive(from, buf), size_t{0});  // still "in flight"
    lc.flush(s1, 0.06);
    size_t n = 0;
    for (int i = 0; i < 100 && n == 0; ++i) {
        n = s2.receive(from, buf);
        if (n == 0) sleep_until_seconds(now_seconds() + 0.002);
    }
    CHECK_EQ(n, size_t{4});
    CHECK_EQ(from.port, s1.local_port());

    lc.set_profile(LinkProfile{0, 0, 1.0, 0});  // drop everything
    lc.send(s1, to, data, 1.0);
    CHECK_EQ(lc.dropped(), uint64_t{1});

    const auto parsed = Address::parse("127.0.0.1:1234", 1);
    CHECK(parsed.has_value() && parsed->port == 1234 && parsed->ip == 0x7F000001);
    CHECK(Address::parse("localhost", 27015)->port == 27015);
}
