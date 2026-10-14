// ledang_bot: headless players for load testing a server.
//   ledang_bot [--connect 127.0.0.1:27015] [--bots 3] [--seconds 60] [--lag ms] [--jitter ms] [--loss 0.05]

#include <cstdio>
#include <memory>
#include <vector>

#include "bot/bot_brain.hpp"
#include "core/args.hpp"
#include "core/clock.hpp"
#include "server/server.hpp"

int main(int argc, char** argv) {
    using namespace ledang;
    const Args args(argc, argv);
    if (args.flag("--help") || args.flag("-h")) {
        std::printf("usage: ledang_bot [--connect host:port] [--bots 3] [--seconds 60] [--lag ms] [--jitter ms] [--loss f]\n");
        return 0;
    }
    enable_precise_timers();
    init_sockets();
    const auto server = Address::parse(args.str("--connect", "127.0.0.1"), kDefaultPort);
    if (!server) {
        std::fprintf(stderr, "can't resolve server address\n");
        return 1;
    }
    const int count = std::clamp(static_cast<int>(args.num("--bots", 3)), 1, kMaxPlayers);
    const double seconds = args.num("--seconds", 60);
    const LinkProfile link{args.num("--lag", 0), args.num("--jitter", 0), args.num("--loss", 0), 0};

    std::vector<std::unique_ptr<ClientCore>> clients;
    std::vector<BotBrain> brains;
    const double start = now_seconds();
    for (int i = 0; i < count; ++i) {
        clients.push_back(std::make_unique<ClientCore>());
        clients.back()->set_link_profile(link);
        clients.back()->connect(*server, start);
        brains.emplace_back(static_cast<uint32_t>(1000 + i));
    }

    double next_think = start;
    std::vector<uint8_t> buttons(static_cast<size_t>(count), 0);
    double next_report = start + 5;
    while (now_seconds() - start < seconds) {
        const double now = now_seconds();
        if (now >= next_think) {
            next_think += 1.0 / kTickRate;
            for (int i = 0; i < count; ++i) buttons[static_cast<size_t>(i)] = brains[static_cast<size_t>(i)].think(*clients[static_cast<size_t>(i)]);
        }
        for (int i = 0; i < count; ++i) clients[static_cast<size_t>(i)]->update(now, buttons[static_cast<size_t>(i)]);
        if (now >= next_report) {
            next_report = now + 5;
            for (int i = 0; i < count; ++i) {
                const ClientCore& c = *clients[static_cast<size_t>(i)];
                std::printf("bot %d: %-11s ping %3.0f ms loss %4.1f%% in %5.1f kbps, corrections %llu/%llu, interp %.1f ticks\n",
                            i + 1, status_name(c.status()), c.net_stats().rtt_ms, c.net_stats().loss * 100,
                            c.net_stats().kbps_in, static_cast<unsigned long long>(c.stats().corrections),
                            static_cast<unsigned long long>(c.stats().predictions), c.stats().interp_delay_ticks);
            }
        }
        sleep_until_seconds(now + 0.002);
    }
    for (auto& c : clients) c->disconnect(now_seconds());
    shutdown_sockets();
    return 0;
}
