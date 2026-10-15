#include "client/render.hpp"

#include <algorithm>
#include <cmath>

#include "sim/enemy.hpp"

namespace ledang {

namespace {

SDL_Color rgb(int r, int g, int b, int a = 255) {
    return SDL_Color{static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), static_cast<Uint8>(a)};
}

uint32_t hash2(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float frand(uint32_t h) { return static_cast<float>(h & 0xFFFF) / 65535.0f; }

void set_color(SDL_Renderer* r, SDL_Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }

double fx_px(int32_t v) { return static_cast<double>(v) / kFx; }

}  // namespace

// ---- Effects --------------------------------------------------------------------------------------------------

void Effects::update(float dt) {
    for (Particle& p : particles) {
        p.vy += p.gravity * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vx *= 1.0f - 1.5f * dt;
        p.life -= dt;
    }
    std::erase_if(particles, [](const Particle& p) { return p.life <= 0; });
    shake = std::max(0.0f, shake - dt * 12.0f);
    squash *= std::exp(-dt * 14.0f);  // springs back to normal in about 0.2 s
}

void Effects::burst(float x, float y, int count, SDL_Color color, float speed, float life, bool glow, float gravity) {
    static uint32_t seed = 1;
    for (int i = 0; i < count; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float a = frand(seed) * 6.2831853f;
        seed = seed * 1664525u + 1013904223u;
        const float s = speed * (0.3f + 0.7f * frand(seed));
        seed = seed * 1664525u + 1013904223u;
        const float l = life * (0.6f + 0.4f * frand(seed));
        particles.push_back(Particle{x, y, std::cos(a) * s, std::sin(a) * s, l, l, color, gravity, glow,
                                     frand(seed >> 3) < 0.3f ? 2.0f : 1.0f});
    }
}

// ---- Renderer -------------------------------------------------------------------------------------------------

Renderer::Renderer(SDL_Renderer* r, const Map& map) : r_(r), map_(&map), sprites_(std::make_unique<Sprites>(r)) {
    canvas_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kViewW, kViewH);
    SDL_SetTextureScaleMode(canvas_, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(canvas_, SDL_BLENDMODE_NONE);
    lightmap_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kViewW, kViewH);
    SDL_SetTextureBlendMode(lightmap_, SDL_BLENDMODE_MOD);
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);

    // Fireflies (kelip-kelip) drift through the forbidden forest.
    for (int i = 0; i < 70; ++i) {
        const uint32_t h = hash2(i, 77);
        fireflies_.push_back(30 * kTile + frand(h) * 60 * kTile);
        fireflies_.push_back(14 * kTile + frand(h >> 7) * 20 * kTile);
    }
}

Renderer::~Renderer() {
    sprites_.reset();
    SDL_DestroyTexture(canvas_);
    SDL_DestroyTexture(lightmap_);
}

void Renderer::begin() {
    SDL_SetRenderTarget(r_, canvas_);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
}

void Renderer::present() {
    SDL_SetRenderTarget(r_, nullptr);
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r_, &w, &h);
    const int scale = std::max(1, std::min(w / kViewW, h / kViewH));
    const SDL_FRect dst{static_cast<float>((w - kViewW * scale) / 2), static_cast<float>((h - kViewH * scale) / 2),
                        static_cast<float>(kViewW * scale), static_cast<float>(kViewH * scale)};
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    SDL_RenderTexture(r_, canvas_, nullptr, &dst);
    SDL_RenderPresent(r_);
}

bool Renderer::save_canvas(const char* path) {
    SDL_SetRenderTarget(r_, canvas_);
    SDL_Surface* s = SDL_RenderReadPixels(r_, nullptr);
    if (!s) return false;
    const bool ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok;
}

void Renderer::snap_camera(double x, double y) {
    cam_x_ = std::clamp(x - kViewW / 2.0, 0.0, static_cast<double>(map_->width() * kTile - kViewW));
    cam_y_ = std::clamp(y - kViewH / 2.0, 0.0, static_cast<double>(map_->height() * kTile - kViewH));
}

