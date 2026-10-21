#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <bit>

namespace pow_engine::crypto {

// Standard 256-bit hash representation (32 bytes)
using Hash256 = std::array<uint8_t, 32>;

// Intermediate 256-bit SHA-256 state (8 x 32-bit words)
using Sha256State = std::array<uint32_t, 8>;

// Standard SHA-256 initial state (first 32 bits of fractional parts of square roots of first 8 primes)
inline constexpr Sha256State SHA256_INITIAL_STATE = {
    0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
    0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL
};

// SHA-256 Round Constants (first 32 bits of fractional parts of cube roots of first 64 primes)
extern const std::array<uint32_t, 64> SHA256_K;

/**
 * @brief Low-level SHA-256 compression function.
 * Transforms an 8-word state using one 64-byte block (16 big-endian 32-bit words).
 */
void sha256_transform(Sha256State& state, const uint8_t block[64]) noexcept;

/**
 * @brief SHA-256 compression function taking 16 pre-parsed 32-bit big-endian words.
 */
void sha256_transform_words(Sha256State& state, const uint32_t W[16]) noexcept;

/**
 * @brief Standard single-pass SHA-256 hash calculation for arbitrary input.
 */
Hash256 sha256(std::span<const uint8_t> data) noexcept;
Hash256 sha256(std::string_view data) noexcept;

/**
 * @brief Standard Bitcoin Double-SHA256 (SHA256(SHA256(data))).
 */
Hash256 sha256d(std::span<const uint8_t> data) noexcept;

/**
 * @brief Precomputes the SHA-256 Midstate for the first 64 bytes of an 80-byte block header.
 * Chunk 1 contains: Version (4B) + PrevBlockHash (32B) + MerkleRoot[0..27] (28B) = 64B.
 * This state is constant while scanning nonces for a given block template.
 */
Sha256State compute_midstate(const uint8_t chunk1[64]) noexcept;

/**
 * @brief Specialized Double-SHA256 for an 80-byte header leveraging a precomputed Midstate.
 * Compresses Chunk 2 (16 bytes: MerkleRoot[28..31], timestamp, bits, nonce) + padding
 * starting from midstate, then performs the second SHA-256 pass (32 bytes + padding).
 * 
 * Performs ONLY 2 block compression transformations instead of 3:
 * Standard SHA256d: Chunk 1 (1) + Chunk 2 (2) + Round 2 (3) = 3 transforms.
 * Midstate SHA256d: Chunk 2 (1) + Round 2 (2) = 2 transforms (~33% compute reduction / ~50% speedup).
 */
Hash256 sha256d_with_midstate(const Sha256State& midstate, const uint8_t chunk2[16]) noexcept;

/**
 * @brief Ultra-fast in-place mining variation: evaluates nonce directly without re-allocating.
 * Writes output hash to out_hash.
 */
void sha256d_midstate_fast(const Sha256State& midstate,
                           const uint8_t chunk2_prefix[12],
                           uint32_t nonce_le,
                           Hash256& out_hash) noexcept;

void sha256d_midstate_fast(const Sha256State& midstate,
                           const uint32_t chunk2_w[3],
                           uint32_t nonce_le,
                           Hash256& out_hash) noexcept;

/**
 * @brief Utility helpers to convert Hash256 to/from hex strings.
 */
std::string to_hex(std::span<const uint8_t> bytes, bool reverse = false);
Hash256 from_hex(std::string_view hex, bool reverse = false);

} // namespace pow_engine::crypto
