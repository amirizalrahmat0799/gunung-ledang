#include "client/game.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ledang {

namespace {

constexpr double kTickSeconds = 1.0 / kTickRate;

// Hit-stop: the world freezes for a few ticks when something lands, so hits feel heavy.
constexpr int kHitstopHit = 3;
constexpr int kHitstopKill = 6;
constexpr int kHitstopHurt = 9;
constexpr int kHitstopDeath = 16;

SDL_Color rgb(int r, int g, int b, int a = 255) {
    return SDL_Color{static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), static_cast<Uint8>(a)};
}

const SDL_Color kGold = rgb(236, 196, 80);
const SDL_Color kPale = rgb(220, 210, 200);
const SDL_Color kBlood = rgb(210, 40, 44);
const SDL_Color kDim = rgb(150, 140, 160);
const SDL_Color kDust = rgb(150, 140, 130, 200);

/// Region names along the climb, shown when you enter them.
const char* area_name(double x, double y) {
    const int tx = static_cast<int>(x) / kTile, ty = static_cast<int>(y) / kTile;
    if (ty < 9) return "PUNCAK LEDANG";
    if (tx >= 83) return "AIR TERJUN";
    if (tx >= 58 && ty < 25) return "DATARAN BERHANTU";
    if (tx >= 38) return "HUTAN LARANGAN";
    if (tx >= 28) return "JURANG BULUH";
    return "KAKI GUNUNG";
}

const char* area_subtitle(const std::string& name) {
    if (name == "PUNCAK LEDANG") return "The summit. Her shrine waits.";
    if (name == "AIR TERJUN") return "The waterfall shaft";
    if (name == "DATARAN BERHANTU") return "The haunted plateau";
    if (name == "HUTAN LARANGAN") return "The forbidden forest";
    if (name == "JURANG BULUH") return "Ravine of bamboo stakes";
    return "The foot of the mountain";
}

std::string format_time(uint32_t ticks) {
    const uint32_t s = ticks / kTickRate;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}

std::string default_save_path() {
    char* dir = SDL_GetPrefPath("Amirizal", "GunungLedang");
    std::string path = dir ? std::string(dir) + "save.txt" : "save.txt";
    SDL_free(dir);
    return path;
}

}  // namespace

Game::Game(SDL_Renderer* renderer, GameOptions options)
    : sdl_(renderer), opt_(std::move(options)), renderer_(renderer, Map::world()) {
    debug_ = opt_.debug;
    save_path_ = opt_.save_path.empty() ? default_save_path() : opt_.save_path;
    if (opt_.warp < 0) {
        std::ifstream in(save_path_, std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            save_ = save_from_text(ss.str());
        }
    }
    if (opt_.start || opt_.warp >= 0) start_world(opt_.warp >= 0 ? std::nullopt : save_);
}

Game::~Game() {
    if (world_ && screen_ != Screen::Title) write_save();
    if (gamepad_) SDL_CloseGamepad(gamepad_);
}

// ---- Flow -----------------------------------------------------------------------------------------------------

void Game::start_world(const std::optional<SaveData>& save) {
    world_ = std::make_unique<World>(Map::world());
    if (save) world_->restore(*save);
    if (opt_.warp >= 0) world_->debug_unlock(AbilityDash | AbilityDoubleJump | AbilityWallJump, opt_.warp);
    prev_ = std::make_unique<World>(*world_);
    accumulator_ = 0;
    hitstop_ = 0;
    fx_ = Effects{};
    banners_.clear();
    last_area_.clear();
    was_on_ground_ = true;
    victory_at_ = world_->victory() ? now_ : -1;
    playing_since_ = now_;
    moved_ = false;
    held_from_menu_ = opt_.autoplay ? 0 : 0xFF;
    screen_ = Screen::Playing;
    const PlayerState& p = world_->player();
    renderer_.snap_camera(to_px(p.x) + 5, to_px(p.y) - 10);
}

void Game::leave_to_title() {
    write_save();
    world_.reset();
    prev_.reset();
    screen_ = Screen::Title;
    menu_index_ = 0;
}