void Renderer::set_camera_target(double x, double y, double dt) {
    const double old_x = cam_x_, old_y = cam_y_;
    snap_camera(x, y);
    const double k = std::min(1.0, dt * 7.0);
    cam_x_ = old_x + (cam_x_ - old_x) * k;
    cam_y_ = old_y + (cam_y_ - old_y) * k;
}

void Renderer::fill(float x, float y, float w, float h, SDL_Color c) {
    set_color(r_, c);
    const SDL_FRect rc{x, y, w, h};
    SDL_RenderFillRect(r_, &rc);
}

void Renderer::sprite(SDL_Texture* t, float x, float y, bool flip, float alpha) {
    float w = 0, h = 0;
    SDL_GetTextureSize(t, &w, &h);
    const SDL_FRect dst{std::floor(x), std::floor(y), w, h};
    SDL_SetTextureAlphaModFloat(t, alpha);
    SDL_RenderTextureRotated(r_, t, nullptr, &dst, 0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureAlphaModFloat(t, 1.0f);
}

void Renderer::text(float x, float y, const std::string& s, SDL_Color c, float scale, bool shadow) {
    SDL_SetRenderScale(r_, scale, scale);
    if (shadow) {
        SDL_SetRenderDrawColor(r_, 0, 0, 0, static_cast<Uint8>(c.a * 3 / 4));
        SDL_RenderDebugText(r_, x / scale + 1.0f / scale, y / scale + 1.0f / scale, s.c_str());
    }
    set_color(r_, c);
    SDL_RenderDebugText(r_, x / scale, y / scale, s.c_str());
    SDL_SetRenderScale(r_, 1, 1);
}

void Renderer::text_centered(float y, const std::string& s, SDL_Color c, float scale) {
    const float w = static_cast<float>(s.size()) * 8.0f * scale;
    text(std::floor((kViewW - w) / 2), y, s, c, scale);
}

// ---- World ----------------------------------------------------------------------------------------------------

void Renderer::draw_background(double time) {
    // Sky: deep violet to a sick red glow at the horizon.
    for (int y = 0; y < kViewH; ++y) {
        const float t = static_cast<float>(y) / kViewH;
        SDL_SetRenderDrawColor(r_, static_cast<Uint8>(8 + 40 * t * t), static_cast<Uint8>(6 + 14 * t),
                               static_cast<Uint8>(22 + 26 * t), 255);
        SDL_RenderLine(r_, 0, static_cast<float>(y), kViewW, static_cast<float>(y));
    }
    // Stars
    for (int i = 0; i < 90; ++i) {
        const uint32_t h = hash2(i, 3);
        const float sx = std::fmod(frand(h) * 1400.0f - static_cast<float>(cam_x_) * 0.02f + 1400.0f, 1400.0f) - 500;
        const float sy = frand(h >> 8) * 140.0f - static_cast<float>(cam_y_) * 0.02f;
        if (sx < 0 || sx >= kViewW || sy < 0) continue;
        const float tw = 0.5f + 0.5f * std::sin(static_cast<float>(time) * (1.0f + frand(h >> 4) * 3) + static_cast<float>(i));
        SDL_SetRenderDrawColor(r_, 200, 190, 220, static_cast<Uint8>(80 + 150 * tw));
        SDL_RenderPoint(r_, std::floor(sx), std::floor(sy));
    }
    // Moon, faintly red
    const float mx = 300 - static_cast<float>(cam_x_) * 0.03f, my = 44 - static_cast<float>(cam_y_) * 0.03f;
    for (int dy = -14; dy <= 14; ++dy) {
        const float half = std::sqrt(static_cast<float>(14 * 14 - dy * dy));
        SDL_SetRenderDrawColor(r_, 226, 196, 170, 255);
        SDL_RenderLine(r_, std::floor(mx - half), my + static_cast<float>(dy), std::floor(mx + half), my + static_cast<float>(dy));
    }
    fill(mx - 6, my - 4, 3, 3, rgb(196, 160, 140));
    fill(mx + 3, my + 2, 4, 3, rgb(196, 160, 140));
    fill(mx - 2, my + 7, 2, 2, rgb(200, 166, 146));

    const double world_bottom = map_->height() * kTile - kViewH;
    // Far range: Gunung Ledang itself, its peak toward the summit side of the world.
    {
        const double horizon = 130 + (world_bottom - cam_y_) * 0.10;
        set_color(r_, rgb(34, 26, 52));
        for (int sx = 0; sx < kViewW; ++sx) {
            const double wx = sx + cam_x_ * 0.12;
            const double peak = 95 * std::exp(-std::pow((wx - 330) / 110.0, 2));
            const double h = 22 + peak + 9 * std::sin(wx * 0.021) + 5 * std::sin(wx * 0.057 + 1.3);
            SDL_RenderLine(r_, static_cast<float>(sx), static_cast<float>(horizon - h), static_cast<float>(sx), kViewH);
        }
    }
    // Mist over the far range
    for (int i = 0; i < 3; ++i) {
        const float y = 120.0f + static_cast<float>(i) * 22 + 4 * std::sin(static_cast<float>(time) * 0.3f + static_cast<float>(i)) + static_cast<float>((world_bottom - cam_y_) * 0.12);
        fill(0, y, kViewW, 10, rgb(140, 120, 170, 18));
    }
    // Near range: jungle canopy silhouettes
    {
        const double horizon = 175 + (world_bottom - cam_y_) * 0.3;
        set_color(r_, rgb(18, 16, 30));
        for (int sx = 0; sx < kViewW; ++sx) {
            const double wx = sx + cam_x_ * 0.32;
            const int crown = static_cast<int>(wx / 9);
            const double bump = 6 * std::sqrt(std::max(0.0, 1.0 - std::pow((std::fmod(wx, 9.0) - 4.5) / 4.5, 2)));
            const double h = 28 + 10 * std::sin(wx * 0.04) + 7 * std::sin(wx * 0.11) + bump + static_cast<double>(hash2(crown, 9) % 7);
            SDL_RenderLine(r_, static_cast<float>(sx), static_cast<float>(horizon - h), static_cast<float>(sx), kViewH);
        }
    }
}

void Renderer::draw_tiles() {
    const int tx0 = std::max(0, ox_ / kTile), tx1 = std::min(map_->width() - 1, (ox_ + kViewW) / kTile);
    const int ty0 = std::max(0, oy_ / kTile), ty1 = std::min(map_->height() - 1, (oy_ + kViewH) / kTile);
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const Tile t = map_->at(tx, ty);
            const float x = static_cast<float>(tx * kTile - ox_), y = static_cast<float>(ty * kTile - oy_);
            const uint32_t h = hash2(tx, ty);
            if (t == Tile::Solid) {
                const bool top = ty > 0 && !map_->solid(tx, ty - 1);
                SDL_Texture* tex = top ? sprites_->rock_top(static_cast<int>(h & 3)) : sprites_->rock(static_cast<int>(h & 3));
                const bool deep = map_->solid(tx - 1, ty) && map_->solid(tx + 1, ty) && map_->solid(tx, ty - 1) &&
                                  map_->solid(tx, ty + 1) && map_->solid(tx, ty - 2);
                if (deep) SDL_SetTextureColorMod(tex, 120, 115, 135);
                sprite(tex, x, y, (h >> 4) & 1);
                SDL_SetTextureColorMod(tex, 255, 255, 255);
            } else if (t == Tile::Platform) {
                sprite(sprites_->bamboo(), x, y);
            } else if (t == Tile::Spikes) {
                sprite(sprites_->stakes(), x, y, (h >> 3) & 1);
            } else if (ty + 1 < map_->height() && map_->solid(tx, ty + 1) && (h % 5) < 2) {
                // grass tufts and bones on the ground
                const SDL_Color c = (h % 11 == 0) ? rgb(170, 160, 140) : rgb(40, 74, 46);
                fill(x + static_cast<float>(h % 13), y + 14, 1, 2, c);
                fill(x + static_cast<float>(h % 13) + 2, y + 13, 1, 3, c);
            }
        }
    }
}

