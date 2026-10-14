#include "net/link_conditioner.hpp"

#include <algorithm>

namespace ledang {

void LinkConditioner::send(UdpSocket& socket, const Address& to, std::span<const uint8_t> data, double now) {
    if (!profile_.active()) {
        socket.send(to, data);
        return;
    }
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    if (unit(rng_) < profile_.loss) {
        ++dropped_;
        return;
    }
    const int copies = unit(rng_) < profile_.duplicate ? 2 : 1;
    for (int i = 0; i < copies; ++i) {
        const double jitter = (unit(rng_) * 2.0 - 1.0) * profile_.jitter_ms;
        const double delay = std::max(0.0, profile_.latency_ms + jitter) / 1000.0;
        queue_.push(Pending{now + delay, order_++, to, std::vector<uint8_t>(data.begin(), data.end())});
    }
}

void LinkConditioner::flush(UdpSocket& socket, double now) {
    while (!queue_.empty() && queue_.top().due <= now) {
        socket.send(queue_.top().to, queue_.top().data);
        queue_.pop();
    }
}

}  // namespace ledang
