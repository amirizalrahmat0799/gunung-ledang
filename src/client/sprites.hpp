#pragma once
// All art is generated at startup: sprites are drawn from ASCII pictures and palettes, tiles from seeded
// noise. No image files to ship.

#include <SDL3/SDL.h>

#include <array>
#include <string_view>
#include <vector>

namespace ledang {

struct PlayerColors {
    SDL_Color headcloth;  // tanjak
    SDL_Color sampin;     // waist cloth
};

/// The four players' colours: kuning diraja, merah, hijau, biru.
const std::array<PlayerColors, 4>& player_colors();

enum PlayerFrame { FrameIdle, FrameRun1, FrameRun2, FrameJump, FrameFall, FrameWall, FrameHurt, FrameCount };

class Sprites {
public:
    explicit Sprites(SDL_Renderer* r);
    ~Sprites();
    Sprites(const Sprites&) = delete;
    Sprites& operator=(const Sprites&) = delete;

    SDL_Texture* player(int slot, PlayerFrame f) const { return players_[static_cast<size_t>(slot)][f]; }
    SDL_Texture* penanggal() const { return penanggal_; }
    SDL_Texture* pocong() const { return pocong_; }
    SDL_Texture* keris_slash() const { return slash_; }
    SDL_Texture* rock(int variant) const { return rock_[static_cast<size_t>(variant) % rock_.size()]; }
    SDL_Texture* rock_top(int variant) const { return rock_top_[static_cast<size_t>(variant) % rock_top_.size()]; }
    SDL_Texture* bamboo() const { return bamboo_; }
    SDL_Texture* stakes() const { return stakes_; }
    SDL_Texture* lantern(bool lit) const { return lit ? lantern_lit_ : lantern_dark_; }
    SDL_Texture* shrine(bool claimed) const { return claimed ? shrine_used_ : shrine_; }
    SDL_Texture* puteri_shrine() const { return goal_; }
    SDL_Texture* light() const { return light_; }
    SDL_Texture* heart(bool full) const { return full ? heart_ : heart_empty_; }
    SDL_Texture* ability_icon(int index) const { return icons_[static_cast<size_t>(index)]; }

private:
    using Palette = std::vector<std::pair<char, SDL_Color>>;
    SDL_Texture* make(const std::vector<std::string_view>& rows, const Palette& palette);
    SDL_Texture* from_pixels(int w, int h, const std::vector<uint32_t>& rgba);

    SDL_Renderer* r_;
    std::vector<SDL_Texture*> owned_;
    std::array<std::array<SDL_Texture*, FrameCount>, 4> players_{};
    SDL_Texture* penanggal_ = nullptr;
    SDL_Texture* pocong_ = nullptr;
    SDL_Texture* slash_ = nullptr;
    std::array<SDL_Texture*, 4> rock_{};
    std::array<SDL_Texture*, 4> rock_top_{};
    SDL_Texture* bamboo_ = nullptr;
    SDL_Texture* stakes_ = nullptr;
    SDL_Texture* lantern_lit_ = nullptr;
    SDL_Texture* lantern_dark_ = nullptr;
    SDL_Texture* shrine_ = nullptr;
    SDL_Texture* shrine_used_ = nullptr;
    SDL_Texture* goal_ = nullptr;
    SDL_Texture* light_ = nullptr;
    SDL_Texture* heart_ = nullptr;
    SDL_Texture* heart_empty_ = nullptr;
    std::array<SDL_Texture*, 3> icons_{};
};

}  // namespace ledang
