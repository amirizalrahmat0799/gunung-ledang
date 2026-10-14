#pragma once
// A scripted player for load and soak testing: wanders the mountain, jumps, dashes and swings at spirits.
// Input only; it plays through ClientCore exactly like a human would.

#include <cstdint>
#include <cmath>
#include <random>

#include "client/client_core.hpp"

namespace ledang {

class BotBrain {
public:
    explicit BotBrain(uint32_t seed) : rng_(seed) {}

    uint8_t think(const ClientCore& client) {
        if (!client.in_game()) return 0;
        if (--hold_ <= 0) {
            std::uniform_int_distribution<int> pick(0, 9);
            const int r = pick(rng_);
            dir_ = r < 6 ? 1 : (r < 9 ? -1 : 0);  // mostly uphill (to the right)
            hold_ = std::uniform_int_distribution<int>(20, 90)(rng_);
        }
        uint8_t b = 0;
        if (dir_ > 0) b |= BtnRight;
        if (dir_ < 0) b |= BtnLeft;

        const PlayerState& p = client.local_player();
        // Jump in bursts: hold for a while (full height), release, repeat.
        ++jump_phase_;
        if (jump_phase_ % 50 < 18) b |= BtnJump;
        if (p.has(FlagWallLeft) || p.has(FlagWallRight)) {
            if (jump_phase_ % 12 < 6) b |= BtnJump;
        }
        if (std::uniform_int_distribution<int>(0, 89)(rng_) == 0) b |= BtnDash;
        if (std::uniform_int_distribution<int>(0, 99)(rng_) == 0) b |= BtnDown;

        // Swing at anything close.
        const double px = client.local_draw_x();
        const double py = client.local_draw_y();
        for (int id = kEntityFirstEnemy; id < kMaxEntities; ++id) {
            const EntityView& v = client.views()[static_cast<size_t>(id)];
            if (!v.present || !(v.record.f[field::EFlags] & EnemyAlive)) continue;
            if (std::abs(v.x - px) < 26 && std::abs(v.y - py) < 18 && (attack_cooldown_-- <= 0)) {
                b |= BtnAttack;
                b = static_cast<uint8_t>(b & ~(BtnLeft | BtnRight));
                b |= v.x < px ? BtnLeft : BtnRight;
                attack_cooldown_ = 20;
            }
        }
        if (idle_) b = 0;
        return b;
    }

    /// Stops all input (used by tests to let prediction settle).
    void set_idle(bool idle) { idle_ = idle; }

private:
    std::mt19937 rng_;
    int hold_ = 0;
    int dir_ = 1;
    int jump_phase_ = 0;
    int attack_cooldown_ = 0;
    bool idle_ = false;
};

}  // namespace ledang
