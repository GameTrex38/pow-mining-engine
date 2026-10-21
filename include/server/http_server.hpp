#pragma once

#include "miner/miner_pool.hpp"
#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <functional>
#include <cstdint>

namespace pow_engine::server {

class HttpServer {
public:
    explicit HttpServer(std::shared_ptr<miner::MinerPool> pool, int port = 8080, std::string web_root = "web");
    ~HttpServer();

    bool start();
    void stop();

    [[nodiscard]] int get_port() const noexcept { return port_; }
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

private:
    void server_loop();
    void handle_client(uintptr_t client_socket);

    std::string handle_get(const std::string& path);
    std::string handle_post(const std::string& path, const std::string& body);

    std::string make_telemetry_json();
    std::string make_benchmark_json(const miner::BenchmarkResult& res);

    std::shared_ptr<miner::MinerPool> pool_;
    int port_;
    std::string web_root_;
    std::atomic<bool> running_{false};
    std::jthread accept_thread_;
    uintptr_t listen_socket_{0};
};

} // namespace pow_engine::server
