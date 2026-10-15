#pragma once
// Draws the game into a 384x216 canvas that is scaled up by whole pixels to the window:
// parallax night sky and mountain, tiles, spirits and players, then a lightmap that leaves only lantern and
// torch light visible, then glowing things (eyes, fireflies, sparks) and the HUD.

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

#include "client/sprites.hpp"
#include "sim/enemy.hpp"
#include "sim/map.hpp"
#include "sim/player.hpp"

namespace ledang {

constexpr int kViewW = 384;
constexpr int kViewH = 216;

struct Particle {
    float x, y, vx, vy;
    float life, max_life;
    SDL_Color color;
    float gravity = 0;
    bool glow = false;   // drawn after lighting, so it shines in the dark
    float size = 1;
};

/// Short-lived visual state owned by the game: particles, screen shake.
struct Effects {
    std::vector<Particle> particles;
    float shake = 0;
    float squash = 0;  // player squash (+) / stretch (-), springs back to 0
    void update(float dt);
    void burst(float x, float y, int count, SDL_Color color, float speed, float life, bool glow, float gravity = 0);
};

/// One spirit as it should be drawn this frame (position interpolated between simulation ticks).
struct EnemyView {
    EnemyState state;
    double x, y;  // pixels
};

/// Everything the renderer needs from the game world for one frame.
struct Scene {
    uint8_t abilities = 0;
    uint16_t lanterns_lit = 0;
    PlayerState player;
    double px = 0, py = 0;  // player position in pixels, interpolated
    std::vector<EnemyView> enemies;
};

class Renderer {
public:
    Renderer(SDL_Renderer* r, const Map& map);
    ~Renderer();

    SDL_Renderer* sdl() const { return r_; }
    const Sprites& sprites() const { return *sprites_; }

    /// Starts a frame: everything is drawn into the low-resolution canvas.
    void begin();
    /// Scales the canvas to the window and presents.
    void present();

    void set_camera_target(double x, double y, double dt);
    void snap_camera(double x, double y);
    double camera_x() const { return cam_x_; }
    double camera_y() const { return cam_y_; }

    /// The world (or, with no scene, just the empty mountain for the title screen).
    void draw_world(const Scene* scene, const Effects& effects, double time);

    // Text and simple UI (positions in canvas pixels).
    void text(float x, float y, const std::string& s, SDL_Color c, float scale = 1, bool shadow = true);
    void text_centered(float y, const std::string& s, SDL_Color c, float scale = 1);
    void fill(float x, float y, float w, float h, SDL_Color c);
    void sprite(SDL_Texture* t, float x, float y, bool flip = false, float alpha = 1);

    /// Saves the current canvas as a BMP (for screenshots / docs).
    bool save_canvas(const char* path);

private:
    void draw_background(double time);
    void draw_tiles();
    void draw_objects(const Scene* scene, double time);
    void draw_enemy(const EnemyView& v, double time, bool glow_pass);
    void draw_player(const PlayerState& p, double x, double y, float squash);
    void draw_lights(const Scene* scene, const Effects& effects, double time);
    void draw_glow(const Scene* scene, const Effects& effects, double time);
    void light(double wx, double wy, float radius, SDL_Color c);

    SDL_Renderer* r_;
    const Map* map_;
    std::unique_ptr<Sprites> sprites_;
    SDL_Texture* canvas_ = nullptr;
    SDL_Texture* lightmap_ = nullptr;
    double cam_x_ = 0, cam_y_ = 0;  // top-left of the view in world pixels
    int ox_ = 0, oy_ = 0;            // integer camera offset for this frame (includes shake)
    std::vector<float> fireflies_;
};

}  // namespace ledang
