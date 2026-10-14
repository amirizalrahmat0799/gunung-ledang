#include "proto/protocol.hpp"

#include <cstring>

#include "net/connection.hpp"

namespace ledang {

namespace {

void put_u64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

std::vector<uint8_t> handshake(PacketType type, std::initializer_list<uint64_t> values, size_t pad_to) {
    std::vector<uint8_t> out;
    write_packet_prefix(out, type);
    for (uint64_t v : values) put_u64(out, v);
    if (out.size() < pad_to) out.resize(pad_to, 0);
    return out;
}

int32_t wrap_sub(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
int32_t wrap_add(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }

constexpr int kEntityIdBits = bits_required(kMaxEntities - 1);

enum class Op : uint8_t { Update = 0, Create = 1, Remove = 2 };

}  // namespace

// ---- Handshake -------------------------------------------------------------------------------------------

std::vector<uint8_t> make_connect_request(uint64_t client_salt) {
    return handshake(PacketType::ConnectRequest, {client_salt}, kConnectRequestSize);
}
std::vector<uint8_t> make_challenge(uint64_t client_salt, uint64_t server_salt) {
    return handshake(PacketType::Challenge, {client_salt, server_salt}, 0);
}
std::vector<uint8_t> make_challenge_response(uint64_t combined_salt) {
    return handshake(PacketType::ChallengeResponse, {combined_salt}, kConnectRequestSize);
}
std::vector<uint8_t> make_denied(DenyReason reason) {
    std::vector<uint8_t> out;
    write_packet_prefix(out, PacketType::Denied);
    out.push_back(static_cast<uint8_t>(reason));
    return out;
}
std::vector<uint8_t> make_disconnect(uint64_t combined_salt) {
    return handshake(PacketType::Disconnect, {combined_salt}, 0);
}

std::optional<uint64_t> read_salt(std::span<const uint8_t> packet, size_t index) {
    const size_t at = kPrefixSize + index * 8;
    if (packet.size() < at + 8) return std::nullopt;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(packet[at + static_cast<size_t>(i)]) << (8 * i);
    return v;
}

// ---- Commands --------------------------------------------------------------------------------------------

std::vector<uint8_t> encode_commands(std::span<const UserCmd> cmds) {
    if (cmds.size() > static_cast<size_t>(kCmdRedundancy)) cmds = cmds.last(static_cast<size_t>(kCmdRedundancy));
    BitWriter w;
    w.write_bits(static_cast<uint32_t>(BodyType::Commands), 8);
    w.write_int(static_cast<int32_t>(cmds.size()), 0, kCmdRedundancy);
    if (!cmds.empty()) {
        w.write_u32(cmds[0].number);
        w.write_u32(cmds[0].view_tick_q8);
    }
    for (size_t i = 0; i < cmds.size(); ++i) {
        w.write_bits(cmds[i].buttons, 7);
        if (i > 0) {
            w.write_varint(wrap_sub(static_cast<int32_t>(cmds[i].view_tick_q8), static_cast<int32_t>(cmds[i - 1].view_tick_q8)));
        }
    }
    return w.finish();
}

bool decode_commands(std::span<const uint8_t> body, std::vector<UserCmd>& out) {
    out.clear();
    BitReader r(body);
    if (r.read_bits(8) != static_cast<uint32_t>(BodyType::Commands)) return false;
    const int count = r.read_int(0, kCmdRedundancy);
    if (count == 0) return !r.failed();
    UserCmd c;
    c.number = r.read_u32();
    c.view_tick_q8 = r.read_u32();
    for (int i = 0; i < count; ++i) {
        c.buttons = static_cast<uint8_t>(r.read_bits(7));
        if (i > 0) {
            ++c.number;
            c.view_tick_q8 = static_cast<uint32_t>(wrap_add(static_cast<int32_t>(c.view_tick_q8), r.read_varint()));
        }
        out.push_back(c);
    }
    return !r.failed();
}

// ---- Entity records --------------------------------------------------------------------------------------

int field_count(EntityType type) {
    switch (type) {
        case EntityType::World: return field::WorldCount;
        case EntityType::Player: return field::PlayerCount;
        case EntityType::Enemy: return field::EnemyCount;
        case EntityType::None: break;
    }
    return 0;
}

EntityRecord player_record(const PlayerSlot& slot) {
    using namespace field;
    const PlayerState& p = slot.state;
    EntityRecord r;
    r.type = EntityType::Player;
    r.f[PX] = p.x;
    r.f[PY] = p.y;
    r.f[PVX] = p.vx;
    r.f[PVY] = p.vy;
    r.f[PFlags] = p.flags;
    r.f[PAirJumps] = p.air_jumps;
    r.f[PAirDashed] = p.air_dashed;
    r.f[PCoyote] = p.coyote;
    r.f[PJumpBuffer] = p.jump_buffer;
    r.f[PDashTicks] = p.dash_ticks;
    r.f[PDashCooldown] = p.dash_cooldown;
    r.f[PAttackTicks] = p.attack_ticks;
    r.f[PControlLock] = p.control_lock;
    r.f[PPrevButtons] = p.prev_buttons;
    r.f[PHp] = p.hp;
    r.f[PHurtTicks] = p.hurt_ticks;
    r.f[PAbilities] = p.abilities;
    r.f[PCheckpoint] = slot.checkpoint;
    r.f[PRespawnTicks] = slot.respawn_ticks;
    return r;
}

PlayerState player_from_record(const EntityRecord& r) {
    using namespace field;
    auto u8 = [&](int i) { return static_cast<uint8_t>(r.f[static_cast<size_t>(i)]); };
    PlayerState p;
    p.x = r.f[PX];
    p.y = r.f[PY];
    p.vx = r.f[PVX];
    p.vy = r.f[PVY];
    p.flags = u8(PFlags);
    p.air_jumps = u8(PAirJumps);
    p.air_dashed = u8(PAirDashed);
    p.coyote = u8(PCoyote);
    p.jump_buffer = u8(PJumpBuffer);
    p.dash_ticks = u8(PDashTicks);
    p.dash_cooldown = u8(PDashCooldown);
    p.attack_ticks = u8(PAttackTicks);
    p.control_lock = u8(PControlLock);
    p.prev_buttons = u8(PPrevButtons);
    p.hp = u8(PHp);
    p.hurt_ticks = u8(PHurtTicks);
    p.abilities = u8(PAbilities);
    return p;
}

EntityRecord enemy_record(const EnemyState& e) {
    using namespace field;
    EntityRecord r;
    r.type = EntityType::Enemy;
    r.f[EKind] = static_cast<int32_t>(e.kind);
    r.f[EFlags] = e.flags;
    r.f[EX] = e.x;
    r.f[EY] = e.y;
    r.f[EVX] = e.vx;
    r.f[EVY] = e.vy;
    r.f[EHp] = e.hp;
    r.f[EHurtTicks] = e.hurt_ticks;
    r.f[ETimer] = e.timer;
    return r;
}

EnemyState enemy_from_record(const EntityRecord& r) {
    using namespace field;
    EnemyState e;
    e.kind = r.f[EKind] == 1 ? EnemyKind::Pocong : EnemyKind::Penanggal;
    e.flags = static_cast<uint8_t>(r.f[EFlags]);
    e.x = r.f[EX];
    e.y = r.f[EY];
    e.vx = r.f[EVX];
    e.vy = r.f[EVY];
    e.hp = static_cast<uint8_t>(r.f[EHp]);
    e.hurt_ticks = static_cast<uint8_t>(r.f[EHurtTicks]);
    e.timer = static_cast<uint16_t>(r.f[ETimer]);
    return e;
}

Snapshot capture(const World& world, uint32_t last_cmd) {
    using namespace field;
    Snapshot s;
    s.tick = world.tick();
    s.last_cmd = last_cmd;
    EntityRecord& w = s.entities[kEntityWorld];
    w.type = EntityType::World;
    w.f[WAbilities] = world.abilities();
    w.f[WLanternsLit] = world.lanterns_lit();
    w.f[WVictory] = world.victory() ? 1 : 0;
    for (int i = 0; i < kMaxPlayers; ++i) {
        const PlayerSlot& slot = world.players()[static_cast<size_t>(i)];
        if (slot.active) s.entities[static_cast<size_t>(kEntityFirstPlayer + i)] = player_record(slot);
    }
    const auto& enemies = world.enemies();
    for (size_t i = 0; i < enemies.size() && kEntityFirstEnemy + i < static_cast<size_t>(kMaxEntities); ++i) {
        s.entities[kEntityFirstEnemy + i] = enemy_record(enemies[i]);
    }
    return s;
}

// ---- Snapshot encoding -----------------------------------------------------------------------------------
// Layout: type u8 | tick u32 | has_baseline bit [age 8 bits] | last_cmd u32 |
//         per changed entity: 1 | id | op (2 bits) | data ... | 0
// Update data: per field, a "changed" bit, then the varint delta from the baseline value.
// Create data: entity type (2 bits), then each field as a varint.

std::vector<uint8_t> encode_snapshot(const Snapshot& snap, const Snapshot* baseline) {
    static const Snapshot kEmpty{};
    const Snapshot& base = baseline ? *baseline : kEmpty;
    BitWriter w;
    w.write_bits(static_cast<uint32_t>(BodyType::Snapshot), 8);
    w.write_u32(snap.tick);
    w.write_bool(baseline != nullptr);
    if (baseline) w.write_bits(snap.tick - baseline->tick, 8);
    w.write_u32(snap.last_cmd);

    for (int id = 0; id < kMaxEntities; ++id) {
        const EntityRecord& now = snap.entities[static_cast<size_t>(id)];
        const EntityRecord& old = base.entities[static_cast<size_t>(id)];
        if (now == old) continue;
        if (now.type == EntityType::None) {
            w.write_bool(true);
            w.write_bits(static_cast<uint32_t>(id), kEntityIdBits);
            w.write_bits(static_cast<uint32_t>(Op::Remove), 2);
            continue;
        }
        const int n = field_count(now.type);
        w.write_bool(true);
        w.write_bits(static_cast<uint32_t>(id), kEntityIdBits);
        if (old.type == now.type) {
            w.write_bits(static_cast<uint32_t>(Op::Update), 2);
            for (int i = 0; i < n; ++i) {
                const int32_t d = wrap_sub(now.f[static_cast<size_t>(i)], old.f[static_cast<size_t>(i)]);
                w.write_bool(d != 0);
                if (d != 0) w.write_varint(d);
            }
        } else {
            w.write_bits(static_cast<uint32_t>(Op::Create), 2);
            w.write_bits(static_cast<uint32_t>(now.type), 2);
            for (int i = 0; i < n; ++i) w.write_varint(now.f[static_cast<size_t>(i)]);
        }
    }
    w.write_bool(false);
    return w.finish();
}

namespace {

bool read_header(BitReader& r, SnapshotHeader& out) {
    if (r.read_bits(8) != static_cast<uint32_t>(BodyType::Snapshot)) return false;
    out.tick = r.read_u32();
    out.baseline_tick.reset();
    if (r.read_bool()) {
        const uint32_t age = r.read_bits(8);
        if (age == 0) return false;
        out.baseline_tick = out.tick - age;
    }
    return !r.failed();
}

}  // namespace

bool decode_snapshot_header(std::span<const uint8_t> body, SnapshotHeader& out) {
    BitReader r(body);
    return read_header(r, out);
}

bool decode_snapshot(std::span<const uint8_t> body, const Snapshot* baseline, Snapshot& out) {
    BitReader r(body);
    SnapshotHeader h;
    if (!read_header(r, h)) return false;
    if (h.baseline_tick.has_value() != (baseline != nullptr)) return false;
    if (baseline && baseline->tick != *h.baseline_tick) return false;

    if (baseline) {
        out = *baseline;
    } else {
        out = Snapshot{};
    }
    out.tick = h.tick;
    out.last_cmd = r.read_u32();

    int guard = 0;
    while (r.read_bool()) {
        if (++guard > kMaxEntities || r.failed()) return false;
        const size_t id = r.read_bits(kEntityIdBits);
        EntityRecord& e = out.entities[id];
        const auto op = static_cast<Op>(r.read_bits(2));
        if (op == Op::Remove) {
            e = EntityRecord{};
        } else if (op == Op::Update) {
            if (e.type == EntityType::None) return false;
            const int n = field_count(e.type);
            for (int i = 0; i < n; ++i) {
                if (r.read_bool()) e.f[static_cast<size_t>(i)] = wrap_add(e.f[static_cast<size_t>(i)], r.read_varint());
            }
        } else if (op == Op::Create) {
            const auto type = static_cast<EntityType>(r.read_bits(2));
            const int n = field_count(type);
            if (n == 0) return false;
            e = EntityRecord{};
            e.type = type;
            for (int i = 0; i < n; ++i) e.f[static_cast<size_t>(i)] = r.read_varint();
        } else {
            return false;
        }
    }
    return !r.failed();
}

// ---- Reliable messages -----------------------------------------------------------------------------------

std::vector<uint8_t> encode_welcome(const Welcome& w) {
    BitWriter b;
    b.write_bits(static_cast<uint32_t>(ReliableType::Welcome), 8);
    b.write_bits(w.slot, 8);
    b.write_u32(w.server_tick);
    return b.finish();
}

std::vector<uint8_t> encode_event(const Event& e) {
    BitWriter b;
    b.write_bits(static_cast<uint32_t>(ReliableType::GameEvent), 8);
    b.write_bits(static_cast<uint32_t>(e.type), 8);
    b.write_bits(e.a, 8);
    b.write_bits(e.b, 8);
    b.write_bits(static_cast<uint16_t>(e.x), 16);
    b.write_bits(static_cast<uint16_t>(e.y), 16);
    return b.finish();
}

std::optional<ReliableType> reliable_type(std::span<const uint8_t> msg) {
    if (msg.empty()) return std::nullopt;
    if (msg[0] == static_cast<uint8_t>(ReliableType::Welcome)) return ReliableType::Welcome;
    if (msg[0] == static_cast<uint8_t>(ReliableType::GameEvent)) return ReliableType::GameEvent;
    return std::nullopt;
}

std::optional<Welcome> decode_welcome(std::span<const uint8_t> msg) {
    BitReader r(msg);
    if (r.read_bits(8) != static_cast<uint32_t>(ReliableType::Welcome)) return std::nullopt;
    Welcome w;
    w.slot = static_cast<uint8_t>(r.read_bits(8));
    w.server_tick = r.read_u32();
    if (r.failed() || w.slot >= kMaxPlayers) return std::nullopt;
    return w;
}

std::optional<Event> decode_event(std::span<const uint8_t> msg) {
    BitReader r(msg);
    if (r.read_bits(8) != static_cast<uint32_t>(ReliableType::GameEvent)) return std::nullopt;
    Event e;
    e.type = static_cast<EventType>(r.read_bits(8));
    e.a = static_cast<uint8_t>(r.read_bits(8));
    e.b = static_cast<uint8_t>(r.read_bits(8));
    e.x = static_cast<int16_t>(static_cast<uint16_t>(r.read_bits(16)));
    e.y = static_cast<int16_t>(static_cast<uint16_t>(r.read_bits(16)));
    if (r.failed() || static_cast<uint8_t>(e.type) > static_cast<uint8_t>(EventType::Victory)) return std::nullopt;
    return e;
}

}  // namespace ledang
