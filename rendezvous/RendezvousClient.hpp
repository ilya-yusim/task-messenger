#pragma once

#include "RendezvousProtocol.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>
#include "logger.hpp"

struct IBlockingStream;

namespace rendezvous {

/**
 * \brief Lightweight client for the rendezvous registration protocol.
 *
 * Each public method opens a short-lived ZeroTier TCP connection, sends a
 * single request frame, reads the response, and closes the socket. Each call
 * is one network attempt — retries are the caller's responsibility (e.g. the
 * registration loop in DispatcherApp). cancel() interrupts an in-flight
 * exchange so shutdown is prompt.
 *
 * Snapshot relay is handled separately by SnapshotReporter over a different
 * VN port; this client does not handle ReportSnapshot frames.
 */
class RendezvousClient {
public:
    RendezvousClient(std::string host, int port, std::shared_ptr<Logger> logger);

    /// Register an endpoint (dispatcher or worker) with the rendezvous service.
    bool register_endpoint(const std::string& role, const std::string& name,
                           const std::string& vn_host, int vn_port);

    /// Unregister a previously registered endpoint.
    bool unregister_endpoint(const std::string& role, const std::string& name);

    /// Discover an endpoint by role (e.g. "dispatcher").
    bool discover_endpoint(const std::string& role, nlohmann::json& out);

    /// Cancel any in-flight exchange and prevent further exchanges from
    /// connecting. Subsequent exchange() calls return std::nullopt immediately.
    void cancel();

private:
    /// Open a connection, perform a single request/response exchange, and
    /// close. Returns the response JSON string on success; std::nullopt on
    /// failure or when cancelled.
    std::optional<std::string> exchange(MessageType req_type,
                                        const std::string& req_body,
                                        MessageType expected_resp_type);

    std::string host_;
    int port_;
    std::shared_ptr<Logger> logger_;

    // Single in-flight socket slot. Only one exchange runs at a time per
    // client because callers serialize their requests; cancel() locks this
    // mutex, then shutdown()s+close()s the cached socket so a blocked
    // connect()/read() unblocks promptly.
    std::mutex socket_mtx_;
    std::shared_ptr<IBlockingStream> active_socket_;
    std::atomic<bool> cancelled_{false};
};

} // namespace rendezvous
