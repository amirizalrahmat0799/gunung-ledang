#include "client/game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/clock.hpp"

namespace ledang {

namespace {

SDL_Color rgb(int r, int g, int b, int a = 255) {
    return SDL_Color{static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), static_cast<Uint8>(a)};
}

const SDL_Color kGold = rgb(236, 196, 80);
const SDL_Color kPale = rgb(220, 210, 200);
const SDL_Color kBlood = rgb(210, 40, 44);
const SDL_Color kDim = rgb(150, 140, 160);

struct NetProfile {
    const char* name;
    LinkProfile link;  // applied in each direction
};
constexpr NetProfile kNetProfiles[] = {
    {"off", {0, 0, 0, 0}},
    {"50 ms ping, 1% loss", {25, 5, 0.01, 0}},
    {"150 ms ping, 5% loss", {75, 10, 0.05, 0}},
    {"250 ms ping, 10% loss", {125, 20, 0.10, 0}},
};
constexpr int kNetProfileCount = 4;

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

const char* area_subtitle(const char* name) {
    const std::string n = name;
    if (n == "PUNCAK LEDANG") return "The summit. Her shrine waits.";
    if (n == "AIR TERJUN") return "The waterfall shaft";
    if (n == "DATARAN BERHANTU") return "The haunted plateau";
    if (n == "HUTAN LARANGAN") return "The forbidden forest";
    if (n == "JURANG BULUH") return "Ravine of bamboo stakes";
    return "The foot of the mountain";
}

}  // namespace

// ---- ListenServer ---------------------------------------------------------------------------------------------

ListenServer::~ListenServer() { stop(); }

bool ListenServer::start(uint16_t port, int warp) {
    server_ = std::make_unique<Server>();
    if (warp >= 0) server_->debug_unlock(AbilityDash | AbilityDoubleJump | AbilityWallJump, warp);
    if (!server_->start(port) && !server_->start(0)) return false;
    port_ = server_->port();
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void ListenServer::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    server_.reset();
}

void ListenServer::set_link_profile(const LinkProfile& p) {
    std::lock_guard lock(mutex_);
    pending_profile_ = p;
}

void ListenServer::run() {
    while (running_) {
        {
            std::lock_guard lock(mutex_);
            if (pending_profile_) {
                server_->set_link_profile(*pending_profile_);
                pending_profile_.reset();
            }
        }
        const double now = now_seconds();
        server_->update(now);
        sleep_until_seconds(now + 0.001);
    }
}

// ---- Game -----------------------------------------------------------------------------------------------------

Game::Game(SDL_Renderer* renderer, GameOptions options)
    : sdl_(renderer), opt_(std::move(options)), renderer_(renderer, Map::world()) {
    if (opt_.host) {
        host_game();
    } else if (!opt_.connect.empty()) {
        join_game(opt_.connect);
    }
}

Game::~Game() {
    if (client_) client_->disconnect(now_seconds());
    for (auto& b : bots_) b->disconnect(now_seconds());
    if (gamepad_) SDL_CloseGamepad(gamepad_);
}

void Game::host_game() {
    listen_ = std::make_unique<ListenServer>();
    if (!listen_->start(opt_.port, opt_.warp)) {
        listen_.reset();
        status_message_ = "Could not open a UDP port to host on.";
        return;
    }
    join_game("127.0.0.1:" + std::to_string(listen_->port()));
    for (int i = 0; i < opt_.bots; ++i) {
        bots_.push_back(std::make_unique<ClientCore>());
        bots_.back()->connect(Address{0x7F000001, listen_->port()}, now_seconds());
        bot_brains_.emplace_back(static_cast<uint32_t>(900 + i));
    }
}

void Game::join_game(const std::string& address) {
    const auto addr = Address::parse(address, opt_.port);
    if (!addr) {
        status_message_ = "Can't find " + address;
        screen_ = Screen::Title;
        return;
    }
    client_ = std::make_unique<ClientCore>();
    client_->connect(*addr, now_seconds());
    screen_ = Screen::Playing;
    joined_at_ = -1;
    victory_at_ = -1;
    moved_ = false;
    status_message_.clear();
    apply_net_profile();
}

