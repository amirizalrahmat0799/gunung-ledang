// gunung_ledang: the game client.
//   gunung_ledang                      title screen (host or join)
//   gunung_ledang --host [--bots 2]    host a co-op game straight away (bots fill the party for testing)
//   gunung_ledang --connect host:port  join a game
// Development: --warp N (start at lantern N with every ability), --overlay, --ghosts, --net 0..3,
//              --autoplay, --screenshot file.bmp [--seconds 3]

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>

#include "client/game.hpp"
#include "core/args.hpp"
#include "core/clock.hpp"

int main(int argc, char** argv) {
    using namespace ledang;
    const Args args(argc, argv);
    GameOptions opt;
    opt.host = args.flag("--host");
    opt.connect = args.str("--connect", "");
    opt.port = static_cast<uint16_t>(args.num("--port", kDefaultPort));
    opt.bots = static_cast<int>(args.num("--bots", 0));
    opt.warp = args.flag("--warp") ? static_cast<int>(args.num("--warp", 0)) : -1;
    opt.autoplay = args.flag("--autoplay");
    opt.overlay = args.flag("--overlay");
    opt.ghosts = args.flag("--ghosts");
    opt.net_profile = std::clamp(static_cast<int>(args.num("--net", 0)), 0, 3);
    opt.screenshot = args.str("--screenshot", "");
    opt.screenshot_seconds = args.num("--seconds", 3);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    enable_precise_timers();
    init_sockets();

    SDL_Window* window = SDL_CreateWindow("Gunung Ledang", kViewW * 3, kViewH * 3, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        std::fprintf(stderr, "could not create a window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    {
        Game game(renderer, opt);
        double last = now_seconds();
        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) game.handle_event(e);
            const double now = now_seconds();
            running = game.frame(now, std::min(now - last, 0.1));
            last = now;
            if (!opt.screenshot.empty()) sleep_until_seconds(now + 1.0 / 120);  // offscreen: no vsync to pace us
        }
    }
    shutdown_sockets();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
