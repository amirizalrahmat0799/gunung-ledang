#pragma once
// The game: title menu, the 60 Hz simulation loop with hit-stop, effects driven by world events, saving and
// loading, the pause menu and the HUD.

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "client/render.hpp"
#include "sim/save.hpp"
#include "sim/world.hpp"

namespace ledang {

struct GameOptions {
    int warp = -1;            // development: start at this lantern with every ability (no saving)
    bool autoplay = false;    // a scripted player (demos, screenshots)
    bool start = false;       // skip the title screen
    bool debug = false;       // F1 overlay on from the start
    std::string save_path;    // default: the per-user data folder SDL picks for the game
    std::string screenshot;   // save the canvas here after `screenshot_seconds` and quit
    double screenshot_seconds = 3;
};

class Game {
public:
    Game(SDL_Renderer* renderer, GameOptions options);
    ~Game();

    void handle_event(const SDL_Event& e);
    /// Advances and draws one frame. Returns false when the game should exit.
    bool frame(double now, double dt);

private:
    enum class Screen { Title, Playing, Paused };
    struct Banner {
        std::string title, subtitle;
        SDL_Color color;
        double start, duration;
    };

    void start_world(const std::optional<SaveData>& save);
    void leave_to_title();
    void write_save();
    void simulate(double dt);
    void handle_world_events();
    Scene build_scene(double alpha) const;
    uint8_t read_input();
    uint8_t autoplay_input();
    void menu_move(int delta);
    void menu_select();
    std::vector<std::string> menu_items() const;
    void banner(const std::string& title, const std::string& subtitle, SDL_Color color, double duration = 4);

    void draw_title();
    void draw_hud();
    void draw_pause();
    void draw_debug();

    SDL_Renderer* sdl_;
    GameOptions opt_;
    Renderer renderer_;
    Effects fx_;
    Screen screen_ = Screen::Title;
    std::unique_ptr<World> world_;
    std::unique_ptr<World> prev_;  // the world one tick ago, for smooth drawing between ticks
    std::optional<SaveData> save_;
    std::string save_path_;
    double accumulator_ = 0;
    int hitstop_ = 0;              // ticks the world stays frozen after a hit
    float hurt_flash_ = 0;         // red screen tint after taking damage
    bool was_on_ground_ = true;
    fx last_vy_ = 0;
    uint8_t last_dash_ticks_ = 0;
    uint8_t held_from_menu_ = 0;
    int menu_index_ = 0;
    std::string last_area_;
    std::vector<Banner> banners_;
    SDL_Gamepad* gamepad_ = nullptr;
    double now_ = 0;
    double started_ = -1;
    double playing_since_ = -1;
    double victory_at_ = -1;
    bool moved_ = false;
    bool quit_ = false;
    bool screenshot_taken_ = false;
    bool debug_ = false;
    double fps_ = 60;
    uint32_t autoplay_tick_ = 0;
};

}  // namespace ledang
