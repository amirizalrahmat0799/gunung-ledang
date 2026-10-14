#pragma once
// What goes inside the packets.
//
// Client -> server (unreliable, every tick): the newest user commands, each sent kCmdRedundancy times so a lost
// packet almost never loses input.
// Server -> client (unreliable, 30 Hz): a snapshot of every entity, delta-compressed against the newest snapshot
// the client is known to have received (learned from packet acks). Only fields that changed are sent.
// Server -> client (reliable): welcome, game events (pickups, deaths, victory).

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/bitstream.hpp"
#include "sim/world.hpp"

namespace ledang {

// ---- Handshake -------------------------------------------------------------------------------------------
// The connect request is padded so a spoofed request can never make the server send more than it received.
constexpr size_t kConnectRequestSize = 1000;

enum class DenyReason : uint8_t { ServerFull = 1, BadProtocol = 2 };

std::vector<uint8_t> make_connect_request(uint64_t client_salt);
std::vector<uint8_t> make_challenge(uint64_t client_salt, uint64_t server_salt);
std::vector<uint8_t> make_challenge_response(uint64_t combined_salt);
std::vector<uint8_t> make_denied(DenyReason reason);
std::vector<uint8_t> make_disconnect(uint64_t combined_salt);
/// Reads the u64 that follows the packet prefix (and the second one for Challenge).
std::optional<uint64_t> read_salt(std::span<const uint8_t> packet, size_t index = 0);

// ---- Unreliable bodies -----------------------------------------------------------------------------------
enum class BodyType : uint8_t { Commands = 1, Snapshot = 2 };

struct UserCmd {
    uint32_t number = 0;        // increases by one per client tick
    uint8_t buttons = 0;
    uint32_t view_tick_q8 = 0;  // the server tick (x256) the client was rendering other entities at
    bool operator==(const UserCmd&) const = default;
};
constexpr int kCmdRedundancy = 8;

/// Writes up to kCmdRedundancy commands (oldest first, consecutive numbers).
std::vector<uint8_t> encode_commands(std::span<const UserCmd> cmds);
bool decode_commands(std::span<const uint8_t> body, std::vector<UserCmd>& out);

// ---- Snapshots -------------------------------------------------------------------------------------------
enum class EntityType : uint8_t { None = 0, World = 1, Player = 2, Enemy = 3 };

constexpr int kMaxEntities = 64;
constexpr int kMaxFields = 20;
constexpr int kEntityWorld = 0;
constexpr int kEntityFirstPlayer = 1;
constexpr int kEntityFirstEnemy = kEntityFirstPlayer + kMaxPlayers;

namespace field {
enum Player : uint8_t {
    PX, PY, PVX, PVY, PFlags, PAirJumps, PAirDashed, PCoyote, PJumpBuffer, PDashTicks, PDashCooldown,
    PAttackTicks, PControlLock, PPrevButtons, PHp, PHurtTicks, PAbilities, PCheckpoint, PRespawnTicks,
    PlayerCount
};
enum Enemy : uint8_t { EKind, EFlags, EX, EY, EVX, EVY, EHp, EHurtTicks, ETimer, EnemyCount };
enum World : uint8_t { WAbilities, WLanternsLit, WVictory, WorldCount };
}  // namespace field

int field_count(EntityType type);

struct EntityRecord {
    EntityType type = EntityType::None;  // None = absent
    std::array<int32_t, kMaxFields> f{};
    bool operator==(const EntityRecord&) const = default;
};

struct Snapshot {
    uint32_t tick = 0;
    uint32_t last_cmd = 0;   // newest command of the receiving client that the server has run
    std::array<EntityRecord, kMaxEntities> entities{};
    bool operator==(const Snapshot&) const = default;
};

/// Captures the world's state for one client.
Snapshot capture(const World& world, uint32_t last_cmd);

EntityRecord player_record(const PlayerSlot& slot);
PlayerState player_from_record(const EntityRecord& r);
EntityRecord enemy_record(const EnemyState& e);
EnemyState enemy_from_record(const EntityRecord& r);

/// Encodes `snap`, sending only differences from `baseline` (or everything if baseline is null).
std::vector<uint8_t> encode_snapshot(const Snapshot& snap, const Snapshot* baseline);

struct SnapshotHeader {
    uint32_t tick = 0;
    std::optional<uint32_t> baseline_tick;
};
/// First step of decoding: tells the caller which baseline it must supply.
bool decode_snapshot_header(std::span<const uint8_t> body, SnapshotHeader& out);
bool decode_snapshot(std::span<const uint8_t> body, const Snapshot* baseline, Snapshot& out);

// ---- Reliable messages -----------------------------------------------------------------------------------
enum class ReliableType : uint8_t { Welcome = 1, GameEvent = 2 };

struct Welcome {
    uint8_t slot = 0;
    uint32_t server_tick = 0;
};

std::vector<uint8_t> encode_welcome(const Welcome& w);
std::vector<uint8_t> encode_event(const Event& e);
std::optional<ReliableType> reliable_type(std::span<const uint8_t> msg);
std::optional<Welcome> decode_welcome(std::span<const uint8_t> msg);
std::optional<Event> decode_event(std::span<const uint8_t> msg);

}  // namespace ledang
