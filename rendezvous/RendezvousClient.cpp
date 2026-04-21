#include "RendezvousClient.hpp"
#include "RendezvousProtocol.hpp"

#include "transport/socket/SocketFactory.hpp"

#include <thread>
#include <chrono>

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

bool RendezvousClient::report_snapshot(const nlohmann::json& snapshot) {
    return report_snapshot_json(snapshot.dump());
}

bool RendezvousClient::report_snapshot_json(const std::string& snapshot_json) {
    auto resp = exchange(MessageType::ReportSnapshot, snapshot_json,
                         MessageType::ReportAck);
    if (!resp) return false;

    try {
        auto j = nlohmann::json::parse(*resp);
        return j.value("ok", false);
    } catch (...) {
        if (logger_) logger_->error("rendezvous: report_snapshot: bad response JSON");
        return false;
    }
}

// ── Internal ────────────────────────────────────────────────────────────────

std::optional<std::string>
RendezvousClient::exchange(MessageType req_type,
                           const std::string& req_body,
                           MessageType expected_resp_type) {
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        try {
            auto sock = transport::SocketFactory::create_blocking_client(logger_);
            std::error_code ec;
            sock->connect(host_, port_, ec);
            if (ec) {
                if (logger_)
                    logger_->warning("rendezvous: connect attempt "
                                     + std::to_string(attempt + 1)
                                     + " failed: " + ec.message());
                std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
                continue;
            }

            write_message(*sock, req_type, req_body);
            auto [resp_type, resp_body] = read_message(*sock);

            sock->close();

            if (resp_type != expected_resp_type) {
                if (logger_)
                    logger_->warning("rendezvous: unexpected response type "
                                     + std::to_string(static_cast<int>(resp_type))
                                     + " (expected "
                                     + std::to_string(static_cast<int>(expected_resp_type))
                                     + ")");
                continue;
            }

            return resp_body;
        } catch (const std::exception& ex) {
            if (logger_)
                logger_->warning("rendezvous: exchange attempt "
                                 + std::to_string(attempt + 1)
                                 + " error: " + ex.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
        }
    }

    if (logger_)
        logger_->error("rendezvous: exchange failed after "
                       + std::to_string(kMaxRetries) + " retries");
    return std::nullopt;
}

} // namespace rendezvous
