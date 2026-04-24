#pragma once

#include "rendezvous/RendezvousProtocol.hpp"
#include "logger.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

struct IServerSocket;
struct IBlockingStream;

namespace rendezvous {

/**
 * \brief Dedicated VN listener for monitoring snapshot reports.
 *
 * Runs on its own VN port (separate from the registration protocol port) so
 * a long-lived snapshot connection from a dispatcher cannot block worker
 * registration/discovery exchanges. Each accepted connection is handled
 * synchronously: read framed messages until the peer closes; for every
 * ReportSnapshot frame, push the payload via on_snapshot_ and acknowledge
 * with ReportAck.
 *
 * Multi-dispatcher readiness: identity (role/name) is read from the first
 * snapshot frame's top-level JSON and reused for every subsequent bump on
 * the same connection, so future per-endpoint state on the server side does
 * not require any listener changes.
 */
class SnapshotListener {
public:
    /// Callback invoked for each accepted snapshot frame. Receives the raw
    /// snapshot JSON plus the connection's cached identity (role/name). The
    /// implementation should store the snapshot and bump that endpoint's
    /// last_seen timestamp.
    using SnapshotCallback = std::function<void(const std::string& role,
                                                const std::string& name,
                                                const std::string& snapshot_json)>;

    SnapshotListener(std::shared_ptr<Logger> logger, SnapshotCallback on_snapshot);
    ~SnapshotListener();

    SnapshotListener(const SnapshotListener&) = delete;
    SnapshotListener& operator=(const SnapshotListener&) = delete;

    /// Bind the VN listening socket and spawn the acceptor thread.
    bool start(const std::string& listen_host, int listen_port);

    /// Stop accepting new connections and join the acceptor thread.
    void stop() noexcept;

private:
    void accept_loop();
    void handle_connection(IBlockingStream& stream);

    std::shared_ptr<Logger> logger_;
    SnapshotCallback on_snapshot_;

    std::atomic<bool> running_{false};
    std::shared_ptr<IServerSocket> server_socket_;
    std::thread accept_thread_;
};

} // namespace rendezvous
