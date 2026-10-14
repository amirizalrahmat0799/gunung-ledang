#pragma once
// Simulates a bad network on the sending side: latency, jitter, packet loss and duplication.
// Used by the demo hotkeys and by the soak tests; disabled it costs nothing.

#include <cstdint>
#include <functional>
#include <queue>
#include <random>
#include <vector>

#include "net/socket.hpp"

namespace ledang {

struct LinkProfile {
    double latency_ms = 0;   // one-way delay added to every packet
    double jitter_ms = 0;    // +/- random variation
    double loss = 0;         // probability a packet is dropped, 0..1
    double duplicate = 0;    // probability a packet is delivered twice

    bool active() const { return latency_ms > 0 || jitter_ms > 0 || loss > 0 || duplicate > 0; }
};

class LinkConditioner {
public:
    explicit LinkConditioner(uint32_t seed = 1234) : rng_(seed) {}

    void set_profile(const LinkProfile& p) { profile_ = p; }
    const LinkProfile& profile() const { return profile_; }

    /// Sends now, or schedules the packet for later according to the profile.
    void send(UdpSocket& socket, const Address& to, std::span<const uint8_t> data, double now);
    /// Sends every scheduled packet whose time has come.
    void flush(UdpSocket& socket, double now);

    uint64_t dropped() const { return dropped_; }

private:
    struct Pending {
        double due;
        uint64_t order;
        Address to;
        std::vector<uint8_t> data;
        bool operator>(const Pending& o) const { return due != o.due ? due > o.due : order > o.order; }
    };

    LinkProfile profile_;
    std::mt19937 rng_;
    std::priority_queue<Pending, std::vector<Pending>, std::greater<>> queue_;
    uint64_t order_ = 0;
    uint64_t dropped_ = 0;
};

}  // namespace ledang
