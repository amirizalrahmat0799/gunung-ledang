#include "sim/save.hpp"

#include <charconv>
#include <cstdio>
#include <map>

namespace ledang {

namespace {

constexpr std::string_view kMagic = "gunung-ledang-save 1";

uint32_t fnv1a(std::string_view s) {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

bool parse_u32(std::string_view s, uint32_t max, uint32_t& out) {
    uint32_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size() || v > max) return false;
    out = v;
    return true;
}

}  // namespace

std::string save_to_text(const SaveData& s) {
    std::string body;
    body += kMagic;
    body += '\n';
    body += "abilities " + std::to_string(s.abilities) + '\n';
    body += "lanterns " + std::to_string(s.lanterns_lit) + '\n';
    body += "checkpoint " + std::to_string(s.checkpoint) + '\n';
    body += "deaths " + std::to_string(s.deaths) + '\n';
    body += "play_ticks " + std::to_string(s.play_ticks) + '\n';
    body += "victory " + std::string(s.victory ? "1" : "0") + '\n';
    char check[32];
    std::snprintf(check, sizeof check, "check %08x\n", fnv1a(body));
    return body + check;
}

std::optional<SaveData> save_from_text(std::string_view text) {
    const size_t check_at = text.rfind("check ");
    if (check_at == std::string_view::npos || check_at == 0 || text[check_at - 1] != '\n') return std::nullopt;
    const std::string_view body = text.substr(0, check_at);
    std::string_view check = text.substr(check_at + 6);
    while (!check.empty() && (check.back() == '\n' || check.back() == '\r')) check.remove_suffix(1);
    char expected[16];
    std::snprintf(expected, sizeof expected, "%08x", fnv1a(body));
    if (check != expected) return std::nullopt;

    std::map<std::string_view, std::string_view> fields;
    size_t pos = 0;
    bool first = true;
    while (pos < body.size()) {
        size_t end = body.find('\n', pos);
        if (end == std::string_view::npos) end = body.size();
        const std::string_view line = body.substr(pos, end - pos);
        pos = end + 1;
        if (first) {
            if (line != kMagic) return std::nullopt;
            first = false;
            continue;
        }
        const size_t space = line.find(' ');
        if (space == std::string_view::npos) return std::nullopt;
        fields[line.substr(0, space)] = line.substr(space + 1);
    }

    SaveData s;
    uint32_t v = 0;
    auto get = [&](std::string_view key, uint32_t max) {
        const auto it = fields.find(key);
        return it != fields.end() && parse_u32(it->second, max, v);
    };
    if (!get("abilities", 7)) return std::nullopt;
    s.abilities = static_cast<uint8_t>(v);
    if (!get("lanterns", 0xFFFF)) return std::nullopt;
    s.lanterns_lit = static_cast<uint16_t>(v);
    if (!get("checkpoint", 0xFF)) return std::nullopt;
    s.checkpoint = static_cast<uint8_t>(v);
    if (!get("deaths", 0xFFFFFFFF)) return std::nullopt;
    s.deaths = v;
    if (!get("play_ticks", 0xFFFFFFFF)) return std::nullopt;
    s.play_ticks = v;
    if (!get("victory", 1)) return std::nullopt;
    s.victory = v != 0;
    return s;
}

}  // namespace ledang
