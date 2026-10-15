// gunung_ledang: the game.
//   gunung_ledang                 title screen
// Development options:
//   --start                       skip the title (continue the save, or a new game)
//   --warp N                      start at lantern N with every ability (doesn't touch your save)
//   --save path                   use a different save file
//   --autoplay                    a scripted player; --screenshot file.bmp [--seconds 3] saves a frame and quits
//   --debug                       F1 overlay on

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>

#include "client/game.hpp"
#include "core/args.hpp"
#include "core/clock.hpp"

int main(int argc, char** argv) {
    using namespace ledang;
    const Args args(argc, argv);
    GameOptions opt;
    opt.start = args.flag("--start");
    opt.warp = args.flag("--warp") ? static_cast<int>(args.num("--warp", 0)) : -1;
    opt.autoplay = args.flag("--autoplay");
    opt.debug = args.flag("--debug");
    opt.save_path = args.str("--save", "");
    opt.screenshot = args.str("--screenshot", "");
    opt.screenshot_seconds = args.num("--seconds", 3);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    enable_precise_timers();

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
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
