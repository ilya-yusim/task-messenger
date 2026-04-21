#pragma once

#include "RendezvousProtocol.hpp"

#include <memory>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>
#include "logger.hpp"

namespace rendezvous {

/**
 * \brief Lightweight client for the rendezvous service.
 *
 * Each public method opens a short-lived ZeroTier TCP connection, sends a
 * request frame, reads the response, and closes the socket.  The class is
 * thread-safe and retries are handled internally (up to \c kMaxRetries).
 */
class RendezvousClient {
public:
    RendezvousClient(std::string host, int port, std::shared_ptr<Logger> logger);

    /// Register an endpoint (dispatcher or worker) with the rendezvous service.
    /// Returns true on success.
    bool register_endpoint(const std::string& role, const std::string& name,
                           const std::string& vn_host, int vn_port);

    /// Unregister a previously registered endpoint.
    /// Returns true on success.
    bool unregister_endpoint(const std::string& role, const std::string& name);

    /// Discover an endpoint by role (e.g. "dispatcher").
    /// On success, fills \p out with the response JSON; returns true.
    bool discover_endpoint(const std::string& role, nlohmann::json& out);

    /// Push a monitoring snapshot to the rendezvous service.
    /// Returns true on success.
    bool report_snapshot(const nlohmann::json& snapshot);

    /// Push an already-serialized monitoring snapshot to the rendezvous service.
    /// Avoids the parse/dump roundtrip when the caller has the payload in string form.
    /// Returns true on success.
    bool report_snapshot_json(const std::string& snapshot_json);

private:
    /// Open a connection, perform a single request/response exchange, and close.
    /// Returns the response JSON string on success; std::nullopt on failure.
    std::optional<std::string> exchange(MessageType req_type,
                                        const std::string& req_body,
                                        MessageType expected_resp_type);

    std::string host_;
    int port_;
    std::shared_ptr<Logger> logger_;

    static constexpr int kMaxRetries = 3;
    static constexpr int kConnectTimeoutMs = 2000;
};

} // namespace rendezvous
