#pragma once

#include "crypto/sha256.hpp"
#include "chain/transaction.hpp"
#include <vector>
#include <string>

namespace pow_engine::chain {

struct MerkleNodeInfo {
    std::string hash_hex;
    int level{0};
    int index{0};
    std::string label;
};

class MerkleTree {
public:
    MerkleTree() = default;
    explicit MerkleTree(const std::vector<Transaction>& transactions);

    [[nodiscard]] crypto::Hash256 get_root() const noexcept;
    [[nodiscard]] std::string get_root_hex() const;
    [[nodiscard]] const std::vector<std::vector<crypto::Hash256>>& get_levels() const noexcept {
        return levels_;
    }

    // Returns flat list of nodes suitable for Web UI visualization
    [[nodiscard]] std::vector<MerkleNodeInfo> get_tree_nodes_for_ui() const;

private:
    std::vector<std::vector<crypto::Hash256>> levels_;
    std::vector<std::string> tx_labels_;
};

} // namespace pow_engine::chain
