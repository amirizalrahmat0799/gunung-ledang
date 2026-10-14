#include "client/sprites.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <string>

namespace ledang {

namespace {

uint32_t pack(SDL_Color c) {
    // SDL_PIXELFORMAT_RGBA32 is byte order R, G, B, A in memory.
    uint32_t v = 0;
    uint8_t* b = reinterpret_cast<uint8_t*>(&v);
    b[0] = c.r;
    b[1] = c.g;
    b[2] = c.b;
    b[3] = c.a;
    return v;
}

SDL_Color rgb(int r, int g, int b, int a = 255) {
    return SDL_Color{static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), static_cast<Uint8>(a)};
}

SDL_Color shade(SDL_Color c, float f) {
    auto ch = [f](Uint8 v) { return static_cast<Uint8>(std::clamp(static_cast<float>(v) * f, 0.0f, 255.0f)); };
    return SDL_Color{ch(c.r), ch(c.g), ch(c.b), c.a};
}

// Shared body rows for the player; legs differ per frame.
constexpr std::string_view kHead[] = {
    "....hhhh....",
    "...hhhhhH...",
    "..hhhhhhhH..",
    "...kssssk...",
    "...sssese...",
    "...ssssss...",
    "....SssS....",
};

}  // namespace

const std::array<PlayerColors, 4>& player_colors() {
    static const std::array<PlayerColors, 4> colors = {{
        {rgb(222, 178, 52), rgb(150, 40, 46)},   // kuning diraja (royal yellow) with red sampin
        {rgb(196, 52, 58), rgb(40, 40, 52)},     // merah
        {rgb(62, 152, 88), rgb(196, 160, 60)},   // hijau
        {rgb(64, 110, 196), rgb(210, 210, 220)}, // biru
    }};
    return colors;
}

SDL_Texture* Sprites::from_pixels(int w, int h, const std::vector<uint32_t>& rgba) {
    SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(t, nullptr, rgba.data(), w * 4);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    owned_.push_back(t);
    return t;
}

SDL_Texture* Sprites::make(const std::vector<std::string_view>& rows, const Palette& palette) {
    std::map<char, uint32_t> pal;
    for (const auto& [ch, color] : palette) pal[ch] = pack(color);
    size_t w = 0;
    for (std::string_view row : rows) w = std::max(w, row.size());
    const int h = static_cast<int>(rows.size());
    std::vector<uint32_t> px(w * static_cast<size_t>(h), 0);
    size_t y = 0;
    for (std::string_view row : rows) {
        for (size_t x = 0; x < row.size(); ++x) {
            const auto it = pal.find(row[x]);
            if (it != pal.end()) px[y * w + x] = it->second;
        }
        ++y;
    }
    return from_pixels(static_cast<int>(w), h, px);
}

