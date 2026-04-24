#pragma once

#include "logger.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

struct IBlockingStream;

namespace monitoring {

/**
 * \brief Persistent connection that streams monitoring snapshots to the
 *        rendezvous service.
 *
 * Single-producer / single-canceller: the reporter thread inside
 * MonitoringService is the only caller of report(); cancel() may be invoked
 * from the shutdown thread to interrupt a blocked I/O call.
 *
 * The connection is opened lazily on the first report() call and reused for
 * subsequent calls. On any I/O failure the cached socket is closed and
 * report() returns false immediately — the next report() call will reconnect.
 * No internal sleeping; pacing is the caller's responsibility (the 1-second
 * reporter tick already paces retries).
 */
class SnapshotReporter {
public:
    SnapshotReporter(std::string host, int port, std::shared_ptr<Logger> logger);
    ~SnapshotReporter();

    SnapshotReporter(const SnapshotReporter&) = delete;
    SnapshotReporter& operator=(const SnapshotReporter&) = delete;

    /// Send a snapshot frame. Returns true on success, false on any failure
    /// (including cancellation). Lazy-connects on first call; reuses the
    /// connection on subsequent calls until an error tears it down.
    bool report(const std::string& snapshot_json);

    /// Interrupt any in-flight report() and close the cached socket.
    /// Subsequent report() calls return false immediately.
    void cancel();

private:
    std::string host_;
    int port_;
    std::shared_ptr<Logger> logger_;

    std::mutex socket_mtx_;
    std::shared_ptr<IBlockingStream> socket_;
    std::atomic<bool> cancelled_{false};
};

} // namespace monitoring
