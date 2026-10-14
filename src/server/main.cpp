// ledang_server: dedicated server for Gunung Ledang.
//   ledang_server [--port 27015] [--lag ms] [--jitter ms] [--loss 0.05] [--quiet]
// --lag/--jitter/--loss simulate a bad network on everything the server sends.

#include <atomic>
#include <csignal>
#include <cstdio>

#include "core/args.hpp"
#include "core/clock.hpp"
#include "server/server.hpp"

namespace {
std::atomic<bool> g_running{true};
void on_signal(int) { g_running = false; }
}  // namespace

int main(int argc, char** argv) {
    using namespace ledang;
    const Args args(argc, argv);
    if (args.flag("--help") || args.flag("-h")) {
        std::printf("usage: ledang_server [--port %u] [--lag ms] [--jitter ms] [--loss fraction] [--quiet]\n",
                    kDefaultPort);
        return 0;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    enable_precise_timers();
    if (!init_sockets()) {
        std::fprintf(stderr, "could not initialise sockets\n");
        return 1;
    }

    Server server;
    server.set_log(!args.flag("--quiet"));
    server.set_link_profile(LinkProfile{args.num("--lag", 0), args.num("--jitter", 0), args.num("--loss", 0), 0});
    if (args.flag("--warp")) server.debug_unlock(AbilityDash | AbilityDoubleJump | AbilityWallJump, static_cast<int>(args.num("--warp", 0)));
    if (!server.start(static_cast<uint16_t>(args.num("--port", kDefaultPort)))) {
        std::fprintf(stderr, "could not open the UDP port (is another server running?)\n");
        return 1;
    }
    std::printf("Gunung Ledang server running at %d Hz. Ctrl+C to stop.\n", kTickRate);

    double next_report = now_seconds() + 10;
    while (g_running) {
        const double now = now_seconds();
        server.update(now);
        if (now >= next_report && !args.flag("--quiet")) {
            next_report = now + 10;
            const ServerStats& s = server.stats();
            std::printf("[server] tick %u, %d player(s), %.0f%% delta snapshots, avg %.0f bytes, cmd gaps %llu\n",
                        server.world().tick(), server.client_count(),
                        s.snapshots_sent ? 100.0 * static_cast<double>(s.delta_snapshots) / static_cast<double>(s.snapshots_sent) : 0.0,
                        s.snapshots_sent ? static_cast<double>(s.snapshot_bytes) / static_cast<double>(s.snapshots_sent) : 0.0,
                        static_cast<unsigned long long>(s.cmd_gaps));
        }
        sleep_until_seconds(now + 0.001);
    }
    shutdown_sockets();
    return 0;
}
