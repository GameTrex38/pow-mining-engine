#include "miner/miner_pool.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace pow_engine::miner {

MinerPool::MinerPool(size_t thread_count, uint32_t initial_bits)
    : dda_(initial_bits, 4.0, 5) {
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 4;
    }
    configured_threads_ = thread_count;

    for (size_t i = 0; i < 64; ++i) { // Pre-allocate up to 64 thread telemetry slots
        telemetry_slots_.push_back(std::make_unique<WorkerSlot>());
        telemetry_slots_.back()->last_sample_time = std::chrono::steady_clock::now();
    }

    // Initialize Genesis Block in history
    chain::Transaction genesis_coinbase;
    genesis_coinbase.is_coinbase = true;
    genesis_coinbase.recipient = "Satoshi_Genesis_Pool";
    genesis_coinbase.amount = 50.0;
    genesis_coinbase.txid_hex = crypto::to_hex(genesis_coinbase.compute_hash(), true);

    chain::MerkleTree genesis_merkle({genesis_coinbase});
    chain::Block genesis_block;
    genesis_block.height = 0;
    genesis_block.transactions = {genesis_coinbase};
    genesis_block.header.version = 1;
    genesis_block.header.prev_block_hash.fill(0);
    genesis_block.header.merkle_root = genesis_merkle.get_root();
    genesis_block.header.timestamp = 1231006505; // Bitcoin Genesis timestamp
    genesis_block.header.bits = initial_bits;
    genesis_block.header.nonce = 2083236893;
    genesis_block.hash_hex = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
    genesis_block.prev_hash_hex = "0000000000000000000000000000000000000000000000000000000000000000";
    genesis_block.merkle_root_hex = genesis_merkle.get_root_hex();
    genesis_block.mining_duration_seconds = 0.01;
    genesis_block.total_hashes = 1;
    genesis_block.mined_by_thread = "Genesis Core";
    genesis_block.timestamp_str = "2009-01-03 18:15:05 UTC";

    blockchain_.push_back(genesis_block);

    prepare_next_block_template();
}

MinerPool::~MinerPool() {
    stop();
}

void MinerPool::start() {
    if (running_.exchange(true)) {
        return;
    }
    is_mining_ = true;
    coordinator_thread_ = std::jthread(&MinerPool::coordinator_loop, this);
}

void MinerPool::stop() {
    running_ = false;
    stop_current_block_ = true;
    block_found_ = true;
    if (coordinator_thread_.joinable()) {
        coordinator_thread_.request_stop();
        coordinator_thread_.join();
    }
    for (auto& w : worker_threads_) {
        if (w.joinable()) {
            w.request_stop();
            w.join();
        }
    }
    worker_threads_.clear();
    is_mining_ = false;
}

void MinerPool::set_thread_count(size_t threads) {
    if (threads == 0) threads = 1;
    if (threads > 64) threads = 64;
    configured_threads_ = threads;
    stop_current_block_ = true; // Signal current worker cohort to finish early and restart with new count
}

void MinerPool::set_midstate_enabled(bool enabled) {
    use_midstate_ = enabled;
}

void MinerPool::set_difficulty(uint32_t bits) {
    std::lock_guard lock(pool_mutex_);
    dda_.set_bits(bits);
    current_template_header_.bits = bits;
    stop_current_block_ = true;
}

void MinerPool::add_transaction(const chain::Transaction& tx) {
    std::lock_guard lock(pool_mutex_);
    mempool_.push_back(tx);
    prepare_next_block_template();
    stop_current_block_ = true;
}

const std::vector<chain::Block>& MinerPool::get_blockchain() const {
    std::lock_guard lock(pool_mutex_);
    return blockchain_;
}

