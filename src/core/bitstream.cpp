#include "core/bitstream.hpp"

#include <algorithm>
#include <cassert>

namespace ledang {

void BitWriter::write_bits(uint32_t value, int bits) {
    assert(bits >= 0 && bits <= 32);
    if (bits == 0) return;
    if (bits < 32) value &= (1u << bits) - 1u;
    scratch_ |= static_cast<uint64_t>(value) << scratch_bits_;
    scratch_bits_ += bits;
    bits_written_ += static_cast<size_t>(bits);
    while (scratch_bits_ >= 8) {
        bytes_.push_back(static_cast<uint8_t>(scratch_ & 0xFF));
        scratch_ >>= 8;
        scratch_bits_ -= 8;
    }
}

void BitWriter::write_int(int32_t value, int32_t min, int32_t max) {
    assert(min <= max && value >= min && value <= max);
    const uint32_t range = static_cast<uint32_t>(max - min);
    write_bits(static_cast<uint32_t>(value - min), bits_required(range));
}

void BitWriter::write_varint(int32_t value) {
    // zigzag maps 0,-1,1,-2,... to 0,1,2,3,... so small magnitudes stay small
    const uint32_t z = (static_cast<uint32_t>(value) << 1) ^ static_cast<uint32_t>(value >> 31);
    if (z == 0) {
        write_bits(0, 2);
    } else if (z < (1u << 5)) {
        write_bits(1, 2);
        write_bits(z, 5);
    } else if (z < (1u << 12)) {
        write_bits(2, 2);
        write_bits(z, 12);
    } else {
        write_bits(3, 2);
        write_bits(z, 32);
    }
}

void BitWriter::write_bytes(std::span<const uint8_t> bytes) {
    for (uint8_t b : bytes) write_bits(b, 8);
}

const std::vector<uint8_t>& BitWriter::finish() {
    if (scratch_bits_ > 0) {
        bytes_.push_back(static_cast<uint8_t>(scratch_ & 0xFF));
        scratch_ = 0;
        scratch_bits_ = 0;
    }
    return bytes_;
}

uint32_t BitReader::read_bits(int bits) {
    assert(bits >= 0 && bits <= 32);
    if (bits == 0 || failed_) return 0;
    if (bit_pos_ + static_cast<size_t>(bits) > data_.size() * 8) {
        failed_ = true;
        return 0;
    }
    uint64_t value = 0;
    int got = 0;
    while (got < bits) {
        const size_t byte = bit_pos_ / 8;
        const int offset = static_cast<int>(bit_pos_ % 8);
        const int take = std::min(8 - offset, bits - got);
        const uint64_t chunk = (static_cast<uint64_t>(data_[byte]) >> offset) & ((1u << take) - 1u);
        value |= chunk << got;
        got += take;
        bit_pos_ += static_cast<size_t>(take);
    }
    return static_cast<uint32_t>(value);
}

int32_t BitReader::read_int(int32_t min, int32_t max) {
    const uint32_t range = static_cast<uint32_t>(max - min);
    const uint32_t raw = read_bits(bits_required(range));
    if (raw > range) {
        failed_ = true;
        return min;
    }
    return static_cast<int32_t>(static_cast<uint32_t>(min) + raw);
}

int32_t BitReader::read_varint() {
    const uint32_t tag = read_bits(2);
    uint32_t z = 0;
    switch (tag) {
        case 0: z = 0; break;
        case 1: z = read_bits(5); break;
        case 2: z = read_bits(12); break;
        default: z = read_bits(32); break;
    }
    return static_cast<int32_t>((z >> 1) ^ (~(z & 1u) + 1u));
}

void BitReader::read_bytes(std::span<uint8_t> out) {
    for (uint8_t& b : out) b = static_cast<uint8_t>(read_bits(8));
}

}  // namespace ledang
