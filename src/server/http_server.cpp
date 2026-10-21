#include "server/http_server.hpp"
#include "crypto/sha256.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define CLOSE_SOCKET(s) closesocket(s)
#define IS_VALID_SOCKET(s) ((s) != INVALID_SOCKET)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
using socket_t = int;
#define CLOSE_SOCKET(s) ::close(s)
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define IS_VALID_SOCKET(s) ((s) >= 0)
#endif

namespace pow_engine::server {

namespace {

std::string get_mime_type(const std::string& path) {
    if (path.ends_with(".html") || path.ends_with(".htm")) return "text/html; charset=utf-8";
    if (path.ends_with(".css")) return "text/css; charset=utf-8";
    if (path.ends_with(".js")) return "application/javascript; charset=utf-8";
    if (path.ends_with(".json")) return "application/json; charset=utf-8";
    if (path.ends_with(".svg")) return "image/svg+xml";
    if (path.ends_with(".ico")) return "image/x-icon";
    return "text/plain; charset=utf-8";
}

std::string make_http_response(int status_code, const std::string& status_text,
                               const std::string& content_type, const std::string& body) {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
       << "Server: Antigravity-PoW-Engine/1.0\r\n"
       << "Content-Type: " << content_type << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type\r\n"
       << "Connection: close\r\n\r\n"
       << body;
    return ss.str();
}

std::string escape_json_string(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\b') o << "\\b";
        else if (c == '\f') o << "\\f";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') o << "\\r";
        else if (c == '\t') o << "\\t";
        else if (static_cast<unsigned char>(c) <= 0x1f) {
            o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
            o << c;
        }
    }
    return o.str();
}

} // namespace

HttpServer::HttpServer(std::shared_ptr<miner::MinerPool> pool, int port, std::string web_root)
    : pool_(std::move(pool)), port_(port), web_root_(std::move(web_root)) {}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start() {
    if (running_.exchange(true)) {
        return true;
    }

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[HttpServer] WSAStartup failed.\n";
        running_ = false;
        return false;
    }
#endif

    socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!IS_VALID_SOCKET(s)) {
        std::cerr << "[HttpServer] Failed to create socket.\n";
        running_ = false;
        return false;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[HttpServer] Failed to bind to port " << port_ << ".\n";
        CLOSE_SOCKET(s);
        running_ = false;
        return false;
    }

    if (listen(s, 16) == SOCKET_ERROR) {
        std::cerr << "[HttpServer] Failed to listen on socket.\n";
        CLOSE_SOCKET(s);
        running_ = false;
        return false;
    }

    listen_socket_ = static_cast<uintptr_t>(s);
    accept_thread_ = std::jthread(&HttpServer::server_loop, this);

    std::cout << "[HttpServer] Telemetry Visualizer live at: http://localhost:" << port_ << "/\n";
    return true;
}

void HttpServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (listen_socket_ != 0) {
        CLOSE_SOCKET(static_cast<socket_t>(listen_socket_));
        listen_socket_ = 0;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.request_stop();
        accept_thread_.join();
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

void HttpServer::server_loop() {
    while (running_) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        socket_t client = accept(static_cast<socket_t>(listen_socket_),
                                 reinterpret_cast<sockaddr*>(&client_addr),
                                 &addr_len);
        if (!IS_VALID_SOCKET(client)) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Process client request in detached thread for low-latency concurrency
        std::thread([this, client]() {
            handle_client(static_cast<uintptr_t>(client));
        }).detach();
    }
}

void HttpServer::handle_client(uintptr_t client_socket) {
    socket_t s = static_cast<socket_t>(client_socket);
    std::vector<char> buffer(8192);
    int received = recv(s, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);

    if (received <= 0) {
        CLOSE_SOCKET(s);
        return;
    }
    buffer[received] = '\0';
    std::string req(buffer.data(), received);

    // Parse HTTP request line
    std::istringstream stream(req);
    std::string method, path, proto;
    stream >> method >> path >> proto;

    // Handle OPTIONS for CORS
    if (method == "OPTIONS") {
        std::string res = make_http_response(204, "No Content", "text/plain", "");
        send(s, res.data(), static_cast<int>(res.size()), 0);
        CLOSE_SOCKET(s);
        return;
    }

    std::string response;
    if (method == "GET" || method == "HEAD") {
        response = handle_get(path);
        if (method == "HEAD") {
            // Strip body for HEAD request
            size_t head_end = response.find("\r\n\r\n");
            if (head_end != std::string::npos) {
                response = response.substr(0, head_end + 4);
            }
        }
    } else if (method == "POST") {
        // Extract body
        size_t body_pos = req.find("\r\n\r\n");
        std::string body = (body_pos != std::string::npos) ? req.substr(body_pos + 4) : "";
        response = handle_post(path, body);
    } else {
        response = make_http_response(405, "Method Not Allowed", "text/plain", "Method Not Allowed");
    }

    send(s, response.data(), static_cast<int>(response.size()), 0);
    CLOSE_SOCKET(s);
}