void Game::write_save() {
    if (!world_ || opt_.warp >= 0) return;  // warped (development) runs never touch the real save
    save_ = world_->save_data();
    const std::string tmp = save_path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << save_to_text(*save_);
        if (!out) return;
    }
    // Replace the old save in one step, so a crash mid-write can't leave a broken file.
    std::error_code ec;
    std::filesystem::rename(tmp, save_path_, ec);
}

void Game::banner(const std::string& title, const std::string& subtitle, SDL_Color color, double duration) {
    banners_.push_back(Banner{title, subtitle, color, now_, duration});
}

// ---- Input ----------------------------------------------------------------------------------------------------

std::vector<std::string> Game::menu_items() const {
    if (screen_ == Screen::Paused) return {"RESUME", "QUIT TO TITLE"};
    std::vector<std::string> items;
    if (save_) items.push_back("CONTINUE");
    items.push_back("NEW GAME");
    items.push_back("QUIT");
    return items;
}

void Game::menu_move(int delta) {
    const int n = static_cast<int>(menu_items().size());
    menu_index_ = (menu_index_ + delta + n) % n;
}

void Game::menu_select() {
    const std::vector<std::string> items = menu_items();
    const std::string& item = items[static_cast<size_t>(std::clamp(menu_index_, 0, static_cast<int>(items.size()) - 1))];
    if (item == "CONTINUE") start_world(save_);
    if (item == "NEW GAME") start_world(std::nullopt);
    if (item == "QUIT") quit_ = true;
    if (item == "RESUME") screen_ = Screen::Playing;
    if (item == "QUIT TO TITLE") leave_to_title();
}

void Game::handle_event(const SDL_Event& e) {
    enum class Nav { None, Up, Down, Select, Back, Pause };
    Nav nav = Nav::None;
    switch (e.type) {
        case SDL_EVENT_QUIT:
            quit_ = true;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad_) gamepad_ = SDL_OpenGamepad(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad_ && SDL_GetGamepadID(gamepad_) == e.gdevice.which) {
                SDL_CloseGamepad(gamepad_);
                gamepad_ = nullptr;
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP: nav = Nav::Up; break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: nav = Nav::Down; break;
                case SDL_GAMEPAD_BUTTON_SOUTH: nav = Nav::Select; break;
                case SDL_GAMEPAD_BUTTON_EAST: nav = Nav::Back; break;
                case SDL_GAMEPAD_BUTTON_START: nav = Nav::Pause; break;
                default: break;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.repeat) break;
            switch (e.key.key) {
                case SDLK_UP:
                case SDLK_W: nav = Nav::Up; break;
                case SDLK_DOWN:
                case SDLK_S: nav = Nav::Down; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                case SDLK_Z: nav = Nav::Select; break;
                case SDLK_ESCAPE: nav = Nav::Pause; break;
                case SDLK_F1: debug_ = !debug_; break;
                default: break;
            }
            break;
        default:
            break;
    }
    if (nav == Nav::None) return;

    if (screen_ == Screen::Playing) {
        if (nav == Nav::Pause) {
            if (victory_at_ >= 0 && now_ - victory_at_ > 2) {
                leave_to_title();
            } else {
                screen_ = Screen::Paused;
                menu_index_ = 0;
            }
        } else if (nav == Nav::Select && victory_at_ >= 0 && now_ - victory_at_ > 4) {
            leave_to_title();
        }
        return;
    }
    if (nav == Nav::Up) menu_move(-1);
    if (nav == Nav::Down) menu_move(1);
    if (nav == Nav::Select) menu_select();
    if (nav == Nav::Back || nav == Nav::Pause) {
        if (screen_ == Screen::Paused) screen_ = Screen::Playing;
        else if (nav == Nav::Pause) quit_ = true;
    }
}

uint8_t Game::read_input() {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    uint8_t b = 0;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) b |= BtnLeft;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) b |= BtnRight;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) b |= BtnUp;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) b |= BtnDown;
    if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_K]) b |= BtnJump;
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_L]) b |= BtnDash;
    if (keys[SDL_SCANCODE_J] || keys[SDL_SCANCODE_X]) b |= BtnAttack;
    if (gamepad_) {
        const Sint16 lx = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX);
        const Sint16 ly = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY);
        if (lx < -12000 || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) b |= BtnLeft;
        if (lx > 12000 || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) b |= BtnRight;
        if (ly < -16000 || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_UP)) b |= BtnUp;
        if (ly > 16000 || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) b |= BtnDown;
        if (SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_SOUTH)) b |= BtnJump;
        if (SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_WEST)) b |= BtnAttack;
        if (SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_EAST) ||
            SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
            b |= BtnDash;
        }
    }
    if (b) moved_ = true;
    return b;
}

