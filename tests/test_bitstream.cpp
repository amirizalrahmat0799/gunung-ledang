#include <limits>
#include <random>

#include "core/bitstream.hpp"
#include "core/sequence.hpp"
#include "test.hpp"

using namespace ledang;

TEST(bitstream_round_trip) {
    BitWriter w;
    w.write_bits(5, 3);
    w.write_bool(true);
    w.write_int(-7, -10, 10);
    w.write_u32(0xDEADBEEF);
    w.write_int(1000, 0, 1000);
    const uint8_t raw[3] = {1, 2, 3};
    w.write_bytes(raw);
    for (int32_t v : {0, 1, -1, 7, -8, 300, -300, 100000, std::numeric_limits<int32_t>::max(),
                      std::numeric_limits<int32_t>::min()}) {
        w.write_varint(v);
    }
    const auto bytes = w.finish();

    BitReader r(bytes);
    CHECK_EQ(r.read_bits(3), 5u);
    CHECK(r.read_bool());
    CHECK_EQ(r.read_int(-10, 10), -7);
    CHECK_EQ(r.read_u32(), 0xDEADBEEFu);
    CHECK_EQ(r.read_int(0, 1000), 1000);
    uint8_t out[3] = {};
    r.read_bytes(out);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);
    for (int32_t v : {0, 1, -1, 7, -8, 300, -300, 100000, std::numeric_limits<int32_t>::max(),
                      std::numeric_limits<int32_t>::min()}) {
        CHECK_EQ(r.read_varint(), v);
    }
    CHECK(!r.failed());
}

TEST(bitstream_varint_is_compact) {
    BitWriter w;
    for (int i = 0; i < 8; ++i) w.write_varint(0);
    CHECK_EQ(w.bits_written(), size_t{16});  // 2 bits per zero
    BitWriter w2;
    w2.write_varint(5);
    CHECK_EQ(w2.bits_written(), size_t{7});
    BitWriter w3;
    w3.write_varint(1152);  // a running player's x over one snapshot
    CHECK_EQ(w3.bits_written(), size_t{14});
}

TEST(bitstream_overrun_fails_safely) {
    const uint8_t data[2] = {0xFF, 0xFF};
    BitReader r(data);
    r.read_bits(12);
    CHECK(!r.failed());
    CHECK_EQ(r.read_bits(8), 0u);
    CHECK(r.failed());
    CHECK_EQ(r.read_u32(), 0u);
}

TEST(bitstream_random_fuzz) {
    std::mt19937 rng(7);
    for (int round = 0; round < 200; ++round) {
        std::vector<std::pair<uint32_t, int>> values;
        BitWriter w;
        const int n = static_cast<int>(rng() % 50);
        for (int i = 0; i < n; ++i) {
            const int bits = 1 + static_cast<int>(rng() % 32);
            const uint32_t v = static_cast<uint32_t>(bits == 32 ? rng() : rng() & ((1u << bits) - 1));
            values.emplace_back(v, bits);
            w.write_bits(v, bits);
        }
        const auto bytes = w.finish();
        BitReader r(bytes);
        for (auto [v, bits] : values) CHECK_EQ(r.read_bits(bits), v);
        CHECK(!r.failed());

        // Garbage must never crash the reader.
        std::vector<uint8_t> junk(rng() % 64);
        for (auto& b : junk) b = static_cast<uint8_t>(rng());
        BitReader jr(junk);
        for (int i = 0; i < 40; ++i) jr.read_varint();
    }
}

TEST(sequence_wraparound) {
    CHECK(seq_newer(1, 0));
    CHECK(!seq_newer(0, 1));
    CHECK(seq_newer(0, 65535));
    CHECK(seq_newer(10, 65530));
    CHECK(!seq_newer(65530, 10));
    CHECK_EQ(seq_diff(2, 65534), 4);
}
