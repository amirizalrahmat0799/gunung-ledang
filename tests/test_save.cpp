#include "sim/save.hpp"
#include "sim/world.hpp"
#include "test.hpp"

using namespace ledang;

TEST(save_round_trip) {
    SaveData s;
    s.abilities = AbilityDash | AbilityWallJump;
    s.lanterns_lit = 0b1011;
    s.checkpoint = 3;
    s.deaths = 12;
    s.play_ticks = 123456;
    s.victory = true;
    const std::string text = save_to_text(s);
    const auto back = save_from_text(text);
    CHECK(back.has_value());
    CHECK(back && *back == s);

    SaveData fresh;
    CHECK(save_from_text(save_to_text(fresh)) == fresh);  // no checkpoint yet
}

TEST(save_rejects_damaged_files) {
    SaveData s;
    s.deaths = 3;
    const std::string text = save_to_text(s);
    CHECK(!save_from_text("").has_value());
    CHECK(!save_from_text(text.substr(0, text.size() / 2)).has_value());  // truncated
    std::string edited = text;
    edited[edited.find("deaths 3")] = 'D';  // corrupted byte
    CHECK(!save_from_text(edited).has_value());
    std::string cheated = text;
    cheated.replace(cheated.find("deaths 3"), 8, "deaths 0");  // changed without fixing the checksum
    CHECK(!save_from_text(cheated).has_value());
    std::string windows = text;  // CRLF line endings at the end (opened and saved in Notepad)
    windows.insert(windows.size() - 1, "\r");
    CHECK(save_from_text(windows).has_value());
}

TEST(world_save_and_restore) {
    World w(Map::world());
    w.debug_unlock(AbilityDash | AbilityDoubleJump, 1);
    for (int i = 0; i < 30; ++i) w.update(0);
    const SaveData s = w.save_data();
    CHECK_EQ(s.abilities, uint8_t{AbilityDash | AbilityDoubleJump});
    CHECK_EQ(s.checkpoint, uint8_t{1});
    CHECK_EQ(s.play_ticks, 30u);

    World restored(Map::world());
    restored.restore(*save_from_text(save_to_text(s)));
    CHECK_EQ(restored.abilities(), s.abilities);
    CHECK_EQ(restored.lanterns_lit(), s.lanterns_lit);
    CHECK_EQ(restored.play_ticks(), 30u);
    // You wake up at your lantern.
    const Lantern& l = restored.lanterns()[1];
    CHECK_EQ(restored.player().x >> kTileShift, l.tx);
    CHECK_EQ((restored.player().y + tuning::kHeight - 1) >> kTileShift, l.ty);

    // A save pointing at a lantern that doesn't exist starts you at the village instead of crashing.
    SaveData bad = s;
    bad.checkpoint = 200;
    World safe(Map::world());
    safe.restore(bad);
    CHECK_EQ(safe.checkpoint(), kNoCheckpoint);
}