/// A simple scripted player for demos and screenshots: runs uphill, jumps, and swings at nearby spirits.
uint8_t Game::autoplay_input() {
    const uint32_t t = ++autoplay_tick_;
    const PlayerState& p = world_->player();
    uint8_t b = BtnRight;
    if (t % 50 < 18) b |= BtnJump;
    if ((p.has(FlagWallLeft) || p.has(FlagWallRight)) && t % 12 < 6) b |= BtnJump;
    if (t % 97 == 0) b |= BtnDash;
    for (const EnemyState& e : world_->enemies()) {
        if (!e.alive()) continue;
        const fx dx = e.x - p.x, dy = e.y - p.y;
        if (std::abs(dx) < px(28) && std::abs(dy) < px(20) && t % 14 == 0) {
            b = static_cast<uint8_t>((b & ~(BtnLeft | BtnRight)) | (dx < 0 ? BtnLeft : BtnRight) | BtnAttack);
        }
        if (std::abs(dx) < px(10) && dy > px(8) && dy < px(30) && !p.has(FlagOnGround) && t % 14 == 7) {
            b |= BtnDown | BtnAttack;  // pogo
        }
    }
    return b;
}

// ---- Simulation -----------------------------------------------------------------------------------------------

void Game::simulate(double dt) {
    accumulator_ = std::min(accumulator_ + dt, 0.25);  // after a long stall, don't try to catch up for seconds
    while (accumulator_ >= kTickSeconds) {
        accumulator_ -= kTickSeconds;
        *prev_ = *world_;
        if (hitstop_ > 0) {
            --hitstop_;
            continue;
        }
        uint8_t buttons = opt_.autoplay ? autoplay_input() : read_input();
        // Buttons still held from the menu (the Space that picked "New game") count only once released.
        held_from_menu_ &= buttons;
        buttons = static_cast<uint8_t>(buttons & ~held_from_menu_);
        world_->update(buttons);

        // Movement effects: dust and squash when landing, jumping and dashing.
        const PlayerState& p = world_->player();
        const bool on_ground = p.has(FlagOnGround);
        const float feet_x = static_cast<float>(to_px(p.x) + 5), feet_y = static_cast<float>(to_px(p.y) + 14);
        if (on_ground && !was_on_ground_ && last_vy_ > 600) {
            fx_.squash = std::min(1.0f, static_cast<float>(last_vy_) / 1400.0f);
            fx_.burst(feet_x, feet_y, 6 + static_cast<int>(last_vy_ / 250), kDust, 40, 0.4f, false, 60);
        }
        if (!on_ground && was_on_ground_ && p.vy < 0) {
            fx_.squash = -0.55f;
            fx_.burst(feet_x, feet_y, 5, kDust, 30, 0.3f, false, 40);
        }
        if (p.dash_ticks > last_dash_ticks_) {
            fx_.burst(feet_x - static_cast<float>(p.facing() * 6), feet_y - 6, 10, rgb(150, 200, 255, 220), 50, 0.35f, true);
            fx_.shake = std::max(fx_.shake, 1.0f);
        }
        was_on_ground_ = on_ground;
        last_vy_ = p.vy;
        last_dash_ticks_ = p.dash_ticks;

        handle_world_events();
    }
}

