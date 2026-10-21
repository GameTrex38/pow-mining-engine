#pragma once

#include "crypto/sha256.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace pow_engine::chain {

struct Transaction {
    std::string txid_hex;
    std::string sender;
    std::string recipient;
    double amount{0.0};
    uint64_t fee_sats{0};
    bool is_coinbase{false};
    uint32_t extra_nonce{0};

    // Computes deterministic 32-byte SHA256d hash of the transaction data
    [[nodiscard]] crypto::Hash256 compute_hash() const noexcept {
        std::string raw;
        if (is_coinbase) {
            raw = "COINBASE:" + recipient + ":" + std::to_string(amount) + ":" + std::to_string(extra_nonce);
        } else {
            raw = "TX:" + sender + "->" + recipient + ":" + std::to_string(amount) + ":" + std::to_string(fee_sats);
        }
        return crypto::sha256d(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(raw.data()), raw.size()));
    }
};

} // namespace pow_engine::chain