void Renderer::draw_objects(const Scene* scene, double time) {
    (void)time;
    const uint8_t abilities = scene ? scene->abilities : 0;
    const uint16_t lit = scene ? scene->lanterns_lit : 0;
    int lantern = 0;
    for (const Marker& m : map_->markers()) {
        const float x = static_cast<float>(m.tx * kTile - ox_), y = static_cast<float>(m.ty * kTile - oy_);
        if (x < -32 || x > kViewW + 32 || y < -32 || y > kViewH + 32) {
            if (m.kind == MarkerKind::Lantern) ++lantern;
            continue;
        }
        switch (m.kind) {
            case MarkerKind::Lantern: {
                sprite(sprites_->lantern((lit >> lantern) & 1), x + 4, y + 2);
                ++lantern;
                break;
            }
            case MarkerKind::ShrineDash:
            case MarkerKind::ShrineDoubleJump:
            case MarkerKind::ShrineWallJump: {
                const uint8_t bit = m.kind == MarkerKind::ShrineDash         ? AbilityDash
                                    : m.kind == MarkerKind::ShrineDoubleJump ? AbilityDoubleJump
                                                                             : AbilityWallJump;
                sprite(sprites_->shrine((abilities & bit) != 0), x + 2, y + 4);
                break;
            }
            case MarkerKind::Goal:
                sprite(sprites_->puteri_shrine(), x - 3, y - 1);
                break;
            default:
                break;
        }
    }
}