void Game::handle_world_events() {
    for (const Event& e : world_->take_events()) {
        const float x = e.x, y = e.y;
        switch (e.type) {
            case EventType::AbilityGained:
                fx_.burst(x + 8, y + 8, 50, kGold, 70, 1.6f, true, -20);
                hitstop_ = std::max(hitstop_, 10);
                if (e.a == AbilityDash) banner("KERAMAT ANGIN", "The wind shrine grants DASH  (Shift / C)", kGold, 5);
                if (e.a == AbilityDoubleJump) banner("KERAMAT BANGAU", "The crane shrine grants DOUBLE JUMP", kGold, 5);
                if (e.a == AbilityWallJump) banner("KERAMAT HARIMAU", "The tiger shrine grants WALL JUMP", kGold, 5);
                write_save();
                break;
            case EventType::LanternLit:
                fx_.burst(x + 8, y + 4, 30, rgb(255, 170, 80), 50, 1.2f, true, -30);
                banner("PELITA MENYALA", "A lantern is lit. You will return here.", rgb(255, 180, 100), 3);
                break;
            case EventType::Rested:
                fx_.burst(x + 8, y + 4, 10, rgb(255, 200, 120), 25, 0.8f, true, -30);
                write_save();
                break;
            case EventType::PlayerHurt:
                fx_.burst(x + 5, y + 7, 14, kBlood, 70, 0.6f, false, 220);
                fx_.shake = 4;
                hurt_flash_ = 0.35f;
                hitstop_ = std::max(hitstop_, kHitstopHurt);
                break;
            case EventType::PlayerDied:
                fx_.burst(x + 5, y + 7, 40, rgb(150, 200, 255), 40, 2.0f, true, -25);
                fx_.shake = 6;
                hurt_flash_ = 0.6f;
                hitstop_ = std::max(hitstop_, kHitstopDeath);
                banner("KAU TUMPAS", "You have fallen. The lantern will call you back...", kBlood, 3);
                break;
            case EventType::PlayerRespawned:
                fx_.burst(x + 5, y + 7, 20, rgb(255, 200, 120), 30, 1.0f, true, -30);
                break;
            case EventType::EnemyHit:
                fx_.burst(x + 6, y + 6, 12, rgb(255, 240, 200), 110, 0.3f, true);
                fx_.shake = std::max(fx_.shake, 1.5f);
                hitstop_ = std::max(hitstop_, kHitstopHit);
                break;
            case EventType::EnemyKilled:
                fx_.burst(x + 6, y + 6, 36, rgb(180, 160, 220), 55, 1.5f, true, -40);
                fx_.burst(x + 6, y + 6, 16, rgb(120, 10, 20), 80, 0.8f, false, 240);
                fx_.shake = std::max(fx_.shake, 3.0f);
                hitstop_ = std::max(hitstop_, kHitstopKill);
                break;
            case EventType::Pogo:
                fx_.burst(x, y + 4, 8, rgb(255, 240, 200), 60, 0.25f, true);
                fx_.squash = -0.5f;
                break;
            case EventType::Victory:
                victory_at_ = now_;
                fx_.burst(x + 5, y, 120, rgb(255, 200, 230), 80, 3.0f, true, -15);
                write_save();
                break;
        }
    }
}

Scene Game::build_scene(double alpha) const {
    Scene s;
    s.abilities = world_->abilities();
    s.lanterns_lit = world_->lanterns_lit();
    s.player = world_->player();
    auto lerp = [alpha](fx a, fx b) {
        // Teleports (respawn, stake hits) snap instead of sliding across the screen.
        if (std::abs(b - a) > 3 * kTileFx) return static_cast<double>(b) / kFx;
        return (static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * alpha) / kFx;
    };
    const PlayerState& a = prev_->player();
    s.px = lerp(a.x, s.player.x);
    s.py = lerp(a.y, s.player.y);
    const auto& now = world_->enemies();
    const auto& before = prev_->enemies();
    for (size_t i = 0; i < now.size(); ++i) {
        const EnemyState& e = now[i];
        const EnemyState& b = i < before.size() ? before[i] : e;
        s.enemies.push_back(EnemyView{e, lerp(b.x, e.x), lerp(b.y, e.y)});
    }
    return s;
}

// ---- Frame ----------------------------------------------------------------------------------------------------

