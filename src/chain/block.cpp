#include "chain/block.hpp"
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace pow_engine::chain {

std::array<uint8_t, 80> BlockHeader::serialize() const noexcept {
    std::array<uint8_t, 80> buffer;
    std::memcpy(buffer.data(), this, sizeof(BlockHeader));
    return buffer;
}

void BlockHeader::get_chunks(std::array<uint8_t, 64>& chunk1, std::array<uint8_t, 16>& chunk2) const noexcept {
    auto raw = serialize();
    std::memcpy(chunk1.data(), raw.data(), 64);
    std::memcpy(chunk2.data(), raw.data() + 64, 16);
}

Target256 nbits_to_target(uint32_t bits) noexcept {
    Target256 target{};
    target.fill(0);

    const uint32_t exponent = (bits >> 24) & 0xff;
    const uint32_t mantissa = bits & 0x007fffff;

    if (exponent == 0 || exponent > 34) {
        return target;
    }

    if (exponent <= 3) {
        const uint32_t val = mantissa >> (8 * (3 - exponent));
        target[31] = static_cast<uint8_t>(val & 0xff);
        target[30] = static_cast<uint8_t>((val >> 8) & 0xff);
        target[29] = static_cast<uint8_t>((val >> 16) & 0xff);
        return target;
    }

    // When exponent > 3, the mantissa is placed at offset (32 - exponent)
    const int start_idx = 32 - static_cast<int>(exponent);
    if (start_idx >= 0 && start_idx < 32) {
        target[start_idx] = static_cast<uint8_t>((mantissa >> 16) & 0xff);
        if (start_idx + 1 < 32) {
            target[start_idx + 1] = static_cast<uint8_t>((mantissa >> 8) & 0xff);
        }
        if (start_idx + 2 < 32) {
            target[start_idx + 2] = static_cast<uint8_t>(mantissa & 0xff);
        }
    }

    return target;
}

uint32_t target_to_nbits(const Target256& target) noexcept {
    int first_idx = -1;
    for (int i = 0; i < 32; ++i) {
        if (target[i] != 0) {
            first_idx = i;
            break;
        }
    }

    if (first_idx == -1) {
        return 0;
    }

    uint32_t size = static_cast<uint32_t>(32 - first_idx);
    uint32_t mantissa = (static_cast<uint32_t>(target[first_idx]) << 16);
    if (first_idx + 1 < 32) {
        mantissa |= (static_cast<uint32_t>(target[first_idx + 1]) << 8);
    }
    if (first_idx + 2 < 32) {
        mantissa |= static_cast<uint32_t>(target[first_idx + 2]);
    }

    // If highest bit is set, shift to prevent sign bit interpretation in Bitcoin compact format
    if (mantissa & 0x00800000) {
        mantissa >>= 8;
        size++;
    }

    return (size << 24) | (mantissa & 0x007fffff);
}

bool check_proof_of_work(const crypto::Hash256& hash_le, uint32_t bits) noexcept {
    Target256 target = nbits_to_target(bits);

    // Convert little-endian hash into big-endian for natural MSB-first comparison
    for (int i = 0; i < 32; ++i) {
        const uint8_t h_byte = hash_le[31 - i];
        const uint8_t t_byte = target[i];
        if (h_byte < t_byte) {
            return true;
        }
        if (h_byte > t_byte) {
            return false;
        }
    }
    return true; // Exactly equal
}

std::string target_to_hex(const Target256& target) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t b : target) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

} // namespace pow_engine::chain