void MinerPool::prepare_next_block_template() {
    // Coinbase reward transaction
    chain::Transaction coinbase;
    coinbase.is_coinbase = true;
    coinbase.recipient = "PoW_WorkerPool_Miner_01";
    coinbase.amount = 6.25;
    coinbase.extra_nonce = current_extra_nonce_++;
    coinbase.txid_hex = crypto::to_hex(coinbase.compute_hash(), true);

    std::vector<chain::Transaction> txs;
    txs.push_back(coinbase);

    // If mempool has pending transactions, attach them
    if (!mempool_.empty()) {
        txs.insert(txs.end(), mempool_.begin(), mempool_.end());
    } else {
        // Generate simulated dynamic financial transactions
        chain::Transaction tx1{"", "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", "3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy", 1.45, 12000, false, 0};
        tx1.txid_hex = crypto::to_hex(tx1.compute_hash(), true);
        txs.push_back(tx1);

        chain::Transaction tx2{"", "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq", "bc1qc7g0ncwhaq2g4um9tkDensity7zfx9x", 0.825, 8500, false, 0};
        tx2.txid_hex = crypto::to_hex(tx2.compute_hash(), true);
        txs.push_back(tx2);
    }

    current_merkle_tree_ = chain::MerkleTree(txs);

    current_template_header_.version = 2;
    if (!blockchain_.empty()) {
        current_template_header_.prev_block_hash = crypto::from_hex(blockchain_.back().hash_hex, true);
    } else {
        current_template_header_.prev_block_hash.fill(0);
    }
    current_template_header_.merkle_root = current_merkle_tree_.get_root();
    current_template_header_.timestamp = static_cast<uint32_t>(std::time(nullptr));
    current_template_header_.bits = dda_.get_current_bits();
    current_template_header_.nonce = 0;

    std::array<uint8_t, 64> chunk1;
    std::array<uint8_t, 16> chunk2;
    current_template_header_.get_chunks(chunk1, chunk2);
    current_midstate_ = crypto::compute_midstate(chunk1.data());

    block_found_ = false;
    winner_recorded_ = false;
    stop_current_block_ = false;
    block_start_time_ = std::chrono::steady_clock::now();
}

void MinerPool::worker_loop(size_t thread_id, [[maybe_unused]] size_t total_workers, uint32_t start_nonce, uint32_t stride) {
    chain::BlockHeader header_copy;
    crypto::Sha256State midstate_copy;
    bool midstate_mode = false;
    uint32_t target_bits = 0;

    {
        std::lock_guard lock(pool_mutex_);
        header_copy = current_template_header_;
        midstate_copy = current_midstate_;
        midstate_mode = use_midstate_.load();
        target_bits = header_copy.bits;
    }

    uint8_t chunk2_prefix[12];
    std::array<uint8_t, 64> c1;
    std::array<uint8_t, 16> c2;
    header_copy.get_chunks(c1, c2);
    std::memcpy(chunk2_prefix, c2.data(), 12);

    uint32_t chunk2_w[3];
    for (int i = 0; i < 3; ++i) {
        chunk2_w[i] = (static_cast<uint32_t>(chunk2_prefix[i * 4 + 0]) << 24) |
                      (static_cast<uint32_t>(chunk2_prefix[i * 4 + 1]) << 16) |
                      (static_cast<uint32_t>(chunk2_prefix[i * 4 + 2]) << 8)  |
                      (static_cast<uint32_t>(chunk2_prefix[i * 4 + 3]));
    }

    uint32_t nonce = start_nonce;
    uint32_t batch_hashes = 0;
    crypto::Hash256 candidate_hash;

    auto raw_header_naive = header_copy.serialize();

    while (!stop_current_block_ && !block_found_ && running_) {
        if (midstate_mode) {
            crypto::sha256d_midstate_fast(midstate_copy, chunk2_w, nonce, candidate_hash);
        } else {
            *reinterpret_cast<uint32_t*>(raw_header_naive.data() + 76) = nonce;
            candidate_hash = crypto::sha256d(raw_header_naive);
        }

        batch_hashes++;
        if (batch_hashes >= 4096) {
            telemetry_slots_[thread_id]->hashes_computed.fetch_add(batch_hashes, std::memory_order_relaxed);
            telemetry_slots_[thread_id]->current_nonce.store(nonce, std::memory_order_relaxed);
            batch_hashes = 0;
        }

        if (chain::check_proof_of_work(candidate_hash, target_bits)) {
            if (!block_found_.exchange(true)) {
                // Winning thread records victory
                winning_header_ = header_copy;
                winning_header_.nonce = nonce;
                winning_hash_ = candidate_hash;
                winning_thread_id_ = thread_id;
                winner_recorded_ = true;
            }
            break;
        }

        nonce += stride;
    }

    if (batch_hashes > 0) {
        telemetry_slots_[thread_id]->hashes_computed.fetch_add(batch_hashes, std::memory_order_relaxed);
        telemetry_slots_[thread_id]->current_nonce.store(nonce, std::memory_order_relaxed);
    }
}

