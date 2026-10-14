#pragma once
// The game client: title screen, hosting (a server on a background thread) or joining, input, events and HUD.

#include <SDL3/SDL.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "bot/bot_brain.hpp"
#include "client/client_core.hpp"
#include "client/render.hpp"
#include "server/server.hpp"

namespace ledang {

struct GameOptions {
    bool host = false;
    std::string connect;       // join this address at startup
    uint16_t port = kDefaultPort;
    int bots = 0;              // headless co-op partners that join a hosted game
    int warp = -1;             // development: start at this lantern with every ability
    bool autoplay = false;     // the local player is driven by a bot (demos, screenshots)
    bool overlay = false;
    bool ghosts = false;
    int net_profile = 0;
    std::string screenshot;    // save the canvas here after `screenshot_seconds` and quit
    double screenshot_seconds = 3;
};

/// A server running on its own thread inside the game, for "Host a game".
class ListenServer {
public:
    ~ListenServer();
    bool start(uint16_t port, int warp);
    void stop();
    void set_link_profile(const LinkProfile& p);
    uint16_t port() const { return port_; }

private:
    void run();
    std::unique_ptr<Server> server_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::optional<LinkProfile> pending_profile_;
    uint16_t port_ = 0;
};

class Game {
public:
    Game(SDL_Renderer* renderer, GameOptions options);
    ~Game();

    void handle_event(const SDL_Event& e);
    /// Advances and draws one frame. Returns false when the game should exit.
    bool frame(double now, double dt);

private:
    enum class Screen { Title, JoinPrompt, Playing };
    struct Banner {
        std::string title, subtitle;
        SDL_Color color;
        double start, duration;
    };

    void host_game();
    void join_game(const std::string& address);
    void leave_game(const std::string& reason);
    uint8_t read_input();
    void apply_net_profile();
    void handle_game_events(double now);
    void banner(const std::string& title, const std::string& subtitle, SDL_Color color, double now, double duration = 4);

    void draw_title(double now);
    void draw_join_prompt(double now);
    void draw_hud(double now);
    void draw_overlay();

    SDL_Renderer* sdl_;
    GameOptions opt_;
    Renderer renderer_;
    Effects fx_;
    Screen screen_ = Screen::Title;
    std::unique_ptr<ClientCore> client_;
    std::unique_ptr<ListenServer> listen_;
    std::vector<std::unique_ptr<ClientCore>> bots_;
    std::vector<BotBrain> bot_brains_;
    std::vector<uint8_t> bot_buttons_;
    BotBrain auto_brain_{4242};
    std::string address_text_ = "127.0.0.1";
    std::string status_message_;
    std::vector<Banner> banners_;
    SDL_Gamepad* gamepad_ = nullptr;
    double started_ = -1;
    double joined_at_ = -1;
    double victory_at_ = -1;
    double last_think_ = 0;
    uint8_t buttons_ = 0;
    bool moved_ = false;
    bool quit_ = false;
    bool screenshot_taken_ = false;
};

}  // namespace ledang