std::string HttpServer::handle_get(const std::string& path) {
    if (path == "/api/telemetry") {
        return make_http_response(200, "OK", "application/json", make_telemetry_json());
    }

    // Serve web files
    std::string file_path = path;
    if (file_path == "/" || file_path.empty()) {
        file_path = "/index.html";
    }

    // Try web_root_ + path, then relative fallback
    std::vector<std::string> candidate_paths = {
        web_root_ + file_path,
        "./web" + file_path,
        "web" + file_path,
        "../web" + file_path
    };

    for (const auto& cp : candidate_paths) {
        std::ifstream file(cp, std::ios::binary);
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            return make_http_response(200, "OK", get_mime_type(cp), ss.str());
        }
    }

    return make_http_response(404, "Not Found", "text/plain", "404 Not Found");
}

std::string HttpServer::handle_post(const std::string& path, const std::string& body) {
    if (path == "/api/benchmark") {
        int duration = 2; // Default 2-second snappy benchmark
        if (body.find("duration=") != std::string::npos) {
            auto pos = body.find("duration=");
            try { duration = std::stoi(body.substr(pos + 9)); } catch (...) {}
        }
        auto result = pool_->run_benchmark(duration);
        return make_http_response(200, "OK", "application/json", make_benchmark_json(result));
    }

    if (path == "/api/config") {
        // Parse thread count
        auto find_val = [&](const std::string& key) -> std::string {
            auto p1 = body.find("\"" + key + "\":");
            if (p1 != std::string::npos) {
                p1 += key.size() + 3;
                while (p1 < body.size() && (body[p1] == ' ' || body[p1] == '"')) p1++;
                auto p2 = body.find_first_of(",}\" \r\n", p1);
                return body.substr(p1, p2 - p1);
            }
            return "";
        };

        std::string threads_str = find_val("threads");
        if (!threads_str.empty()) {
            try {
                size_t th = std::stoull(threads_str);
                pool_->set_thread_count(th);
            } catch (...) {}
        }

        std::string midstate_str = find_val("midstate");
        if (!midstate_str.empty()) {
            bool enable = (midstate_str == "true" || midstate_str == "1");
            pool_->set_midstate_enabled(enable);
        }

        std::string bits_str = find_val("bits");
        if (!bits_str.empty()) {
            try {
                uint32_t bits = static_cast<uint32_t>(std::stoul(bits_str, nullptr, 16));
                pool_->set_difficulty(bits);
            } catch (...) {}
        }

        return make_http_response(200, "OK", "application/json", "{\"status\":\"ok\"}");
    }

    if (path == "/api/transaction") {
        auto find_str = [&](const std::string& key) -> std::string {
            auto p1 = body.find("\"" + key + "\":");
            if (p1 != std::string::npos) {
                p1 += key.size() + 3;
                while (p1 < body.size() && (body[p1] == ' ' || body[p1] == '"')) p1++;
                auto p2 = body.find_first_of("\"", p1);
                return body.substr(p1, p2 - p1);
            }
            return "";
        };

        chain::Transaction tx;
        tx.sender = find_str("sender");
        tx.recipient = find_str("recipient");
        if (tx.sender.empty()) tx.sender = "User_Web_Wallet";
        if (tx.recipient.empty()) tx.recipient = "Merchant_Cold_Storage";
        tx.amount = 1.5;
        tx.fee_sats = 5000;
        tx.is_coinbase = false;
        tx.txid_hex = crypto::to_hex(tx.compute_hash(), true);

        pool_->add_transaction(tx);
        return make_http_response(200, "OK", "application/json", "{\"status\":\"tx_added\",\"txid\":\"" + tx.txid_hex + "\"}");
    }

    return make_http_response(404, "Not Found", "application/json", "{\"error\":\"endpoint not found\"}");
}