void Renderer::draw_enemy(const EnemyView& v, double time, bool glow_pass) {
    const EnemyState& e = v.state;
    if (!e.alive()) return;
    const float x = std::floor(static_cast<float>(v.x) - static_cast<float>(ox_));
    const float y = std::floor(static_cast<float>(v.y) - static_cast<float>(oy_));
    const bool left = (e.flags & EnemyFacingLeft) != 0;
    // Struck spirits flash white for a moment.
    const bool flash = e.hurt_ticks > enemy_tuning::kHurtTicks - 5;
    const bool hurt = e.hurt_ticks > 0;
    const float t = static_cast<float>(time);

    if (e.kind == EnemyKind::Penanggal) {
        if (glow_pass) {
            fill(x + 3, y + 4, 2, 2, rgb(255, 60, 40));
            fill(x + 7, y + 4, 2, 2, rgb(255, 60, 40));
            if (flash) sprite(sprites_->penanggal_white(), x - 1, y - 1, left);
            return;
        }
        // Entrails dangle and sway behind the head.
        const float vx = static_cast<float>(fx_px(e.vx));
        float px = x + 6, py = y + 12;
        for (int i = 0; i < 12; ++i) {
            const float nx = x + 6 + std::sin(t * 5 + static_cast<float>(i) * 0.7f) * static_cast<float>(i) * 0.3f - vx * static_cast<float>(i) * 0.9f;
            const float ny = y + 12 + static_cast<float>(i) * 1.6f;
            SDL_SetRenderDrawColor(r_, i % 3 == 0 ? 190 : 120, i % 3 == 0 ? 70 : 14, i % 3 == 0 ? 80 : 22, 255);
            SDL_RenderLine(r_, px, py, nx, ny);
            SDL_RenderLine(r_, px + 1, py, nx + 1, ny);
            px = nx;
            py = ny;
        }
        // Wild hair streaming behind
        SDL_SetRenderDrawColor(r_, 16, 10, 18, 255);
        for (int i = 0; i < 5; ++i) {
            const float sx = x + 2 + static_cast<float>(i) * 2;
            SDL_RenderLine(r_, sx, y + 2, sx - vx * 4 + std::sin(t * 3 + static_cast<float>(i)) * 2, y + 14 + static_cast<float>(i % 3) * 2);
        }
        SDL_Texture* tex = sprites_->penanggal();
        if (hurt && !flash) SDL_SetTextureColorMod(tex, 255, 120, 120);
        sprite(tex, x - 1, y - 1, left);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    } else {
        SDL_Texture* tex = sprites_->pocong();
        const bool airborne = !(e.flags & EnemyOnGround);
        float w = 12, h = 18;
        SDL_GetTextureSize(tex, &w, &h);
        const float stretch = airborne ? 2.0f : 0.0f;
        const SDL_FRect dst{x - 1, y - 2 - stretch, w, h + stretch};
        const double angle = airborne ? (left ? -6.0 : 6.0) : 0.0;
        const SDL_FlipMode flip = left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        if (glow_pass) {
            fill(x + 3, y + 3 - stretch, 1, 1, rgb(255, 70, 50));
            fill(x + 6, y + 3 - stretch, 1, 1, rgb(255, 70, 50));
            if (flash) SDL_RenderTextureRotated(r_, sprites_->pocong_white(), nullptr, &dst, angle, nullptr, flip);
            return;
        }
        if (hurt && !flash) SDL_SetTextureColorMod(tex, 255, 120, 120);
        SDL_RenderTextureRotated(r_, tex, nullptr, &dst, angle, nullptr, flip);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    }
}

