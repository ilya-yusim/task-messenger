#pragma once

#include "rendezvous/RendezvousProtocol.hpp"
#include "logger.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct IServerSocket;
namespace httplib { class Server; }

namespace rendezvous {

/// Registered endpoint tracked by the rendezvous service.
struct RegisteredEndpoint {
    std::string role;
    std::string name;
    std::string vn_host;
    int vn_port = 0;
    std::chrono::steady_clock::time_point last_seen{};
};

/**
 * \brief Lightweight rendezvous service that bridges endpoint registration,
 *        discovery, and monitoring snapshot relay.
 *
 * Two listener threads:
 * 1. **VN listener** — accepts framed-JSON connections over the virtual network
 *    to handle register/unregister/discover/report protocol messages.
 * 2. **HTTP listener** — cpp-httplib on regular TCP for the browser dashboard
 *    (`/`, `/api/monitor`, `/healthz`).
 */
class RendezvousServer {
public:
    struct Config {
        std::string vn_listen_host = "0.0.0.0";  ///< VN interface to bind
        int         vn_listen_port = 8088;        ///< VN port for protocol traffic
        std::string http_listen_host = "0.0.0.0"; ///< HTTP interface for dashboard
        int         http_listen_port = 9090;       ///< HTTP port for dashboard
        int         ttl_seconds = 30;              ///< Endpoint TTL
    };

    explicit RendezvousServer(std::shared_ptr<Logger> logger);
    ~RendezvousServer();

    RendezvousServer(const RendezvousServer&) = delete;
    RendezvousServer& operator=(const RendezvousServer&) = delete;

    /// Start both VN and HTTP listeners.  Returns true on success.
    bool start(const Config& cfg);

    /// Stop both listeners and join threads.
    void stop() noexcept;

    /// Whether the server is currently running.
    bool is_running() const noexcept;

private:
    // ── VN protocol listener ────────────────────────────────────────────────
    void vn_accept_loop();
    void handle_connection(IBlockingStream& stream);
    std::string dispatch_message(MessageType type, const std::string& body);

    std::string handle_register(const std::string& body);
    std::string handle_unregister(const std::string& body);
    std::string handle_discover(const std::string& body);
    std::string handle_report(const std::string& body);

    // ── HTTP dashboard listener ─────────────────────────────────────────────
    void register_http_routes();
    bool bind_http(const std::string& host, int port);
    void start_http_thread();
    void stop_http() noexcept;
    static std::string resolve_dashboard_dir();

    // ── State ───────────────────────────────────────────────────────────────
    std::shared_ptr<Logger> logger_;
    Config config_;
    std::atomic<bool> running_{false};

    // VN listener
    std::shared_ptr<IServerSocket> vn_server_socket_;
    std::thread vn_thread_;

    // HTTP listener
    std::unique_ptr<httplib::Server> http_server_;
    std::thread http_thread_;

    // Registered endpoint (single-dispatcher for now)
    mutable std::mutex state_mtx_;
    std::optional<RegisteredEndpoint> endpoint_;
    std::string last_snapshot_json_;
};

} // namespace rendezvous
