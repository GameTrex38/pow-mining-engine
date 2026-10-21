#pragma once

#include "crypto/sha256.hpp"
#include "chain/block.hpp"
#include "chain/merkle.hpp"
#include "miner/dda.hpp"

#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <string>

namespace pow_engine::miner {

struct alignas(64) WorkerSlot {
    std::atomic<uint64_t> hashes_computed{0};
    std::atomic<uint32_t> current_nonce{0};
    std::atomic<double> hashrate_mhs{0.0};
    uint64_t prev_hashes_sample{0};
    std::chrono::steady_clock::time_point last_sample_time;
    // Cache line padding
    char padding[64 - sizeof(std::atomic<uint64_t>) - sizeof(std::atomic<uint32_t>) -
                 sizeof(std::atomic<double>) - sizeof(uint64_t) - sizeof(std::chrono::steady_clock::time_point)];
};

struct BenchmarkResult {
    double single_naive_mhs{0.0};
    double single_midstate_mhs{0.0};
    double multi_midstate_mhs{0.0};
    double midstate_speedup_percent{0.0};
    double multicore_scaling_factor{0.0};
    size_t thread_count{0};
    std::string summary;
};

struct MiningPoolTelemetry {
    uint64_t current_height{0};
    uint32_t current_bits{0};
    std::string bits_hex;
    std::string current_target_hex;
    bool midstate_enabled{true};
    size_t active_threads{0};
    double total_hashrate_mhs{0.0};
    uint64_t total_hashes_all_time{0};
    std::vector<double> per_thread_mhs;
    std::vector<uint32_t> per_thread_nonces;
    std::vector<chain::Block> recent_blocks;
    std::vector<chain::MerkleNodeInfo> current_merkle_nodes;
    std::string active_header_hex;
    std::string active_midstate_hex;
    bool is_mining{false};
    bool is_benchmarking{false};
};

class MinerPool {
public:
    explicit MinerPool(size_t thread_count = 0, uint32_t initial_bits = 0x1f0fffff);
    ~MinerPool();

    void start();
    void stop();

    // Configuration modifiers
    void set_thread_count(size_t threads);
    void set_midstate_enabled(bool enabled);
    void set_difficulty(uint32_t bits);
    void add_transaction(const chain::Transaction& tx);

    [[nodiscard]] MiningPoolTelemetry get_telemetry();
    BenchmarkResult run_benchmark(int duration_sec = 2);

    [[nodiscard]] const std::vector<chain::Block>& get_blockchain() const;

private:
    void coordinator_loop();
    void worker_loop(size_t thread_id, size_t total_workers, uint32_t start_nonce, uint32_t stride);
    void prepare_next_block_template();

    std::atomic<bool> running_{false};
    std::atomic<bool> is_mining_{false};
    std::atomic<bool> is_benchmarking_{false};
    std::atomic<bool> stop_current_block_{false};
    std::atomic<bool> block_found_{false};
    std::atomic<bool> use_midstate_{true};
    std::atomic<size_t> configured_threads_{0};

    DifficultyController dda_;
    std::vector<std::jthread> worker_threads_;
    std::jthread coordinator_thread_;

    mutable std::mutex pool_mutex_;
    std::vector<std::unique_ptr<WorkerSlot>> telemetry_slots_;

    // Blockchain state
    std::vector<chain::Block> blockchain_;
    std::vector<chain::Transaction> mempool_;
    chain::BlockHeader current_template_header_;
    crypto::Sha256State current_midstate_;
    chain::MerkleTree current_merkle_tree_;
    uint32_t current_extra_nonce_{0};
    std::chrono::steady_clock::time_point block_start_time_;

    // Winning block data slot
    std::atomic<bool> winner_recorded_{false};
    chain::BlockHeader winning_header_;
    crypto::Hash256 winning_hash_;
    size_t winning_thread_id_{0};
};

} // namespace pow_engine::miner
