#include "chain/merkle.hpp"
#include <array>

namespace pow_engine::chain {

MerkleTree::MerkleTree(const std::vector<Transaction>& transactions) {
    if (transactions.empty()) {
        levels_.push_back({crypto::Hash256{}});
        return;
    }

    std::vector<crypto::Hash256> current_level;
    current_level.reserve(transactions.size());
    tx_labels_.reserve(transactions.size());

    for (const auto& tx : transactions) {
        current_level.push_back(tx.compute_hash());
        if (tx.is_coinbase) {
            tx_labels_.push_back("Coinbase: " + tx.recipient + " (" + std::to_string(tx.amount) + " BTC)");
        } else {
            tx_labels_.push_back(tx.sender + " -> " + tx.recipient + " (" + std::to_string(tx.amount) + " BTC)");
        }
    }

    levels_.push_back(current_level);

    while (levels_.back().size() > 1) {
        const auto& prev = levels_.back();
        std::vector<crypto::Hash256> next_level;
        next_level.reserve((prev.size() + 1) / 2);

        for (size_t i = 0; i < prev.size(); i += 2) {
            const auto& left = prev[i];
            const auto& right = (i + 1 < prev.size()) ? prev[i + 1] : left; // Duplicate odd element

            // Concatenate left || right (64 bytes total)
            std::array<uint8_t, 64> combined;
            std::copy(left.begin(), left.end(), combined.begin());
            std::copy(right.begin(), right.end(), combined.begin() + 32);

            next_level.push_back(crypto::sha256d(combined));
        }

        levels_.push_back(std::move(next_level));
    }
}

crypto::Hash256 MerkleTree::get_root() const noexcept {
    if (levels_.empty() || levels_.back().empty()) {
        return crypto::Hash256{};
    }
    return levels_.back().front();
}

std::string MerkleTree::get_root_hex() const {
    auto root = get_root();
    return crypto::to_hex(root, true); // Display in standard reversed hex
}

std::vector<MerkleNodeInfo> MerkleTree::get_tree_nodes_for_ui() const {
    std::vector<MerkleNodeInfo> result;
    for (size_t lvl = 0; lvl < levels_.size(); ++lvl) {
        const auto& nodes = levels_[lvl];
        for (size_t idx = 0; idx < nodes.size(); ++idx) {
            MerkleNodeInfo info;
            info.level = static_cast<int>(lvl);
            info.index = static_cast<int>(idx);
            info.hash_hex = crypto::to_hex(nodes[idx], true);
            if (lvl == 0 && idx < tx_labels_.size()) {
                info.label = tx_labels_[idx];
            } else if (lvl == levels_.size() - 1) {
                info.label = "Merkle Root";
            } else {
                info.label = "Internal Hash (L" + std::to_string(lvl) + ", N" + std::to_string(idx) + ")";
            }
            result.push_back(std::move(info));
        }
    }
    return result;
}

} // namespace pow_engine::chain
