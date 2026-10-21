#include "miner/miner_pool.hpp"
#include "server/http_server.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <iomanip>
#include <cstdlib>

namespace {
std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int) {
    g_shutdown_requested.store(true);
}
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    size_t thread_count = 0; // 0 = auto-detect hardware cores
    int port = 8080;

    // Check for cloud environment variable $PORT (used by Render, Railway, Cloud Run, Heroku)
    if (const char* env_p = std::getenv("PORT")) {
        try {
            port = std::stoi(env_p);
        } catch (...) {}
    }

    bool cli_only = false;
    bool initial_midstate = true;
    bool run_bench_and_exit = false;
    uint32_t initial_difficulty = 0x1f0fffff; // Testnet difficulty for snappy interactive mining

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            thread_count = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--cli-only") {
            cli_only = true;
        } else if (arg == "--no-midstate") {
            initial_midstate = false;
        } else if (arg == "--difficulty" && i + 1 < argc) {
            initial_difficulty = static_cast<uint32_t>(std::stoul(argv[++i], nullptr, 16));
        } else if (arg == "--benchmark") {
            run_bench_and_exit = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "========================================================================\n"
                      << "  Low-Level Multi-Threaded PoW Mining Engine & Telemetry Visualizer\n"
                      << "========================================================================\n\n"
                      << "Usage: pow_engine [options]\n\n"
                      << "Options:\n"
                      << "  --threads <N>       Number of CPU mining threads (default: hardware cores)\n"
                      << "  --port <port>       Port for Web Telemetry UI (default: 8080)\n"
                      << "  --cli-only          Run rich ANSI terminal monitor without starting Web UI\n"
                      << "  --no-midstate       Start with SHA-256 Midstate Caching disabled\n"
                      << "  --difficulty <hex>  Set initial nBits difficulty (default: 1f0fffff)\n"
                      << "  --benchmark         Execute quick 3-phase throughput benchmark & exit\n"
                      << "  --help, -h          Display this help message\n\n";
            return 0;
        }
    }

    std::cout << "\033[1;36m"
              << "========================================================================\n"
              << "  PoW Mining Engine (C++20 Native Core, Zero External Crypto Deps)\n"
              << "  Midstate Caching Optimization • Lock-Free Multi-Threaded Striding\n"
              << "========================================================================\n"
              << "\033[0m\n";

    auto pool = std::make_shared<pow_engine::miner::MinerPool>(thread_count, initial_difficulty);
    pool->set_midstate_enabled(initial_midstate);

    if (run_bench_and_exit) {
        std::cout << "\033[1;33m[*] Running PoW Engine Crypto Benchmark Suite...\033[0m\n";
        auto bm = pool->run_benchmark(3);
        std::cout << "\n------------------------------------------------------------------------\n"
                  << "  Benchmark Execution Report:\n"
                  << "------------------------------------------------------------------------\n"
                  << "  • 1-Thread Naive SHA256d:    " << std::fixed << std::setprecision(2) << bm.single_naive_mhs << " MH/s (3 transforms/nonce)\n"
                  << "  • 1-Thread Midstate SHA256d: " << bm.single_midstate_mhs << " MH/s (2 transforms/nonce)\n"
                  << "  • Midstate Speedup Factor:   \033[1;32m+" << bm.midstate_speedup_percent << "%\033[0m\n"
                  << "  • " << bm.thread_count << "-Thread Midstate Peak:   \033[1;36m" << bm.multi_midstate_mhs << " MH/s\033[0m\n"
                  << "  • Multi-Core Scaling Factor: " << bm.multicore_scaling_factor << "x\n"
                  << "------------------------------------------------------------------------\n";
        return 0;
    }

    std::unique_ptr<pow_engine::server::HttpServer> server;
    if (!cli_only) {
        server = std::make_unique<pow_engine::server::HttpServer>(pool, port, "web");
        if (!server->start()) {
            std::cerr << "\033[1;31m[!] Warning: Failed to bind HTTP server to port " << port << ". Falling back to CLI mode.\033[0m\n";
            cli_only = true;
        }
    }

    // Start mining coordinator
    pool->start();

    std::cout << "\033[1;32m[+] Mining engine running with " << pool->get_telemetry().active_threads << " worker threads.\033[0m\n";
    if (!cli_only) {
        std::cout << "\033[1;35m[+] Dashboard live at: http://localhost:" << port << "/\033[0m\n";
    }
    std::cout << "[*] Press Ctrl+C to terminate cleanly.\n\n";

    uint64_t last_height = 0;

    // Monitoring loop
    while (!g_shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(cli_only ? 500 : 2000));
        auto t = pool->get_telemetry();

        if (cli_only) {
            // ANSI Rich Terminal Dashboard
            std::cout << "\033[2J\033[H"; // Clear screen & move home
            std::cout << "\033[1;34m=== Low-Level PoW Engine Telemetry (CLI Dashboard) ===\033[0m\n";
            std::cout << "Height: \033[1;32m" << t.current_height << "\033[0m | "
                      << "Difficulty: \033[1;33m0x" << t.bits_hex << "\033[0m | "
                      << "Midstate: " << (t.midstate_enabled ? "\033[1;32mENABLED\033[0m" : "\033[1;31mDISABLED\033[0m") << "\n";
            std::cout << "Total Hashrate: \033[1;36m" << std::fixed << std::setprecision(2) << t.total_hashrate_mhs << " MH/s\033[0m | "
                      << "Total Nonces: " << t.total_hashes_all_time << "\n";
            std::cout << "Target: " << t.current_target_hex << "\n\n";

            std::cout << "Worker Cores (alignas(64) cache-line padded):\n";
            for (size_t i = 0; i < t.active_threads; ++i) {
                std::cout << "  Core #" << i << ": " << std::setw(6) << t.per_thread_mhs[i] << " MH/s | Nonce: 0x"
                          << std::hex << std::setw(8) << std::setfill('0') << t.per_thread_nonces[i] << std::dec << "\n";
            }

            if (!t.recent_blocks.empty()) {
                std::cout << "\nLatest Block Mined:\n"
                          << "  Height: #" << t.recent_blocks.front().height << " | "
                          << "Hash: \033[1;32m" << t.recent_blocks.front().hash_hex << "\033[0m\n"
                          << "  Nonce: " << t.recent_blocks.front().header.nonce << " | Time: "
                          << t.recent_blocks.front().mining_duration_seconds << "s\n";
            }
        } else {
            // Periodic console heartbeat
            std::cout << "[Live Telemetry] Hashrate: " << std::fixed << std::setprecision(2) << t.total_hashrate_mhs
                      << " MH/s | Height: " << t.current_height
                      << " | Midstate: " << (t.midstate_enabled ? "ON" : "OFF")
                      << " | Target: 0x" << t.bits_hex << "\n";

            if (t.current_height > last_height) {
                last_height = t.current_height;
                if (!t.recent_blocks.empty()) {
                    const auto& b = t.recent_blocks.front();
                    std::cout << "\033[1;32m[*** BLOCK MINED! ***] Height: #" << b.height
                              << " | Nonce: " << b.header.nonce
                              << " | Hash: " << b.hash_hex
                              << " | Duration: " << b.mining_duration_seconds << "s\033[0m\n";
                }
            }
        }
    }

    std::cout << "\n[!] Shutting down mining engine...\n";
    pool->stop();
    if (server) {
        server->stop();
    }
    std::cout << "[+] Engine stopped cleanly.\n";
    return 0;
}