Sprites::Sprites(SDL_Renderer* r) : r_(r) {
    const SDL_Color outline = rgb(20, 16, 24);
    const SDL_Color skin = rgb(170, 112, 74);
    const SDL_Color skin_dark = rgb(126, 80, 54);
    const SDL_Color baju = rgb(44, 40, 66);
    const SDL_Color pants = rgb(26, 24, 34);
    const SDL_Color eye = rgb(240, 232, 214);
    const SDL_Color gold = rgb(220, 176, 64);

    // ---- Players: a pendekar (silat warrior) in tanjak and sampin, with a keris at the waist ----------------
    for (size_t slot = 0; slot < 4; ++slot) {
        const PlayerColors& pc = player_colors()[slot];
        const Palette pal = {std::pair{'k', outline}, std::pair{'s', skin}, std::pair{'S', skin_dark},
                          std::pair{'h', pc.headcloth}, std::pair{'H', shade(pc.headcloth, 0.7f)},
                          std::pair{'b', baju}, std::pair{'c', pc.sampin}, std::pair{'p', pants},
                          std::pair{'e', eye}, std::pair{'y', gold}};
        auto frame = [&](std::string_view a, std::string_view b, std::string_view c, std::string_view d,
                         std::string_view e, std::string_view f, std::string_view g, std::string_view h,
                         std::string_view i) {
            return make({kHead[0], kHead[1], kHead[2], kHead[3], kHead[4], kHead[5], kHead[6], a, b, c, d, e, f, g,
                         h, i},
                        pal);
        };
        auto& fr = players_[slot];
        fr[FrameIdle] = frame("...bbbbbb...", "..bbbbbbbb..", "..sbbbbbbs..", "..scccccys..", "...cccccc...",
                              "...pppppp...", "...pp..pp...", "...pp..pp...", "..kkk..kkk..");
        fr[FrameRun1] = frame("...bbbbbb...", "..bbbbbbbbs.", ".sbbbbbbb...", "..ccccccy...", "...cccccc...",
                              "...pppppp...", "..pp....pp..", ".pp......pp.", ".kk......kk.");
        fr[FrameRun2] = frame("...bbbbbb...", "..bbbbbbbb..", "..sbbbbbbs..", "..ccccccys..", "...cccccc...",
                              "...pppppp...", "....pppp....", "....pp.pp...", "...kkk.kkk..");
        fr[FrameJump] = frame(".s.bbbbbb.s.", "..bbbbbbbb..", "...bbbbbb...", "..ccccccy...", "...cccccc...",
                              "..pppppppp..", "..pp....pp..", "..kk....kk..", "............");
        fr[FrameFall] = frame("...bbbbbb...", ".sbbbbbbbbs.", "..bbbbbbbb..", "..ccccccy...", "...cccccc...",
                              "...pppppp...", "...pp..pp...", "..pp....pp..", "..kk....kk..");
        fr[FrameWall] = frame("...bbbbbbs..", "..bbbbbbbs..", "..bbbbbbb...", "..cccccyc...", "...cccccc...",
                              "...pppppp...", "....pp.pp...", "...pp..pp...", "...kk..kk...");
        fr[FrameHurt] = frame(".s.bbbbbb.s.", "..bbbbbbbb..", "..bbbbbbbb..", "..cccccccy..", "...cccccc...",
                              "...pppppp...", "..pp...pp...", ".pp.....pp..", ".kk.....kk..");
    }

    // ---- Penanggal: a flying head with wild hair; entrails are drawn live below it -------------------------
    const SDL_Color pale = rgb(196, 176, 150);
    const SDL_Color pale_dark = rgb(150, 126, 104);
    penanggal_ = make({"...kkkkkkkk...", "..kkkkkkkkkk..", ".kkkkkkkkkkkk.", ".kkksssssskkk.", "kkksssssssskkk",
                       "kkssrrssrrsskk", "kkssrrssrrsskk", "kksssssssssskk", ".kssSmmmmSssk.", ".kkssmwwmsskk.",
                       ".k.kssssssk.k.", "k...kSSSSk...k", "k....oooo....k", ".k....oo....k."},
                      {{'k', rgb(14, 10, 16)}, {'s', pale}, {'S', pale_dark}, {'r', rgb(255, 40, 30)},
                       {'m', rgb(70, 10, 14)}, {'w', rgb(236, 230, 210)}, {'o', rgb(150, 20, 30)}});

    // ---- Pocong: a shrouded corpse, tied at head and feet ---------------------------------------------------
    pocong_ = make({".....ww.....", "....wggw....", ".....ww.....", "...wwwwww...", "..wwffffww..", "..wfrffrfw..",
                    "..wffmmffw..", "..wwffffww..", "..wwwwwwww..", ".wwwwgwwwww.", ".wwwwwwwwww.", ".wwwwwwgwww.",
                    ".wwgwwwwwww.", ".wwwwwwwwww.", "..wwwwwwww..", "...wwggww...", "....wggw....", ".....ww....."},
                   {{'w', rgb(214, 208, 192)}, {'g', rgb(150, 144, 130)}, {'f', rgb(120, 136, 112)},
                    {'r', rgb(255, 50, 40)}, {'m', rgb(40, 30, 30)}});

    // ---- Keris slash: a crescent arc ------------------------------------------------------------------------
    {
        const int w = 22, h = 20;
        std::vector<uint32_t> px(static_cast<size_t>(w * h), 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const double dx = x - 2.0, dy = y - h / 2.0;
                const double d = std::sqrt(dx * dx + dy * dy);
                const double ang = std::atan2(dy, dx);
                if (d > 13 && d < 19.5 && std::abs(ang) < 1.25) {
                    const double t = (d - 13) / 6.5;  // inner edge brightest
                    const int a = static_cast<int>(255 * (1.0 - t) * (1.0 - std::abs(ang) / 1.3));
                    px[static_cast<size_t>(y * w + x)] = pack(rgb(255, 240, 190, std::clamp(a, 0, 255)));
                }
            }
        }
        slash_ = from_pixels(w, h, px);
    }

    // ---- Tiles ----------------------------------------------------------------------------------------------
    std::mt19937 rng(1459);  // the year Hang Tuah's mission to the Puteri is set, more or less
    auto noise_tile = [&](bool mossy) {
        std::vector<uint32_t> px(16 * 16);
        std::uniform_int_distribution<int> n(-10, 10);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                int base = 50 + n(rng);
                if ((x * 7 + y * 13 + static_cast<int>(rng() % 5)) % 23 == 0) base += 24;  // speckles
                SDL_Color c = rgb(base, base - 6, base + 6);
                if (x == 0 || y == 0) c = shade(c, 1.15f);
                if (x == 15 || y == 15) c = shade(c, 0.7f);
                if (mossy && y < 4) {
                    const int g = 70 + n(rng) * 2;
                    const bool blade = y < 3 - static_cast<int>((x * 5 + 3) % 4 == 0);
                    c = y == 3 ? rgb(36, 60, 40) : (blade ? rgb(40, g, 52) : c);
                }
                px[static_cast<size_t>(y * 16 + x)] = pack(c);
            }
        }
        // a crack
        int cx = static_cast<int>(rng() % 12) + 2, cy = mossy ? 6 : static_cast<int>(rng() % 8) + 2;
        for (int i = 0; i < 6; ++i) {
            px[static_cast<size_t>(std::clamp(cy, 0, 15) * 16 + std::clamp(cx, 0, 15))] = pack(rgb(24, 20, 28));
            cx += static_cast<int>(rng() % 3) - 1;
            ++cy;
        }
        return from_pixels(16, 16, px);
    };
    for (auto& t : rock_) t = noise_tile(false);
    for (auto& t : rock_top_) t = noise_tile(true);

    bamboo_ = make({"kkkkkkkkkkkkkkkk", "yyyyyyyKyyyyyyyK", "YYYYYYYKYYYYYYYK", "yyyyyyyKyyyyyyyK", "kkkkkkkkkkkkkkkk",
                    ".k............k.", ".k............k."},
                   {{'k', rgb(40, 34, 20)}, {'y', rgb(168, 160, 82)}, {'Y', rgb(120, 116, 56)}, {'K', rgb(70, 62, 30)}});
    // Buluh runcing: sharpened bamboo stakes, the old village defence
    stakes_ = make({"................", "................", "................", "................",
                    "................", "................", "..r.....r....r..", "..y....ry....y..",
                    ".yy....yy...yy..", ".yY..r.yY...yY.r", ".yY..y.yY...yY.y", ".yY.yy.yY..yyY.y",
                    ".yY.yY.yY..yYY.y", "kyYkyYkyYkkyYYky", "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk"},
                   {{'y', rgb(176, 160, 90)}, {'Y', rgb(120, 104, 56)}, {'r', rgb(120, 20, 24)}, {'k', rgb(30, 24, 22)}});

    const auto lantern_pal = [&](bool lit) {
        return Palette{
            {'f', lit ? rgb(255, 214, 90) : rgb(0, 0, 0, 0)}, {'F', lit ? rgb(255, 130, 40) : rgb(0, 0, 0, 0)},
            {'g', lit ? rgb(200, 150, 70) : rgb(60, 54, 50)},  {'k', rgb(30, 22, 18)},
            {'w', rgb(96, 64, 40)}};
    };
    lantern_lit_ = make({"..kkkk..", ".kgffgk.", ".kgFFgk.", ".kgffgk.", "..kkkk..", "...ww...", "...ww...",
                         "...ww...", "...ww...", "...ww...", "...ww...", "...ww...", "..wwww..", ".kkkkkk."},
                        lantern_pal(true));
    lantern_dark_ = make({"..kkkk..", ".kgffgk.", ".kgFFgk.", ".kgffgk.", "..kkkk..", "...ww...", "...ww...",
                          "...ww...", "...ww...", "...ww...", "...ww...", "...ww...", "..wwww..", ".kkkkkk."},
                         lantern_pal(false));

    // Keramat: a weathered grave marker wrapped in yellow cloth
    const std::vector<std::string_view> shrine_rows = {"....kkkk....", "...kggggk...", "...kgGggk...", "...kggggk...", "..yyyyyyyy..",
                              "...yYyyyy...", "...kggggk...", "...kgggGk...", "...kggggk...", "..kkggggkk..",
                              ".kggggggggk.", "kkkkkkkkkkkk"};
    shrine_ = make(shrine_rows, {{'k', rgb(28, 26, 30)}, {'g', rgb(120, 122, 118)}, {'G', rgb(90, 92, 88)},
                                 {'y', rgb(236, 196, 50)}, {'Y', rgb(190, 140, 30)}});
    shrine_used_ = make(shrine_rows, {{'k', rgb(28, 26, 30)}, {'g', rgb(90, 92, 88)}, {'G', rgb(70, 72, 68)},
                                      {'y', rgb(110, 96, 60)}, {'Y', rgb(90, 76, 46)}});

    // The Puteri's shrine at the summit: a small pavilion sheltering a glowing flower
    goal_ = make({"..........rr..........", "........rrrrrr........", "......rrrrrrrrrr......",
                  "....rrrrrrrrrrrrrr....", "..rrrrrrrrrrrrrrrrrr..", "rrrRRRRRRRRRRRRRRRRrrr",
                  "..w................w..", "..w.......pp.......w..", "..w......pPPp......w..",
                  "..w.......pp.......w..", "..w........g.......w..", "..w.......gg.......w..",
                  "..w........g.......w..", "..w.....kkkkkk.....w..", "..w.....kggggk.....w..",
                  "wwwwwwwwwwwwwwwwwwwwww", "kkkkkkkkkkkkkkkkkkkkkk"},
                 {{'r', rgb(90, 40, 34)}, {'R', rgb(130, 60, 40)}, {'w', rgb(110, 76, 46)}, {'k', rgb(30, 24, 22)},
                  {'p', rgb(255, 210, 230)}, {'P', rgb(255, 255, 220)}, {'g', rgb(90, 160, 90)}});

    // Soft radial light for the lightmap (brightness falls off smoothly; drawn additively).
    {
        const int s = 64;
        std::vector<uint32_t> px(static_cast<size_t>(s * s));
        for (int y = 0; y < s; ++y) {
            for (int x = 0; x < s; ++x) {
                const double dx = (x + 0.5 - s / 2.0) / (s / 2.0), dy = (y + 0.5 - s / 2.0) / (s / 2.0);
                const double d = std::min(1.0, std::sqrt(dx * dx + dy * dy));
                const double i = std::pow(1.0 - d, 1.6) * (1.0 + 0.6 * d);  // smooth, fairly wide
                const int v = static_cast<int>(255 * std::clamp(i, 0.0, 1.0));
                px[static_cast<size_t>(y * s + x)] = pack(rgb(v, v, v, 255));
            }
        }
        light_ = from_pixels(s, s, px);
        SDL_SetTextureScaleMode(light_, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(light_, SDL_BLENDMODE_ADD);
    }

    const std::vector<std::string_view> heart_rows = {".rr.rr.", "rrrrrrr", "rrrrrrr", ".rrrrr.", "..rrr..", "...r..."};
    heart_ = make(heart_rows, {{'r', rgb(200, 30, 40)}});
    heart_empty_ = make(heart_rows, {{'r', rgb(60, 30, 36)}});

    const SDL_Color ic = rgb(236, 196, 50);
    icons_[0] = make({"........", ".yyyyy..", "......y.", ".yyyyy..", "........", "..yyyyyy", "........",
                      ".yyyy..."},
                     {{'y', ic}});  // wind
    icons_[1] = make({"y......y", "yy....yy", ".yy..yy.", "..yyyy..", "...yy...", "...yy...", "...yy...",
                      "..y..y.."},
                     {{'y', ic}});  // crane wings
    icons_[2] = make({"y..y..y.", "y..y..y.", ".y..y..y", ".y..y..y", "..y..y..", "..y..y..", "........",
                      "yyyyyyyy"},
                     {{'y', ic}});  // tiger claws
}

Sprites::~Sprites() {
    for (SDL_Texture* t : owned_) SDL_DestroyTexture(t);
}

}  // namespace ledang