void MinerPool::coordinator_loop() {
    while (running_) {
        if (is_benchmarking_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        size_t active_workers = configured_threads_.load();
        stop_current_block_ = false;

        // Launch worker cohort with nonce striding
        worker_threads_.clear();
        for (size_t i = 0; i < active_workers; ++i) {
            worker_threads_.emplace_back(
                &MinerPool::worker_loop, this, i, active_workers, static_cast<uint32_t>(i), static_cast<uint32_t>(active_workers));
        }

        // Monitor while mining current block
        auto last_telemetry_time = std::chrono::steady_clock::now();
        while (!block_found_ && !stop_current_block_ && running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - last_telemetry_time).count();
            if (dt >= 0.25) {
                last_telemetry_time = now;
                for (size_t i = 0; i < active_workers; ++i) {
                    uint64_t current_h = telemetry_slots_[i]->hashes_computed.load(std::memory_order_relaxed);
                    uint64_t diff = current_h - telemetry_slots_[i]->prev_hashes_sample;
                    telemetry_slots_[i]->prev_hashes_sample = current_h;
                    double rate = (static_cast<double>(diff) / dt) / 1'000'000.0;
                    telemetry_slots_[i]->hashrate_mhs.store(rate, std::memory_order_relaxed);
                }
            }
        }

        // Wait for workers to finish current block
        for (auto& w : worker_threads_) {
            if (w.joinable()) {
                w.join();
            }
        }
        worker_threads_.clear();

        if (winner_recorded_) {
            std::lock_guard lock(pool_mutex_);
            auto end_time = std::chrono::steady_clock::now();
            double duration_sec = std::chrono::duration<double>(end_time - block_start_time_).count();
            if (duration_sec < 0.001) duration_sec = 0.001;

            uint64_t block_hashes = 0;
            for (size_t i = 0; i < active_workers; ++i) {
                block_hashes += telemetry_slots_[i]->hashes_computed.load(std::memory_order_relaxed);
            }

            chain::Block new_block;
            new_block.height = blockchain_.size();
            new_block.header = winning_header_;
            new_block.hash_hex = crypto::to_hex(winning_hash_, true);
            new_block.prev_hash_hex = crypto::to_hex(winning_header_.prev_block_hash, true);
            new_block.merkle_root_hex = crypto::to_hex(winning_header_.merkle_root, true);
            new_block.mining_duration_seconds = duration_sec;
            new_block.total_hashes = block_hashes;
            new_block.mined_by_thread = "Core Worker #" + std::to_string(winning_thread_id_);

            auto now_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::stringstream ss;
            ss << std::put_time(std::gmtime(&now_time_t), "%Y-%m-%d %H:%M:%S UTC");
            new_block.timestamp_str = ss.str();

            blockchain_.push_back(new_block);

            // Execute Dynamic Difficulty Adjustment
            dda_.maybe_adjust(blockchain_);

            // Clear mempool and prepare next block
            mempool_.clear();
            prepare_next_block_template();
        } else if (stop_current_block_) {
            // Configuration or difficulty changed, recreate template
            std::lock_guard lock(pool_mutex_);
            prepare_next_block_template();
        }
    }
}

MiningPoolTelemetry MinerPool::get_telemetry() {
    MiningPoolTelemetry t;
    std::lock_guard lock(pool_mutex_);

    t.current_height = blockchain_.size();
    t.current_bits = current_template_header_.bits;
    {
        std::ostringstream ss_bits;
        ss_bits << std::hex << std::setw(8) << std::setfill('0') << t.current_bits;
        t.bits_hex = ss_bits.str();
    }
    t.current_target_hex = chain::target_to_hex(chain::nbits_to_target(t.current_bits));
    t.midstate_enabled = use_midstate_.load();
    t.active_threads = configured_threads_.load();
    t.is_mining = is_mining_.load();
    t.is_benchmarking = is_benchmarking_.load();

    auto raw_header = current_template_header_.serialize();
    t.active_header_hex = crypto::to_hex(raw_header);
    
    std::ostringstream ss_mid;
    for (uint32_t word : current_midstate_) {
        ss_mid << std::hex << std::setw(8) << std::setfill('0') << word;
    }
    t.active_midstate_hex = ss_mid.str();

    t.total_hashrate_mhs = 0.0;
    t.total_hashes_all_time = 0;
    t.per_thread_mhs.resize(t.active_threads, 0.0);
    t.per_thread_nonces.resize(t.active_threads, 0);

    for (size_t i = 0; i < t.active_threads; ++i) {
        double mhs = telemetry_slots_[i]->hashrate_mhs.load(std::memory_order_relaxed);
        t.per_thread_mhs[i] = mhs;
        t.total_hashrate_mhs += mhs;
        t.per_thread_nonces[i] = telemetry_slots_[i]->current_nonce.load(std::memory_order_relaxed);
        t.total_hashes_all_time += telemetry_slots_[i]->hashes_computed.load(std::memory_order_relaxed);
    }

    t.current_merkle_nodes = current_merkle_tree_.get_tree_nodes_for_ui();

    // Return last 10 blocks (newest first)
    size_t count = std::min<size_t>(blockchain_.size(), 10);
    for (size_t i = 0; i < count; ++i) {
        t.recent_blocks.push_back(blockchain_[blockchain_.size() - 1 - i]);
    }

    return t;
}

