#pragma once
// Bit-level packing for network messages: values are written with exactly as many bits as their range needs.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ledang {

/// Number of bits needed to represent values in [0, range].
constexpr int bits_required(uint32_t range) {
    int bits = 0;
    while (range > 0) {
        ++bits;
        range >>= 1;
    }
    return bits;
}

class BitWriter {
public:
    void write_bits(uint32_t value, int bits);
    void write_bool(bool value) { write_bits(value ? 1u : 0u, 1); }
    /// Writes a value in [min, max] using bits_required(max - min) bits.
    void write_int(int32_t value, int32_t min, int32_t max);
    /// Writes a small-magnitude signed value compactly (zigzag + 2-bit length tag): 0 costs 2 bits, +-15 costs 7,
    /// +-2047 costs 14, anything else 34.
    void write_varint(int32_t value);
    void write_u32(uint32_t value) { write_bits(value, 32); }
    void write_bytes(std::span<const uint8_t> bytes);

    /// Flushes the scratch word; returns the packed bytes.
    const std::vector<uint8_t>& finish();
    size_t bits_written() const { return bits_written_; }
    size_t bytes_written() const { return (bits_written_ + 7) / 8; }

private:
    std::vector<uint8_t> bytes_;
    uint64_t scratch_ = 0;
    int scratch_bits_ = 0;
    size_t bits_written_ = 0;
};

class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> data) : data_(data) {}

    uint32_t read_bits(int bits);
    bool read_bool() { return read_bits(1) != 0; }
    int32_t read_int(int32_t min, int32_t max);
    int32_t read_varint();
    uint32_t read_u32() { return read_bits(32); }
    void read_bytes(std::span<uint8_t> out);

    /// True once a read went past the end of the data; every later read returns 0.
    bool failed() const { return failed_; }
    size_t bits_remaining() const { return data_.size() * 8 - bit_pos_; }

private:
    std::span<const uint8_t> data_;
    size_t bit_pos_ = 0;
    bool failed_ = false;
};

}  // namespace ledang
