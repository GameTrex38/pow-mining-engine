#pragma once

#include "crypto/sha256.hpp"
#include "chain/transaction.hpp"
#include <array>
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>

namespace pow_engine::chain {

// 256-bit target in big-endian byte order (target[0] is MSB, target[31] is LSB)
using Target256 = std::array<uint8_t, 32>;

#pragma pack(push, 1)
struct BlockHeader {
    uint32_t version{1};
    crypto::Hash256 prev_block_hash{};
    crypto::Hash256 merkle_root{};
    uint32_t timestamp{0};
    uint32_t bits{0x1f0fffff}; // Default reasonable testnet target
    uint32_t nonce{0};

    // Serializes the packed 80-byte header into a contiguous byte array
    [[nodiscard]] std::array<uint8_t, 80> serialize() const noexcept;

    // Splits header into Chunk 1 (64 bytes) and Chunk 2 (16 bytes)
    void get_chunks(std::array<uint8_t, 64>& chunk1, std::array<uint8_t, 16>& chunk2) const noexcept;
};
#pragma pack(pop)

static_assert(sizeof(BlockHeader) == 80, "BlockHeader MUST be exactly 80 bytes packed!");

struct Block {
    uint64_t height{0};
    BlockHeader header;
    std::vector<Transaction> transactions;
    std::string hash_hex;
    std::string prev_hash_hex;
    std::string merkle_root_hex;
    double mining_duration_seconds{0.0};
    uint64_t total_hashes{0};
    std::string mined_by_thread;
    std::string timestamp_str;
};

// Target math helpers
Target256 nbits_to_target(uint32_t bits) noexcept;
uint32_t target_to_nbits(const Target256& target) noexcept;
bool check_proof_of_work(const crypto::Hash256& hash, uint32_t bits) noexcept;
std::string target_to_hex(const Target256& target);

} // namespace pow_engine::chain
