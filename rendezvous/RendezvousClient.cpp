#include "RendezvousClient.hpp"
#include "RendezvousProtocol.hpp"

#include "transport/socket/IBlockingStream.hpp"
#include "transport/socket/SocketFactory.hpp"

namespace rendezvous {

RendezvousClient::RendezvousClient(std::string host, int port,
                                   std::shared_ptr<Logger> logger)
    : host_(std::move(host)), port_(port), logger_(std::move(logger)) {}

// ── Public API ──────────────────────────────────────────────────────────────

bool RendezvousClient::register_endpoint(const std::string& role,
                                          const std::string& name,
                                          const std::string& vn_host,
                                          int vn_port) {
    nlohmann::json body;
    body["role"] = role;
    body["name"] = name;
    body["vn_host"] = vn_host;
    body["vn_port"] = vn_port;

    auto resp = exchange(MessageType::RegisterRequest, body.dump(),
                         MessageType::RegisterResponse);
    if (!resp) return false;

    try {
        auto j = nlohmann::json::parse(*resp);
        return j.value("ok", false);
    } catch (...) {
        if (logger_) logger_->error("rendezvous: register_endpoint: bad response JSON");
        return false;
    }
}

bool RendezvousClient::unregister_endpoint(const std::string& role,
                                            const std::string& name) {
    nlohmann::json body;
    body["role"] = role;
    body["name"] = name;

    auto resp = exchange(MessageType::UnregisterRequest, body.dump(),
                         MessageType::UnregisterResponse);
    if (!resp) return false;

    try {
        auto j = nlohmann::json::parse(*resp);
        return j.value("ok", false);
    } catch (...) {
        if (logger_) logger_->error("rendezvous: unregister_endpoint: bad response JSON");
        return false;
    }
}

bool RendezvousClient::discover_endpoint(const std::string& role,
                                          nlohmann::json& out) {
    nlohmann::json body;
    body["role"] = role;

    auto resp = exchange(MessageType::DiscoverRequest, body.dump(),
                         MessageType::DiscoverResponse);
    if (!resp) return false;

    try {
        out = nlohmann::json::parse(*resp);
        return true;
    } catch (...) {
        if (logger_) logger_->error("rendezvous: discover_endpoint: bad response JSON");
        return false;
    }
}

// ── Internal ────────────────────────────────────────────────────────────────

void RendezvousClient::cancel() {
    cancelled_.store(true, std::memory_order_release);

    std::shared_ptr<IBlockingStream> sock;
    {
        std::lock_guard<std::mutex> lock(socket_mtx_);
        sock = active_socket_;
    }
    if (sock) {
        // Interrupts any ongoing connect()/read()/write() on the socket.
        sock->shutdown();
        sock->close();
    }
}

std::optional<std::string>
RendezvousClient::exchange(MessageType req_type,
                           const std::string& req_body,
                           MessageType expected_resp_type) {
    if (cancelled_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    std::shared_ptr<IBlockingStream> sock;
    try {
        sock = transport::SocketFactory::create_blocking_client(logger_);
    } catch (const std::exception& ex) {
        if (logger_)
            logger_->warning(std::string("rendezvous: socket create failed: ") + ex.what());
        return std::nullopt;
    }

    // Publish the socket so cancel() can reach it. Re-check the cancelled
    // flag after publishing to honour a cancel() that fired in the gap.
    {
        std::lock_guard<std::mutex> lock(socket_mtx_);
        active_socket_ = sock;
    }
    if (cancelled_.load(std::memory_order_acquire)) {
        sock->shutdown();
        sock->close();
        std::lock_guard<std::mutex> lock(socket_mtx_);
        active_socket_.reset();
        return std::nullopt;
    }

    std::optional<std::string> result;
    try {
        std::error_code ec;
        sock->connect(host_, port_, ec);
        if (ec) {
            if (logger_)
                logger_->warning("rendezvous: connect failed: " + ec.message());
        } else {
            write_message(*sock, req_type, req_body);
            auto [resp_type, resp_body] = read_message(*sock);
            if (resp_type != expected_resp_type) {
                if (logger_)
                    logger_->warning("rendezvous: unexpected response type "
                                     + std::to_string(static_cast<int>(resp_type))
                                     + " (expected "
                                     + std::to_string(static_cast<int>(expected_resp_type))
                                     + ")");
            } else {
                result = std::move(resp_body);
            }
        }
    } catch (const std::exception& ex) {
        if (logger_)
            logger_->warning(std::string("rendezvous: exchange error: ") + ex.what());
    }

    try { sock->close(); } catch (...) {}
    {
        std::lock_guard<std::mutex> lock(socket_mtx_);
        active_socket_.reset();
    }
    return result;
}

} // namespace rendezvous