void Renderer::draw_player(const PlayerState& p, double wx, double wy, float squash) {
    if (p.has(FlagDead)) return;
    const float x = std::floor(static_cast<float>(wx) - static_cast<float>(ox_));
    const float y = std::floor(static_cast<float>(wy) - static_cast<float>(oy_));
    const bool left = p.has(FlagFacingLeft);

    PlayerFrame f = FrameIdle;
    if (p.hurt_ticks > 45) {
        f = FrameHurt;
    } else if (!p.has(FlagOnGround)) {
        if (p.has(FlagWallLeft) || p.has(FlagWallRight)) {
            f = FrameWall;
        } else {
            f = p.vy < 0 ? FrameJump : FrameFall;
        }
    } else if (std::abs(p.vx) > 64) {
        f = (static_cast<int>(wx / 7) % 2) ? FrameRun1 : FrameRun2;
    }
    SDL_Texture* tex = sprites_->player(0, f);
    const bool wall_flip = f == FrameWall ? p.has(FlagWallLeft) : left;

    if (p.dash_ticks > 0) {
        SDL_SetTextureColorMod(tex, 120, 200, 255);
        for (int k = 3; k >= 1; --k) {
            sprite(tex, x - 1 - static_cast<float>(p.facing() * k * 7), y - 2, wall_flip, 0.12f * static_cast<float>(4 - k));
        }
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    }
    const bool blink = p.hurt_ticks > 0 && (p.hurt_ticks / 4) % 2 == 1;
    // Squash and stretch: wider and shorter on landing (squash > 0), taller and thinner on take-off (< 0).
    // The sprite stays anchored at its feet.
    const float sx = 1.0f + 0.25f * squash, sy = 1.0f - 0.25f * squash;
    const float w = 12 * sx, h = 16 * sy;
    const SDL_FRect dst{std::floor(x + 5 - w / 2), std::floor(y + 14 - h), std::round(w), std::round(h)};
    SDL_SetTextureAlphaModFloat(tex, blink ? 0.35f : 1.0f);
    SDL_RenderTextureRotated(r_, tex, nullptr, &dst, 0, nullptr, wall_flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureAlphaModFloat(tex, 1.0f);
}

void Renderer::light(double wx, double wy, float radius, SDL_Color c) {
    const SDL_FRect dst{static_cast<float>(wx) - static_cast<float>(ox_) - radius,
                        static_cast<float>(wy) - static_cast<float>(oy_) - radius, radius * 2, radius * 2};
    SDL_SetTextureColorMod(sprites_->light(), c.r, c.g, c.b);
    SDL_RenderTexture(r_, sprites_->light(), nullptr, &dst);
}

void Renderer::draw_lights(const Scene* scene, const Effects& effects, double time) {
    SDL_SetRenderTarget(r_, lightmap_);
    // Ambient darkness: slightly brighter near the top where the moon lights the sky.
    SDL_SetRenderDrawColor(r_, 50, 44, 80, 255);
    SDL_RenderClear(r_);
    const float t = static_cast<float>(time);
    light(300 - cam_x_ * 0.03 + cam_x_, 44 - cam_y_ * 0.03 + cam_y_, 150, rgb(70, 52, 70));

    const uint8_t abilities = scene ? scene->abilities : 0;
    const uint16_t lit = scene ? scene->lanterns_lit : 0;
    int lantern = 0;
    for (const Marker& m : map_->markers()) {
        const double cx = m.tx * kTile + 8.0, cy = m.ty * kTile + 6.0;
        const float flicker = std::sin(t * 9 + static_cast<float>(m.tx)) * 3 + std::sin(t * 23 + static_cast<float>(m.ty)) * 2;
        switch (m.kind) {
            case MarkerKind::Lantern:
                if ((lit >> lantern) & 1) {
                    light(cx, cy, 110 + flicker, rgb(255, 170, 90));
                } else {
                    light(cx, cy, 22, rgb(90, 80, 120));
                }
                ++lantern;
                break;
            case MarkerKind::ShrineDash:
            case MarkerKind::ShrineDoubleJump:
            case MarkerKind::ShrineWallJump: {
                const uint8_t bit = m.kind == MarkerKind::ShrineDash         ? AbilityDash
                                    : m.kind == MarkerKind::ShrineDoubleJump ? AbilityDoubleJump
                                                                             : AbilityWallJump;
                const bool claimed = abilities & bit;
                light(cx, cy + 4, claimed ? 24.0f : 52 + 8 * std::sin(t * 2), claimed ? rgb(80, 80, 100) : rgb(120, 210, 255));
                break;
            }
            case MarkerKind::Goal:
                light(cx + 8, cy, 80 + 6 * std::sin(t * 1.5f), rgb(255, 180, 220));
                break;
            default:
                break;
        }
    }
    if (scene) {
        for (const EnemyView& v : scene->enemies) {
            if (v.state.alive() && v.state.kind == EnemyKind::Penanggal) light(v.x + 6, v.y + 6, 36, rgb(170, 30, 30));
        }
        const bool dead = scene->player.has(FlagDead);
        const float flicker = std::sin(t * 11) * 2.5f;
        light(scene->px + 5, scene->py + 4, dead ? 30.0f : 100 + flicker, dead ? rgb(90, 120, 160) : rgb(255, 220, 180));
        if (!dead) light(scene->px + 5, scene->py + 4, 34, rgb(140, 110, 80));  // hot core: the face stays readable
    }
    for (const Particle& p : effects.particles) {
        if (p.glow && p.size > 1) light(p.x, p.y, 10 * (p.life / p.max_life), p.color);
    }
    SDL_SetRenderTarget(r_, canvas_);
    SDL_RenderTexture(r_, lightmap_, nullptr, nullptr);
}

void Renderer::draw_glow(const Scene* scene, const Effects& effects, double time) {
    const float t = static_cast<float>(time);
    // Fireflies
    for (size_t i = 0; i + 1 < fireflies_.size(); i += 2) {
        const float fxp = fireflies_[i] + 14 * std::sin(t * 0.4f + static_cast<float>(i)) - static_cast<float>(ox_);
        const float fyp = fireflies_[i + 1] + 8 * std::sin(t * 0.7f + static_cast<float>(i) * 1.7f) - static_cast<float>(oy_);
        if (fxp < 0 || fxp >= kViewW || fyp < 0 || fyp >= kViewH) continue;
        const float blink = std::sin(t * 2.3f + static_cast<float>(i) * 0.9f);
        if (blink < 0.2f) continue;
        fill(std::floor(fxp), std::floor(fyp), 1, 1, rgb(200, 255, 120, static_cast<int>(255 * blink)));
    }
    if (scene) {
        for (const EnemyView& v : scene->enemies) draw_enemy(v, time, true);
        const PlayerState& p = scene->player;
        const double px = scene->px - ox_;
        const double py = scene->py - oy_;
        if (p.has(FlagDead)) {
            // a wandering spirit while the lantern calls you back
            fill(static_cast<float>(px) + 4, static_cast<float>(py) + 4 + 3 * std::sin(t * 3), 3, 3, rgb(160, 200, 255, 200));
        } else if (p.attack_ticks >= tuning::kAttackActiveLast - 2) {
            // The keris slash: bright, drawn over the darkness.
            const float a = std::clamp(static_cast<float>(p.attack_ticks - tuning::kAttackActiveLast + 3) / 9.0f, 0.0f, 1.0f);
            SDL_Texture* slash = sprites_->keris_slash();
            SDL_SetTextureAlphaModFloat(slash, a);
            const bool left = p.has(FlagFacingLeft);
            SDL_FRect dst{};
            double angle = 0;
            SDL_FlipMode flip = SDL_FLIP_NONE;
            switch (p.attack_dir) {
                case AttackDir::Side:
                    dst = {static_cast<float>(left ? px - 14 : px + 2), static_cast<float>(py - 3), 22, 20};
                    flip = left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                    break;
                case AttackDir::Up:
                    dst = {static_cast<float>(px - 6), static_cast<float>(py - 17), 22, 20};
                    angle = -90;
                    break;
                case AttackDir::Down:
                    dst = {static_cast<float>(px - 6), static_cast<float>(py + 10), 22, 20};
                    angle = 90;
                    break;
            }
            dst.x = std::floor(dst.x);
            dst.y = std::floor(dst.y);
            SDL_RenderTextureRotated(r_, slash, nullptr, &dst, angle, nullptr, flip);
            SDL_SetTextureAlphaModFloat(slash, 1.0f);
        }
    }
    for (const Particle& p : effects.particles) {
        if (!p.glow) continue;
        const float a = std::clamp(p.life / p.max_life, 0.0f, 1.0f);
        SDL_Color c = p.color;
        c.a = static_cast<Uint8>(255 * a);
        fill(std::floor(p.x - static_cast<float>(ox_)), std::floor(p.y - static_cast<float>(oy_)), p.size, p.size, c);
    }
}

void Renderer::draw_world(const Scene* scene, const Effects& effects, double time) {
    const float sx = effects.shake > 0 ? std::sin(static_cast<float>(time) * 90) * effects.shake : 0;
    const float sy = effects.shake > 0 ? std::cos(static_cast<float>(time) * 77) * effects.shake : 0;
    ox_ = static_cast<int>(std::floor(cam_x_ + sx));
    oy_ = static_cast<int>(std::floor(cam_y_ + sy));

    draw_background(time);
    draw_tiles();
    draw_objects(scene, time);

    if (scene) {
        for (const EnemyView& v : scene->enemies) draw_enemy(v, time, false);
        draw_player(scene->player, scene->px, scene->py, effects.squash);
        for (const Particle& p : effects.particles) {
            if (p.glow) continue;
            const float a = std::clamp(p.life / p.max_life, 0.0f, 1.0f);
            SDL_Color c = p.color;
            c.a = static_cast<Uint8>(c.a * a);
            fill(std::floor(p.x - static_cast<float>(ox_)), std::floor(p.y - static_cast<float>(oy_)), p.size, p.size, c);
        }
    }

    draw_lights(scene, effects, time);
    draw_glow(scene, effects, time);
}

}  // namespace ledang