BenchmarkResult MinerPool::run_benchmark(int duration_sec) {
    is_benchmarking_ = true;
    stop_current_block_ = true;

    // Wait for active mining workers to stop
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    BenchmarkResult res;
    res.thread_count = configured_threads_.load();
    if (res.thread_count == 0) res.thread_count = 4;

    chain::BlockHeader test_header = current_template_header_;
    std::array<uint8_t, 64> c1;
    std::array<uint8_t, 16> c2;
    test_header.get_chunks(c1, c2);
    auto midstate = crypto::compute_midstate(c1.data());
    uint8_t chunk2_prefix[12];
    std::memcpy(chunk2_prefix, c2.data(), 12);
    uint32_t chunk2_w[3];
    for (int i = 0; i < 3; ++i) {
        chunk2_w[i] = (static_cast<uint32_t>(chunk2_prefix[i * 4 + 0]) << 24) |
                      (static_cast<uint32_t>(chunk2_prefix[i * 4 + 1]) << 16) |
                      (static_cast<uint32_t>(chunk2_prefix[i * 4 + 2]) << 8)  |
                      (static_cast<uint32_t>(chunk2_prefix[i * 4 + 3]));
    }

    const uint64_t N = static_cast<uint64_t>(std::max(1, duration_sec)) * 800'000ULL;

    // 1. Single-Threaded Naive SHA-256d
    {
        auto raw_bytes = test_header.serialize();
        crypto::Hash256 h;
        auto start = std::chrono::high_resolution_clock::now();
        for (uint64_t i = 0; i < N; ++i) {
            *reinterpret_cast<uint32_t*>(raw_bytes.data() + 76) = static_cast<uint32_t>(i);
            h = crypto::sha256d(raw_bytes);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(end - start).count();
        res.single_naive_mhs = (static_cast<double>(N) / dt) / 1'000'000.0;
    }

    // 2. Single-Threaded Midstate SHA-256d
    {
        crypto::Hash256 h;
        auto start = std::chrono::high_resolution_clock::now();
        for (uint64_t i = 0; i < N; ++i) {
            crypto::sha256d_midstate_fast(midstate, chunk2_w, static_cast<uint32_t>(i), h);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(end - start).count();
        res.single_midstate_mhs = (static_cast<double>(N) / dt) / 1'000'000.0;
    }

    // 3. Multi-Threaded Midstate SHA-256d
    {
        const uint64_t n_per_thread = N;
        std::vector<std::jthread> threads;
        auto start = std::chrono::high_resolution_clock::now();

        for (size_t t = 0; t < res.thread_count; ++t) {
            threads.emplace_back([=, &midstate, &chunk2_w]() {
                crypto::Hash256 h;
                const uint32_t start_nonce = static_cast<uint32_t>(t * n_per_thread);
                for (uint64_t i = 0; i < n_per_thread; ++i) {
                    crypto::sha256d_midstate_fast(midstate, chunk2_w, start_nonce + static_cast<uint32_t>(i), h);
                }
            });
        }
        for (auto& t : threads) t.join();
        auto end = std::chrono::high_resolution_clock::now();

        double dt = std::chrono::duration<double>(end - start).count();
        uint64_t total_hashes = n_per_thread * res.thread_count;
        res.multi_midstate_mhs = (static_cast<double>(total_hashes) / dt) / 1'000'000.0;
    }

    if (res.single_naive_mhs > 0.0) {
        res.midstate_speedup_percent = ((res.single_midstate_mhs / res.single_naive_mhs) - 1.0) * 100.0;
    }
    if (res.single_midstate_mhs > 0.0) {
        res.multicore_scaling_factor = res.multi_midstate_mhs / res.single_midstate_mhs;
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "Midstate Caching Speedup: +" << res.midstate_speedup_percent << "% | "
       << "Multi-Core Scaling (" << res.thread_count << " threads): " << res.multicore_scaling_factor << "x | "
       << "Peak: " << res.multi_midstate_mhs << " MH/s";
    res.summary = ss.str();

    is_benchmarking_ = false;
    stop_current_block_ = false;
    return res;
}

} // namespace pow_engine::miner