std::string HttpServer::make_telemetry_json() {
    auto t = pool_->get_telemetry();
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "{\n"
       << "  \"height\": " << t.current_height << ",\n"
       << "  \"bits\": " << t.current_bits << ",\n"
       << "  \"bits_hex\": \"" << std::hex << std::setw(8) << std::setfill('0') << t.current_bits << std::dec << "\",\n"
       << "  \"target_hex\": \"" << t.current_target_hex << "\",\n"
       << "  \"midstate_enabled\": " << (t.midstate_enabled ? "true" : "false") << ",\n"
       << "  \"active_threads\": " << t.active_threads << ",\n"
       << "  \"total_mhs\": " << t.total_hashrate_mhs << ",\n"
       << "  \"total_hashes\": " << t.total_hashes_all_time << ",\n"
       << "  \"is_mining\": " << (t.is_mining ? "true" : "false") << ",\n"
       << "  \"is_benchmarking\": " << (t.is_benchmarking ? "true" : "false") << ",\n"
       << "  \"active_header_hex\": \"" << t.active_header_hex << "\",\n"
       << "  \"active_midstate_hex\": \"" << t.active_midstate_hex << "\",\n";

    // Per thread stats
    ss << "  \"per_thread\": [\n";
    for (size_t i = 0; i < t.active_threads; ++i) {
        ss << "    {\"id\": " << i
           << ", \"mhs\": " << t.per_thread_mhs[i]
           << ", \"nonce\": " << t.per_thread_nonces[i] << "}"
           << (i + 1 < t.active_threads ? ",\n" : "\n");
    }
    ss << "  ],\n";

    // Merkle tree nodes
    ss << "  \"merkle_nodes\": [\n";
    for (size_t i = 0; i < t.current_merkle_nodes.size(); ++i) {
        const auto& node = t.current_merkle_nodes[i];
        ss << "    {\"level\": " << node.level
           << ", \"index\": " << node.index
           << ", \"hash\": \"" << node.hash_hex << "\""
           << ", \"label\": \"" << escape_json_string(node.label) << "\"}"
           << (i + 1 < t.current_merkle_nodes.size() ? ",\n" : "\n");
    }
    ss << "  ],\n";

    // Recent blocks
    ss << "  \"recent_blocks\": [\n";
    for (size_t i = 0; i < t.recent_blocks.size(); ++i) {
        const auto& b = t.recent_blocks[i];
        ss << "    {\n"
           << "      \"height\": " << b.height << ",\n"
           << "      \"hash\": \"" << b.hash_hex << "\",\n"
           << "      \"prev_hash\": \"" << b.prev_hash_hex << "\",\n"
           << "      \"merkle_root\": \"" << b.merkle_root_hex << "\",\n"
           << "      \"nonce\": " << b.header.nonce << ",\n"
           << "      \"bits_hex\": \"" << std::hex << std::setw(8) << std::setfill('0') << b.header.bits << std::dec << "\",\n"
           << "      \"duration_sec\": " << b.mining_duration_seconds << ",\n"
           << "      \"total_hashes\": " << b.total_hashes << ",\n"
           << "      \"mined_by\": \"" << escape_json_string(b.mined_by_thread) << "\",\n"
           << "      \"timestamp\": \"" << escape_json_string(b.timestamp_str) << "\"\n"
           << "    }" << (i + 1 < t.recent_blocks.size() ? ",\n" : "\n");
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

std::string HttpServer::make_benchmark_json(const miner::BenchmarkResult& res) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "{\n"
       << "  \"single_naive_mhs\": " << res.single_naive_mhs << ",\n"
       << "  \"single_midstate_mhs\": " << res.single_midstate_mhs << ",\n"
       << "  \"multi_midstate_mhs\": " << res.multi_midstate_mhs << ",\n"
       << "  \"midstate_speedup_percent\": " << res.midstate_speedup_percent << ",\n"
       << "  \"multicore_scaling_factor\": " << res.multicore_scaling_factor << ",\n"
       << "  \"thread_count\": " << res.thread_count << ",\n"
       << "  \"summary\": \"" << escape_json_string(res.summary) << "\"\n"
       << "}\n";
    return ss.str();
}

} // namespace pow_engine::server
