#include "crypto/sha256.hpp"
#include <cstring>
#include <iomanip>
#include <sstream>

namespace pow_engine::crypto {

const std::array<uint32_t, 64> SHA256_K = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

[[nodiscard]] inline constexpr uint32_t bswap32(uint32_t x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(x);
#elif defined(_MSC_VER)
    return _byteswap_ulong(x);
#else
    return ((x & 0x000000ffU) << 24) |
           ((x & 0x0000ff00U) << 8)  |
           ((x & 0x00ff0000U) >> 8)  |
           ((x & 0xff000000U) >> 24);
#endif
}

// Inline helper functions for SHA-256 primitives
[[nodiscard]] inline constexpr uint32_t rotr(uint32_t x, unsigned int n) noexcept {
    return std::rotr(x, n);
}

[[nodiscard]] inline constexpr uint32_t sigma0(uint32_t x) noexcept {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

[[nodiscard]] inline constexpr uint32_t sigma1(uint32_t x) noexcept {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

[[nodiscard]] inline constexpr uint32_t Sigma0(uint32_t x) noexcept {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

[[nodiscard]] inline constexpr uint32_t Sigma1(uint32_t x) noexcept {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

[[nodiscard]] inline constexpr uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

[[nodiscard]] inline constexpr uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

void sha256_transform_words(Sha256State& state, const uint32_t W_in[16]) noexcept {
    uint32_t W[64];
    for (int t = 0; t < 16; ++t) {
        W[t] = W_in[t];
    }
    for (int t = 16; t < 64; ++t) {
        W[t] = sigma1(W[t - 2]) + W[t - 7] + sigma0(W[t - 15]) + W[t - 16];
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int t = 0; t < 64; ++t) {
        const uint32_t T1 = h + Sigma1(e) + Ch(e, f, g) + SHA256_K[t] + W[t];
        const uint32_t T2 = Sigma0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256_transform(Sha256State& state, const uint8_t block[64]) noexcept {
    uint32_t W[16];
    for (int i = 0; i < 16; ++i) {
        W[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8)  |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    sha256_transform_words(state, W);
}

Hash256 sha256(std::span<const uint8_t> data) noexcept {
    Sha256State state = SHA256_INITIAL_STATE;
    const uint64_t total_bits = static_cast<uint64_t>(data.size()) * 8;
    const size_t num_full_blocks = data.size() / 64;

    for (size_t b = 0; b < num_full_blocks; ++b) {
        sha256_transform(state, data.data() + b * 64);
    }

    // Padding
    const size_t remainder = data.size() % 64;
    std::array<uint8_t, 128> pad_buffer{};
    if (remainder > 0) {
        std::memcpy(pad_buffer.data(), data.data() + num_full_blocks * 64, remainder);
    }
    pad_buffer[remainder] = 0x80;

    const size_t pad_block_count = (remainder < 56) ? 1 : 2;
    const size_t total_pad_bytes = pad_block_count * 64;

    // Append 64-bit length big-endian at the end of the last block
    for (int i = 0; i < 8; ++i) {
        pad_buffer[total_pad_bytes - 1 - i] = static_cast<uint8_t>(total_bits >> (i * 8));
    }

    sha256_transform(state, pad_buffer.data());
    if (pad_block_count == 2) {
        sha256_transform(state, pad_buffer.data() + 64);
    }

    Hash256 result{};
    for (int i = 0; i < 8; ++i) {
        result[i * 4 + 0] = static_cast<uint8_t>(state[i] >> 24);
        result[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
        result[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
        result[i * 4 + 3] = static_cast<uint8_t>(state[i]);
    }
    return result;
}

Hash256 sha256(std::string_view data) noexcept {
    return sha256(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
}

Hash256 sha256d(std::span<const uint8_t> data) noexcept {
    Hash256 first = sha256(data);
    return sha256(std::span<const uint8_t>(first.data(), first.size()));
}

Sha256State compute_midstate(const uint8_t chunk1[64]) noexcept {
    Sha256State state = SHA256_INITIAL_STATE;
    sha256_transform(state, chunk1);
    return state;
}

Hash256 sha256d_with_midstate(const Sha256State& midstate, const uint8_t chunk2[16]) noexcept {
    // 1st round: compress Chunk 2 + padding using Midstate
    Sha256State state1 = midstate;
    uint32_t W1[16]{};
    for (int i = 0; i < 4; ++i) {
        W1[i] = (static_cast<uint32_t>(chunk2[i * 4 + 0]) << 24) |
                (static_cast<uint32_t>(chunk2[i * 4 + 1]) << 16) |
                (static_cast<uint32_t>(chunk2[i * 4 + 2]) << 8)  |
                (static_cast<uint32_t>(chunk2[i * 4 + 3]));
    }
    W1[4] = 0x80000000UL;
    // W1[5..14] = 0
    W1[15] = 640; // 80 bytes * 8 bits = 640 bits
    sha256_transform_words(state1, W1);

    // 2nd round: hash the 32-byte intermediate digest
    // We can directly construct W2 from state1 (in big-endian byte format)
    Sha256State state2 = SHA256_INITIAL_STATE;
    uint32_t W2[16]{};
    for (int i = 0; i < 8; ++i) {
        W2[i] = state1[i];
    }
    W2[8] = 0x80000000UL;
    // W2[9..14] = 0
    W2[15] = 256; // 32 bytes * 8 bits = 256 bits
    sha256_transform_words(state2, W2);

    Hash256 result{};
    for (int i = 0; i < 8; ++i) {
        result[i * 4 + 0] = static_cast<uint8_t>(state2[i] >> 24);
        result[i * 4 + 1] = static_cast<uint8_t>(state2[i] >> 16);
        result[i * 4 + 2] = static_cast<uint8_t>(state2[i] >> 8);
        result[i * 4 + 3] = static_cast<uint8_t>(state2[i]);
    }
    return result;
}

void sha256d_midstate_fast(const Sha256State& midstate,
                           const uint32_t chunk2_w[3],
                           uint32_t nonce_le,
                           Hash256& out_hash) noexcept {
    // 1st round
    Sha256State state1 = midstate;
    uint32_t W1[16];
    W1[0] = chunk2_w[0];
    W1[1] = chunk2_w[1];
    W1[2] = chunk2_w[2];
    W1[3] = bswap32(nonce_le);
    W1[4] = 0x80000000UL;
    W1[5] = 0; W1[6] = 0; W1[7] = 0; W1[8] = 0;
    W1[9] = 0; W1[10] = 0; W1[11] = 0; W1[12] = 0;
    W1[13] = 0; W1[14] = 0;
    W1[15] = 640;
    sha256_transform_words(state1, W1);

    // 2nd round
    Sha256State state2 = SHA256_INITIAL_STATE;
    uint32_t W2[16];
    for (int i = 0; i < 8; ++i) {
        W2[i] = state1[i];
    }
    W2[8] = 0x80000000UL;
    W2[9] = 0; W2[10] = 0; W2[11] = 0; W2[12] = 0;
    W2[13] = 0; W2[14] = 0;
    W2[15] = 256;
    sha256_transform_words(state2, W2);

    for (int i = 0; i < 8; ++i) {
        out_hash[i * 4 + 0] = static_cast<uint8_t>(state2[i] >> 24);
        out_hash[i * 4 + 1] = static_cast<uint8_t>(state2[i] >> 16);
        out_hash[i * 4 + 2] = static_cast<uint8_t>(state2[i] >> 8);
        out_hash[i * 4 + 3] = static_cast<uint8_t>(state2[i]);
    }
}

void sha256d_midstate_fast(const Sha256State& midstate,
                           const uint8_t chunk2_prefix[12],
                           uint32_t nonce_le,
                           Hash256& out_hash) noexcept {
    uint32_t chunk2_w[3];
    for (int i = 0; i < 3; ++i) {
        chunk2_w[i] = (static_cast<uint32_t>(chunk2_prefix[i * 4 + 0]) << 24) |
                      (static_cast<uint32_t>(chunk2_prefix[i * 4 + 1]) << 16) |
                      (static_cast<uint32_t>(chunk2_prefix[i * 4 + 2]) << 8)  |
                      (static_cast<uint32_t>(chunk2_prefix[i * 4 + 3]));
    }
    sha256d_midstate_fast(midstate, chunk2_w, nonce_le, out_hash);
}

std::string to_hex(std::span<const uint8_t> bytes, bool reverse) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    if (!reverse) {
        for (uint8_t b : bytes) {
            oss << std::setw(2) << static_cast<int>(b);
        }
    } else {
        for (auto it = bytes.rbegin(); it != bytes.rend(); ++it) {
            oss << std::setw(2) << static_cast<int>(*it);
        }
    }
    return oss.str();
}

Hash256 from_hex(std::string_view hex, bool reverse) {
    Hash256 res{};
    size_t len = hex.length() / 2;
    if (len > 32) len = 32;

    for (size_t i = 0; i < len; ++i) {
        unsigned int byte_val = 0;
        std::stringstream ss;
        ss << std::hex << hex.substr(i * 2, 2);
        ss >> byte_val;
        if (reverse) {
            res[31 - i] = static_cast<uint8_t>(byte_val);
        } else {
            res[i] = static_cast<uint8_t>(byte_val);
        }
    }
    return res;
}

} // namespace pow_engine::crypto
