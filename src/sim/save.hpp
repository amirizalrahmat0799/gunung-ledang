#pragma once
// Save data: what survives quitting the game. Saved at every lantern, shrine and at the summit, and when you
// quit. The file is plain text so it is easy to inspect, with a checksum line that catches truncated or
// corrupted files (a corrupted save is ignored rather than loaded half-way).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ledang {

constexpr uint8_t kNoCheckpoint = 0xFF;

struct SaveData {
    uint8_t abilities = 0;
    uint16_t lanterns_lit = 0;
    uint8_t checkpoint = kNoCheckpoint;  // lantern index to continue from
    uint32_t deaths = 0;
    uint32_t play_ticks = 0;
    bool victory = false;

    bool operator==(const SaveData&) const = default;
};

std::string save_to_text(const SaveData& s);
/// Returns nullopt if the text is not a valid, intact save.
std::optional<SaveData> save_from_text(std::string_view text);

}  // namespace ledang