bool Game::frame(double now, double dt) {
    now_ = now;
    if (started_ < 0) started_ = now;
    if (dt > 0) fps_ = fps_ * 0.95 + (1.0 / dt) * 0.05;
    renderer_.begin();

    if (screen_ == Screen::Title || !world_) {
        fx_.update(static_cast<float>(dt));
        const double t = now - started_;
        renderer_.snap_camera(800 + 620 * std::sin(t * 0.04), 330 + 120 * std::sin(t * 0.07));
        renderer_.draw_world(nullptr, fx_, now);
        draw_title();
    } else {
        if (screen_ == Screen::Playing) {
            simulate(dt);
            fx_.update(static_cast<float>(dt));
            hurt_flash_ = std::max(0.0f, hurt_flash_ - static_cast<float>(dt));
        }
        const double alpha = hitstop_ > 0 ? 1.0 : accumulator_ / kTickSeconds;
        const Scene scene = build_scene(alpha);
        const PlayerState& p = scene.player;
        renderer_.set_camera_target(scene.px + 5 + p.facing() * 24, scene.py - 10, dt);
        const std::string area = area_name(scene.px, scene.py);
        if (area != last_area_) {
            last_area_ = area;
            banner(area, area_subtitle(area), kPale, 3);
        }
        renderer_.draw_world(&scene, fx_, now);
        draw_hud();
        if (screen_ == Screen::Paused) draw_pause();
        if (debug_) draw_debug();
    }

    if (!opt_.record_dir.empty() && now - started_ >= opt_.screenshot_seconds) {
        char path[512];
        std::snprintf(path, sizeof path, "%s/frame_%04d.bmp", opt_.record_dir.c_str(), recorded_);
        renderer_.save_canvas(path);
        if (++recorded_ >= opt_.record_frames) quit_ = true;
    }
    if (!opt_.screenshot.empty() && !screenshot_taken_ && now - started_ >= opt_.screenshot_seconds) {
        screenshot_taken_ = true;
        if (!renderer_.save_canvas(opt_.screenshot.c_str())) std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
        quit_ = true;
    }
    renderer_.present();
    return !quit_;
}

// ---- Screens and HUD ------------------------------------------------------------------------------------------

void Game::draw_title() {
    const float t = static_cast<float>(now_ - started_);
    renderer_.fill(0, 0, kViewW, kViewH, rgb(0, 0, 0, 90));
    const float fade = std::min(1.0f, t / 2.0f);
    renderer_.text_centered(34, "GUNUNG LEDANG", rgb(220, 50, 50, static_cast<int>(255 * fade)), 3);
    renderer_.text_centered(64, "a horror on the haunted mountain", rgb(200, 190, 200, static_cast<int>(220 * fade)));

    const std::vector<std::string> items = menu_items();
    menu_index_ = std::clamp(menu_index_, 0, static_cast<int>(items.size()) - 1);
    for (size_t i = 0; i < items.size(); ++i) {
        const bool selected = static_cast<int>(i) == menu_index_;
        const std::string label = selected ? "> " + items[i] + " <" : items[i];
        renderer_.text_centered(96 + static_cast<float>(i) * 14, label, selected ? kGold : kDim);
    }
    if (save_) {
        const std::string info = std::to_string(std::popcount(save_->abilities)) + "/3 shrines   " +
                                 format_time(save_->play_ticks) + "   " + std::to_string(save_->deaths) + " deaths";
        renderer_.text_centered(144, info, rgb(150, 140, 160, 200));
    }

    renderer_.text_centered(176, "The Sultan of Melaka asked for her hand.", rgb(150, 140, 160, 200));
    renderer_.text_centered(188, "She asked for seven trays of mosquito hearts.", rgb(150, 140, 160, 200));
    renderer_.text_centered(200, "Nobody who climbed after her came down.", rgb(170, 60, 60, 220));
#ifdef LEDANG_VERSION
    renderer_.text(4, 4, LEDANG_VERSION, rgb(90, 80, 100), 1, false);
#endif
}

void Game::draw_pause() {
    renderer_.fill(0, 0, kViewW, kViewH, rgb(6, 4, 12, 170));
    renderer_.text_centered(56, "PAUSED", kPale, 2);
    const std::vector<std::string> items = menu_items();
    menu_index_ = std::clamp(menu_index_, 0, static_cast<int>(items.size()) - 1);
    for (size_t i = 0; i < items.size(); ++i) {
        const bool selected = static_cast<int>(i) == menu_index_;
        renderer_.text_centered(92 + static_cast<float>(i) * 14, selected ? "> " + items[i] + " <" : items[i], selected ? kGold : kDim);
    }
    const std::string info = std::to_string(std::popcount(world_->abilities())) + "/3 shrines   " +
                             format_time(world_->play_ticks()) + "   " + std::to_string(world_->deaths()) + " deaths";
    renderer_.text_centered(136, info, kDim);
    renderer_.text_centered(186, "Progress is saved at every lantern and shrine.", rgb(120, 110, 130));
}

