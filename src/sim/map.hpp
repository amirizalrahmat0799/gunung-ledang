#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "sim/types.hpp"

namespace ledang {

enum class Tile : uint8_t {
    Empty,
    Solid,     // '#'
    Platform,  // '=' solid only from above; hold Down to drop through
    Spikes,    // '^'
};

enum class MarkerKind : uint8_t {
    PlayerSpawn,  // 'P'
    Lantern,      // 'L' checkpoint and light source
    Penanggal,    // 'h' flying head
    Pocong,       // 'p' hopping shroud
    ShrineDash,   // 'D'
    ShrineDoubleJump,  // 'J'
    ShrineWallJump,    // 'W'
    Guardian,     // 'B' (reserved for the summit guardian)
    Goal,         // 'G' the Puteri's shrine at the summit
};

struct Marker {
    MarkerKind kind;
    int tx, ty;  // tile coordinates
};

class Map {
public:
    /// Parses the ASCII format described in docs/map-format.md. Throws std::runtime_error on bad input.
    static Map parse(std::string_view text);
    /// The built-in world: the slopes of Gunung Ledang.
    static const Map& world();

    int width() const { return width_; }
    int height() const { return height_; }
    Tile at(int tx, int ty) const {
        if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) return Tile::Solid;
        return tiles_[static_cast<size_t>(ty) * static_cast<size_t>(width_) + static_cast<size_t>(tx)];
    }
    bool solid(int tx, int ty) const { return at(tx, ty) == Tile::Solid; }
    const std::vector<Marker>& markers() const { return markers_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<Tile> tiles_;
    std::vector<Marker> markers_;
};

}  // namespace ledang
