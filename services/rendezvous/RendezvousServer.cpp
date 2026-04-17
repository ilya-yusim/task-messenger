//
// RendezvousServer.cpp — VN protocol listener + message dispatch + lifecycle
//
// The HTTP route registration and dashboard resolution live in
// RendezvousHttpHandler.cpp to avoid Windows header conflicts between
// the VN transport headers and httplib.h (both redefine winsock symbols).
//

#include "RendezvousServer.hpp"

#include "transport/socket/IServerSocket.hpp"
#include "transport/socket/IBlockingStream.hpp"
#include "transport/socket/SocketFactory.hpp"
#include "processUtils.hpp"

#include <nlohmann/json.hpp>

namespace rendezvous {

// Constructor and destructor are in RendezvousHttpHandler.cpp (needs httplib).

// ── Lifecycle ───────────────────────────────────────────────────────────────

bool RendezvousServer::start(const Config& cfg) {
    if (running_.load(std::memory_order_relaxed)) return true;
    config_ = cfg;

    // --- VN listener setup ---
    auto vn_sock = transport::SocketFactory::create_blocking_server(logger_);
    if (!vn_sock->start_listening(config_.vn_listen_host, config_.vn_listen_port, 16)) {
        if (logger_)
            logger_->error("RendezvousServer: failed to listen on VN "
                           + config_.vn_listen_host + ":" + std::to_string(config_.vn_listen_port));
        return false;
    }
    vn_server_socket_ = std::move(vn_sock);

    // --- HTTP listener setup (delegates to httplib-only TU) ---
    register_http_routes();
    if (!bind_http(config_.http_listen_host, config_.http_listen_port)) {
        return false;
    }

    running_.store(true, std::memory_order_relaxed);

    // Start VN acceptor thread
    vn_thread_ = std::thread([this]() {
        try { ProcessUtils::set_current_thread_name("RV-VN-Accept"); } catch (...) {}
        vn_accept_loop();
    });

    // Start HTTP acceptor thread
    start_http_thread();

    if (logger_) {
        logger_->info("RendezvousServer: VN protocol on "
                      + config_.vn_listen_host + ":" + std::to_string(config_.vn_listen_port)
                      + ", HTTP dashboard on "
                      + config_.http_listen_host + ":" + std::to_string(config_.http_listen_port));
    }

    return true;
}

void RendezvousServer::stop() noexcept {
    running_.store(false, std::memory_order_relaxed);

    // Shut down VN server socket first to unblock accept loop
    if (vn_server_socket_) {
        vn_server_socket_->shutdown();
    }
    if (vn_thread_.joinable()) vn_thread_.join();
    if (vn_server_socket_) {
        vn_server_socket_->close();
        vn_server_socket_.reset();
    }

    stop_http();

    if (logger_) logger_->info("RendezvousServer: stopped");
}

bool RendezvousServer::is_running() const noexcept {
    return running_.load(std::memory_order_relaxed);
}

// ── VN Protocol Listener ────────────────────────────────────────────────────

void RendezvousServer::vn_accept_loop() {
    while (running_.load(std::memory_order_relaxed)) {
        try {
            std::error_code ec;
            auto client_socket = vn_server_socket_->accept(ec);
            if (!client_socket) {
                if (ec && running_.load(std::memory_order_relaxed)) {
                    if (logger_)
                        logger_->error("RendezvousServer: VN accept error: " + ec.message());
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                continue;
            }

            if (!running_.load(std::memory_order_relaxed)) {
                try { client_socket->close(); } catch (...) {}
                break;
            }

            // create_blocking_server() produces Blocking children → safe downcast
            auto client = std::dynamic_pointer_cast<IBlockingStream>(client_socket);
            try {
                handle_connection(*client);
            } catch (const std::exception& ex) {
                if (logger_)
                    logger_->warning("RendezvousServer: connection handler error: "
                                     + std::string(ex.what()));
            }
            try { client->close(); } catch (...) {}

        } catch (const std::exception& e) {
            if (running_.load(std::memory_order_relaxed) && logger_)
                logger_->error("RendezvousServer: VN accept exception: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void RendezvousServer::handle_connection(IBlockingStream& stream) {
    auto [msg_type, body] = read_message(stream);
    std::string response_body = dispatch_message(msg_type, body);
    MessageType resp_type{};

    switch (msg_type) {
    case MessageType::RegisterRequest:   resp_type = MessageType::RegisterResponse; break;
    case MessageType::UnregisterRequest: resp_type = MessageType::UnregisterResponse; break;
    case MessageType::DiscoverRequest:   resp_type = MessageType::DiscoverResponse; break;
    case MessageType::ReportSnapshot:    resp_type = MessageType::ReportAck; break;
    default:
        if (logger_)
            logger_->warning("RendezvousServer: unknown message type "
                             + std::to_string(static_cast<int>(msg_type)));
        return;
    }

    write_message(stream, resp_type, response_body);
}

std::string RendezvousServer::dispatch_message(MessageType type, const std::string& body) {
    switch (type) {
    case MessageType::RegisterRequest:   return handle_register(body);
    case MessageType::UnregisterRequest: return handle_unregister(body);
    case MessageType::DiscoverRequest:   return handle_discover(body);
    case MessageType::ReportSnapshot:    return handle_report(body);
    default:
        return R"({"ok":false,"error":"unknown message type"})";
    }
}

// ── Protocol Handlers ───────────────────────────────────────────────────────

std::string RendezvousServer::handle_register(const std::string& body) {
    try {
        auto j = nlohmann::json::parse(body);
        RegisteredEndpoint ep;
        ep.role    = j.value("role", "");
        ep.name    = j.value("name", "");
        ep.vn_host = j.value("vn_host", "");
        ep.vn_port = j.value("vn_port", 0);
        ep.last_seen = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lk(state_mtx_);
            endpoint_ = std::move(ep);
        }

        if (logger_)
            logger_->info("RendezvousServer: registered endpoint "
                          + j.value("role", "") + "/" + j.value("name", "")
                          + " at " + j.value("vn_host", "") + ":" + std::to_string(j.value("vn_port", 0)));

        return R"({"ok":true})";
    } catch (const std::exception& ex) {
        if (logger_)
            logger_->warning("RendezvousServer: register parse error: " + std::string(ex.what()));
        return R"({"ok":false,"error":"bad request"})";
    }
}

std::string RendezvousServer::handle_unregister(const std::string& body) {
    try {
        auto j = nlohmann::json::parse(body);
        std::string role = j.value("role", "");
        std::string name = j.value("name", "");

        {
            std::lock_guard<std::mutex> lk(state_mtx_);
            if (endpoint_ && endpoint_->role == role && endpoint_->name == name) {
                endpoint_.reset();
                if (logger_)
                    logger_->info("RendezvousServer: unregistered endpoint " + role + "/" + name);
            }
        }

        return R"({"ok":true})";
    } catch (const std::exception& ex) {
        if (logger_)
            logger_->warning("RendezvousServer: unregister parse error: " + std::string(ex.what()));
        return R"({"ok":false,"error":"bad request"})";
    }
}

std::string RendezvousServer::handle_discover(const std::string& body) {
    try {
        auto j = nlohmann::json::parse(body);
        std::string role = j.value("role", "");

        std::lock_guard<std::mutex> lk(state_mtx_);
        if (!endpoint_ || endpoint_->role != role) {
            return R"({"found":false})";
        }

        auto elapsed = std::chrono::steady_clock::now() - endpoint_->last_seen;
        bool stale = elapsed > std::chrono::seconds(config_.ttl_seconds);

        nlohmann::json resp;
        resp["found"]   = true;
        resp["vn_host"] = endpoint_->vn_host;
        resp["vn_port"] = endpoint_->vn_port;
        resp["name"]    = endpoint_->name;
        resp["stale"]   = stale;
        return resp.dump();
    } catch (const std::exception& ex) {
        if (logger_)
            logger_->warning("RendezvousServer: discover parse error: " + std::string(ex.what()));
        return R"({"found":false,"error":"bad request"})";
    }
}

std::string RendezvousServer::handle_report(const std::string& body) {
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        last_snapshot_json_ = body;
        if (endpoint_) {
            endpoint_->last_seen = std::chrono::steady_clock::now();
        }
    }
    return R"({"ok":true})";
}

} // namespace rendezvous