void Game::draw_hud() {
    const PlayerState& p = world_->player();
    for (int i = 0; i < tuning::kMaxHp; ++i) {
        renderer_.sprite(renderer_.sprites().heart(i < p.hp), 6.0f + static_cast<float>(i) * 9, 6);
    }
    for (int i = 0; i < 3; ++i) {
        if (world_->abilities() & (1 << i)) renderer_.sprite(renderer_.sprites().ability_icon(i), 6.0f + static_cast<float>(i) * 11, 16);
    }

    if (hurt_flash_ > 0) renderer_.fill(0, 0, kViewW, kViewH, rgb(150, 0, 10, static_cast<int>(160 * std::min(1.0f, hurt_flash_ * 2))));

    // Banners: newest on top, fading in and out.
    std::erase_if(banners_, [this](const Banner& b) { return now_ - b.start > b.duration; });
    if (!banners_.empty()) {
        const Banner& b = banners_.back();
        const double age = now_ - b.start;
        const double a = std::min({1.0, age * 3, (b.duration - age) * 2});
        SDL_Color c = b.color;
        c.a = static_cast<Uint8>(255 * std::clamp(a, 0.0, 1.0));
        renderer_.text_centered(36, b.title, c, 2);
        SDL_Color sc = kPale;
        sc.a = c.a;
        if (!b.subtitle.empty()) renderer_.text_centered(56, b.subtitle, sc);
    }

    if (!moved_ && !opt_.autoplay && now_ - playing_since_ < 25) {
        renderer_.text_centered(174, "A/D move  SPACE jump  J strike  ESC pause", rgb(200, 190, 200, 200));
        renderer_.text_centered(186, "W+J strike up   S+J in the air: strike down", rgb(170, 160, 180, 200));
        renderer_.text_centered(198, "Strike down on a spirit or the stakes to bounce", rgb(150, 140, 160, 200));
    }

    if (victory_at_ >= 0) {
        const float a = static_cast<float>(std::min(1.0, (now_ - victory_at_) / 3.0));
        renderer_.fill(0, 0, kViewW, kViewH, rgb(20, 0, 10, static_cast<int>(150 * a)));
        renderer_.text_centered(64, "PUNCAK GUNUNG LEDANG", rgb(255, 200, 230, static_cast<int>(255 * a)), 2);
        renderer_.text_centered(92, "You reached the Puteri's shrine.", rgb(230, 220, 230, static_cast<int>(255 * a)));
        renderer_.text_centered(104, "She is not here. Only her flower remains,", rgb(230, 220, 230, static_cast<int>(255 * a)));
        renderer_.text_centered(116, "and it is still warm.", rgb(230, 120, 140, static_cast<int>(255 * a)));
        const std::string stats = format_time(world_->play_ticks()) + "   " + std::to_string(world_->deaths()) + " deaths";
        renderer_.text_centered(140, stats, rgb(200, 180, 200, static_cast<int>(220 * a)));
        if (now_ - victory_at_ > 4) renderer_.text_centered(170, "ENTER to return", rgb(150, 140, 160, static_cast<int>(200 * a)));
    }
}

void Game::draw_debug() {
    const PlayerState& p = world_->player();
    char line[128];
    std::snprintf(line, sizeof line, "%.0f fps  tick %u  pos %d,%d  vel %d,%d  hitstop %d", fps_, world_->tick(),
                  to_px(p.x), to_px(p.y), p.vx, p.vy, hitstop_);
    renderer_.fill(2, kViewH - 13, kViewW - 4, 11, rgb(0, 0, 0, 170));
    renderer_.text(5, kViewH - 11, line, rgb(150, 255, 170), 1, false);
}

}  // namespace ledang