void Game::leave_game(const std::string& reason) {
    if (client_) client_->disconnect(now_seconds());
    client_.reset();
    for (auto& b : bots_) b->disconnect(now_seconds());
    bots_.clear();
    bot_brains_.clear();
    if (listen_) listen_->stop();
    listen_.reset();
    banners_.clear();
    fx_ = Effects{};
    status_message_ = reason;
    screen_ = Screen::Title;
}

void Game::apply_net_profile() {
    LinkProfile p = kNetProfiles[opt_.net_profile].link;
    if (listen_) {
        listen_->set_link_profile(p);  // both directions are simulated
    } else {
        p.latency_ms *= 2;  // remote server: only our upstream can be slowed, so put the whole round trip there
        p.jitter_ms *= 2;
    }
    if (client_) client_->set_link_profile(p);
}

void Game::banner(const std::string& title, const std::string& subtitle, SDL_Color color, double now, double duration) {
    banners_.push_back(Banner{title, subtitle, color, now, duration});
}

void Game::handle_event(const SDL_Event& e) {
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
        case SDL_EVENT_TEXT_INPUT:
            if (screen_ == Screen::JoinPrompt && address_text_.size() < 40) {
                for (const char* c = e.text.text; *c; ++c) {
                    if ((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '.' ||
                        *c == ':' || *c == '-') {
                        address_text_ += *c;
                    }
                }
            }
            break;
        case SDL_EVENT_KEY_DOWN: {
            if (e.key.repeat && e.key.key != SDLK_BACKSPACE) break;
            const SDL_Keycode k = e.key.key;
            if (screen_ == Screen::Title) {
                if (k == SDLK_1 || k == SDLK_H) host_game();
                if (k == SDLK_2 || k == SDLK_J) {
                    screen_ = Screen::JoinPrompt;
                    SDL_StartTextInput(SDL_GetRenderWindow(sdl_));
                }
                if (k == SDLK_ESCAPE || k == SDLK_Q) quit_ = true;
            } else if (screen_ == Screen::JoinPrompt) {
                if (k == SDLK_BACKSPACE && !address_text_.empty()) address_text_.pop_back();
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                    SDL_StopTextInput(SDL_GetRenderWindow(sdl_));
                    join_game(address_text_);
                }
                if (k == SDLK_ESCAPE) {
                    SDL_StopTextInput(SDL_GetRenderWindow(sdl_));
                    screen_ = Screen::Title;
                }
            } else {
                if (k == SDLK_ESCAPE) leave_game("");
                if (k == SDLK_F1) opt_.overlay = !opt_.overlay;
                if (k == SDLK_F2) {
                    opt_.net_profile = (opt_.net_profile + 1) % kNetProfileCount;
                    apply_net_profile();
                    banner("NETWORK", std::string("Simulating: ") + kNetProfiles[opt_.net_profile].name, kPale, now_seconds(), 2.5);
                }
                if (k == SDLK_F3 && client_) {
                    client_->set_prediction(!client_->prediction());
                    banner("PREDICTION", client_->prediction() ? "on: you move instantly" : "off: you wait for the server", kPale, now_seconds(), 2.5);
                }
                if (k == SDLK_F4 && client_) {
                    client_->set_smoothing(!client_->smoothing());
                    banner("SMOOTHING", client_->smoothing() ? "on" : "off: corrections snap", kPale, now_seconds(), 2.5);
                }
                if (k == SDLK_F5) opt_.ghosts = !opt_.ghosts;
            }
            break;
        }
        default:
            break;
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

void Game::handle_game_events(double now) {
    const int me = client_->slot();
    for (const Event& e : client_->take_events()) {
        const float x = e.x, y = e.y;
        switch (e.type) {
            case EventType::PlayerJoined:
                if (e.a != me) banner("P" + std::to_string(e.a + 1) + " has joined the climb", "", player_colors()[e.a].headcloth, now, 2.5);
                break;
            case EventType::PlayerLeft:
                banner("P" + std::to_string(e.a + 1) + " has left", "", kDim, now, 2.5);
                break;
            case EventType::AbilityGained: {
                fx_.burst(x + 8, y + 8, 50, kGold, 70, 1.6f, true, -20);
                if (e.a == AbilityDash) banner("KERAMAT ANGIN", "The wind shrine grants DASH  (Shift / C)", kGold, now, 5);
                if (e.a == AbilityDoubleJump) banner("KERAMAT BANGAU", "The crane shrine grants DOUBLE JUMP", kGold, now, 5);
                if (e.a == AbilityWallJump) banner("KERAMAT HARIMAU", "The tiger shrine grants WALL JUMP", kGold, now, 5);
                break;
            }
            case EventType::LanternLit:
                fx_.burst(x + 8, y + 4, 30, rgb(255, 170, 80), 50, 1.2f, true, -30);
                banner("PELITA MENYALA", "A lantern is lit. The fallen return here.", rgb(255, 180, 100), now, 3);
                break;
            case EventType::PlayerHurt:
                fx_.burst(x + 5, y + 7, 12, kBlood, 60, 0.6f, false, 200);
                if (e.a == me) fx_.shake = 3;
                break;
            case EventType::PlayerDied:
                fx_.burst(x + 5, y + 7, 40, rgb(150, 200, 255), 40, 2.0f, true, -25);
                if (e.a == me) {
                    fx_.shake = 5;
                    banner("KAU TUMPAS", "You have fallen. The lantern will call you back...", kBlood, now, 3);
                } else {
                    banner("P" + std::to_string(e.a + 1) + " has fallen", "", kBlood, now, 2.5);
                }
                break;
            case EventType::PlayerRespawned:
                break;
            case EventType::EnemyHit:
                fx_.burst(x + 6, y + 6, 10, rgb(255, 240, 200), 90, 0.35f, true);
                break;
            case EventType::EnemyKilled:
                fx_.burst(x + 6, y + 6, 36, rgb(180, 160, 220), 50, 1.5f, true, -40);
                fx_.burst(x + 6, y + 6, 14, rgb(120, 10, 20), 70, 0.8f, false, 220);
                break;
            case EventType::Victory:
                victory_at_ = now;
                fx_.burst(x + 5, y, 120, rgb(255, 200, 230), 80, 3.0f, true, -15);
                break;
        }
    }
}

bool Game::frame(double now, double dt) {
    if (started_ < 0) started_ = now;
    fx_.update(static_cast<float>(dt));
    renderer_.begin();

    if (screen_ == Screen::Title || screen_ == Screen::JoinPrompt) {
        const double t = now - started_;
        renderer_.snap_camera(800 + 620 * std::sin(t * 0.04), 330 + 120 * std::sin(t * 0.07));
        renderer_.draw_world(nullptr, fx_, now, {});
        if (screen_ == Screen::Title) {
            draw_title(now);
        } else {
            draw_join_prompt(now);
        }
    } else if (client_) {
        // Inputs are sampled at the simulation rate.
        bot_buttons_.resize(bots_.size(), 0);
        while (last_think_ + 1.0 / kTickRate <= now) {
            last_think_ = std::max(last_think_ + 1.0 / kTickRate, now - 0.1);
            buttons_ = opt_.autoplay ? auto_brain_.think(*client_) : read_input();
            for (size_t i = 0; i < bots_.size(); ++i) bot_buttons_[i] = bot_brains_[i].think(*bots_[i]);
        }
        bot_buttons_.resize(bots_.size(), 0);
        for (size_t i = 0; i < bots_.size(); ++i) bots_[i]->update(now, bot_buttons_[i]);
        client_->update(now, buttons_);

        const ClientStatus st = client_->status();
        if (st == ClientStatus::Denied || st == ClientStatus::TimedOut || st == ClientStatus::Disconnected) {
            leave_game(st == ClientStatus::Denied ? "The server is full (4 players)." : "Lost connection to the server.");
            renderer_.present();
            return !quit_;
        }
        if (client_->in_game()) {
            if (joined_at_ < 0) {
                joined_at_ = now;
                renderer_.snap_camera(client_->local_draw_x() + 5, client_->local_draw_y() + 7);
            }
            handle_game_events(now);
            renderer_.set_camera_target(client_->local_draw_x() + 5 + client_->local_player().facing() * 24,
                                        client_->local_draw_y() - 10, dt);
            static std::string last_area;
            const std::string area = area_name(client_->local_draw_x(), client_->local_draw_y());
            if (area != last_area && now - joined_at_ > 0.5) {
                last_area = area;
                banner(area, area_subtitle(area.c_str()), kPale, now, 3);
            }
            if (victory_at_ < 0 && client_->world_record().f[field::WVictory]) victory_at_ = now;
        }
        renderer_.draw_world(client_.get(), fx_, now, DrawOptions{opt_.ghosts, false});
        draw_hud(now);
        if (opt_.overlay) draw_overlay();
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

void Game::draw_title(double now) {
    const float t = static_cast<float>(now - started_);
    renderer_.fill(0, 0, kViewW, kViewH, rgb(0, 0, 0, 90));
    const float fade = std::min(1.0f, t / 2.0f);
    renderer_.text_centered(34, "GUNUNG LEDANG", rgb(220, 50, 50, static_cast<int>(255 * fade)), 3);
    renderer_.text_centered(64, "a co-op horror on the haunted mountain", rgb(200, 190, 200, static_cast<int>(220 * fade)));

    renderer_.text_centered(98, "[1]  HOST A GAME", kPale);
    renderer_.text_centered(112, "[2]  JOIN A GAME", kPale);
    renderer_.text_centered(126, "[ESC] QUIT", kDim);
#ifdef LEDANG_VERSION
    renderer_.text(4, kViewH - 10, LEDANG_VERSION, rgb(90, 80, 100), 1, false);
#endif

    if (!status_message_.empty()) renderer_.text_centered(146, status_message_, rgb(255, 120, 100));
    renderer_.text_centered(176, "The Sultan of Melaka asked for her hand.", rgb(150, 140, 160, 200));
    renderer_.text_centered(188, "She asked for seven trays of mosquito hearts.", rgb(150, 140, 160, 200));
    renderer_.text_centered(200, "Nobody who climbed after her came down.", rgb(170, 60, 60, 220));
}

void Game::draw_join_prompt(double now) {
    renderer_.fill(0, 0, kViewW, kViewH, rgb(0, 0, 0, 120));
    renderer_.text_centered(60, "JOIN A GAME", kGold, 2);
    renderer_.text_centered(92, "Server address (host or host:port)", kDim);
    const bool caret = static_cast<int>(now * 2) % 2 == 0;
    renderer_.fill(82, 106, 220, 16, rgb(20, 16, 30, 230));
    renderer_.text(88, 110, address_text_ + (caret ? "_" : ""), kPale, 1, false);
    renderer_.text_centered(134, "ENTER to connect   ESC to go back", kDim);
}

void Game::draw_hud(double now) {
    if (!client_->in_game()) {
        const std::string s = std::string(status_name(client_->status())) + "...";
        renderer_.text_centered(100, s, kPale);
        return;
    }
    const PlayerState& p = client_->local_player();
    for (int i = 0; i < tuning::kMaxHp; ++i) renderer_.sprite(renderer_.sprites().heart(i < p.hp), 6.0f + static_cast<float>(i) * 9, 6);
    const uint8_t abilities = static_cast<uint8_t>(client_->world_record().f[field::WAbilities]);
    for (int i = 0; i < 3; ++i) {
        if (abilities & (1 << i)) renderer_.sprite(renderer_.sprites().ability_icon(i), 6.0f + static_cast<float>(i) * 11, 16);
    }

    // Party
    float y = 6;
    for (int s = 0; s < kMaxPlayers; ++s) {
        const EntityRecord& r = client_->latest_snapshot().entities[static_cast<size_t>(kEntityFirstPlayer + s)];
        if (r.type != EntityType::Player) continue;
        const PlayerColors& pc = player_colors()[static_cast<size_t>(s)];
        renderer_.fill(kViewW - 62, y, 6, 6, pc.headcloth);
        std::string label = "P" + std::to_string(s + 1) + (s == client_->slot() ? " you" : "");
        if (r.f[field::PFlags] & FlagDead) label = "P" + std::to_string(s + 1) + " ...";
        renderer_.text(kViewW - 52, y - 1, label, s == client_->slot() ? kPale : kDim, 1);
        y += 10;
    }

    // Banners: newest on top, fading in and out.
    std::erase_if(banners_, [now](const Banner& b) { return now - b.start > b.duration; });
    if (!banners_.empty()) {
        const Banner& b = banners_.back();
        const double age = now - b.start;
        const double a = std::min({1.0, age * 3, (b.duration - age) * 2});
        SDL_Color c = b.color;
        c.a = static_cast<Uint8>(255 * std::clamp(a, 0.0, 1.0));
        renderer_.text_centered(36, b.title, c, 2);
        SDL_Color sc = kPale;
        sc.a = c.a;
        if (!b.subtitle.empty()) renderer_.text_centered(56, b.subtitle, sc);
    }

    if (p.has(FlagDead)) renderer_.text_centered(150, "...", kDim);

    if (!moved_ && !opt_.autoplay && !opt_.overlay && now - joined_at_ < 20) {
        renderer_.text_centered(186, "A/D move  SPACE jump  J attack  ESC leave", rgb(200, 190, 200, 200));
        renderer_.text_centered(198, "F1 netgraph  F2 lag  F3 prediction  F5 ghosts", rgb(150, 140, 160, 200));
    }

    if (victory_at_ >= 0) {
        const float a = static_cast<float>(std::min(1.0, (now - victory_at_) / 3.0));
        renderer_.fill(0, 0, kViewW, kViewH, rgb(20, 0, 10, static_cast<int>(150 * a)));
        renderer_.text_centered(70, "PUNCAK GUNUNG LEDANG", rgb(255, 200, 230, static_cast<int>(255 * a)), 2);
        renderer_.text_centered(98, "You reached the Puteri's shrine.", rgb(230, 220, 230, static_cast<int>(255 * a)));
        renderer_.text_centered(110, "She is not here. Only her flower remains,", rgb(230, 220, 230, static_cast<int>(255 * a)));
        renderer_.text_centered(122, "and it is still warm.", rgb(230, 120, 140, static_cast<int>(255 * a)));
    }
}

void Game::draw_overlay() {
    const ConnectionStats& n = client_->net_stats();
    const ClientStats& s = client_->stats();
    char lines[6][96];
    std::snprintf(lines[0], sizeof lines[0], "PING %3.0fms LOSS %4.1f%% IN %.0f OUT %.0f kbps", n.rtt_ms, n.loss * 100,
                  n.kbps_in, n.kbps_out);
    std::snprintf(lines[1], sizeof lines[1], "PREDICT %s SMOOTH %s FIX %llu/%llu %.1fpx",
                  client_->prediction() ? "on " : "OFF", client_->smoothing() ? "on " : "OFF",
                  static_cast<unsigned long long>(s.corrections), static_cast<unsigned long long>(s.predictions),
                  s.last_error_px);
    std::snprintf(lines[2], sizeof lines[2], "INTERP %.1ft (%.0fms) JIT %.2f STARVE %llu", s.interp_delay_ticks,
                  s.interp_delay_ticks * 1000.0 / kTickRate, s.jitter_ticks, static_cast<unsigned long long>(s.starved_frames));
    std::snprintf(lines[3], sizeof lines[3], "TICK %u VIEW %.1f CMD %u ACK %u", client_->server_tick(),
                  client_->render_tick(), client_->newest_cmd(), client_->acked_cmd());
    std::snprintf(lines[4], sizeof lines[4], "SNAP %.0fB  SIM %s",
                  s.snapshots ? static_cast<double>(s.snapshot_bytes) / static_cast<double>(s.snapshots) : 0.0,
                  kNetProfiles[opt_.net_profile].name);
    std::snprintf(lines[5], sizeof lines[5], "F2 net sim  F3 predict  F4 smooth  F5 ghosts");
    renderer_.fill(2, 150, 372, 64, rgb(0, 0, 0, 170));
    for (int i = 0; i < 6; ++i) {
        renderer_.text(6, 153 + static_cast<float>(i) * 10, lines[i], i == 5 ? kDim : rgb(150, 255, 170), 1, false);
    }
}

}  // namespace ledang
