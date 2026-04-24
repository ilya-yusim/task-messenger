/**
 * \file worker/session/WorkerSession.hpp
 * \brief Central session controller implementing \c IWorkerService.
 */
#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <chrono>
#include "logger.hpp"
#include "worker/ui/IWorkerService.hpp"
#include "worker/processor/TaskProcessor.hpp"
#include "worker/WorkerOptions.hpp"

class IRuntimeMode;
namespace rendezvous { class RendezvousClient; }

// WorkerSession orchestrates connection lifecycle and implements IWorkerService
// Runtime provides socket + I/O strategy (blocking vs async)
/** \brief Coordinates runtime lifecycle, metrics, and UI integration. */
class WorkerSession : public IWorkerService {
public:
    /**
     * \brief Construct a session for a single worker instance.
     * \param opts Connection and runtime configuration.
     * \param logger Shared logger forwarded to dependent components.
     */
    WorkerSession(const WorkerOptions& opts, std::shared_ptr<Logger> logger);

    /** \brief Run the session control loop until shutdown is requested. */
    void start() override;

    // IWorkerService interface
    /** \brief Report completed task count accumulated by the runtime. */
    int GetTaskCount() override;
    /** \brief Retrieve the current connection state string. */
    std::string GetConnectionStatus() override;
    /** \brief Retrieve formatted bytes-sent counter for UI consumption. */
    std::string GetBytesSent() override;
    /** \brief Retrieve formatted bytes-received counter for UI consumption. */
    std::string GetBytesReceived() override;
    /** \brief Total log line count available from the logger sink. */
    int GetNumberOfLogLines() override;
    /**
     * \brief Fetch a window of log lines for the UI.
     * \param start Zero-based start index.
     * \param count Maximum number of lines to retrieve.
     */
    std::vector<std::string> GetLogLines(int start, int count) override;
    /** \brief Request a full shutdown, disconnecting and releasing resources. */
    void shutdown() override;
    /** \brief Resume or initiate the runtime loop. */
    void start_runtime() override;
    /** \brief Pause the active runtime loop without releasing resources. */
    void pause_runtime() override;
    /** \brief Disconnect the runtime socket while keeping configuration intact. */
    void disconnect_runtime() override;

private:
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<IRuntimeMode> runtime_;
    TaskProcessor processor_;

    std::string host_;
    int port_;
    WorkerMode mode_;

    std::atomic<bool> start_requested_{true};
    std::atomic<bool> disconnect_requested_{false};
    std::atomic<bool> shutdown_requested_{false};

    std::string connection_status_{"Disconnected"};
    std::mutex status_mtx_;

    /// Outcome of a discovery attempt.
    enum class DiscoveryResult {
        Disabled,   ///< Rendezvous mode is off — caller should use configured host/port.
        Updated,    ///< Resolved a dispatcher and host_/port_ (and runtime) were changed.
        Unchanged,  ///< Resolved the same endpoint that was already in use.
        Cancelled,  ///< Shutdown or disconnect was requested before an endpoint resolved.
    };

    /// In rendezvous mode, block (with exponential backoff) until a live
    /// dispatcher is resolved, shutdown is requested, or a UI disconnect
    /// fires. In direct-connect mode, returns \c Disabled immediately.
    /// Never falls back to the configured host/port when rendezvous is enabled.
    DiscoveryResult try_discover_dispatcher();

    /// Persistent client used for rendezvous discovery. Constructed once at
    /// start() when rendezvous is enabled. Its first socket connect acquires
    /// the VN lease, so no explicit join is required. Holding it on the
    /// session lets shutdown() call cancel() to interrupt a blocked
    /// discover_endpoint() exchange.
    std::shared_ptr<rendezvous::RendezvousClient> rendezvous_client_;

    /// Exponential backoff delay for reconnection after I/O errors (direct-
    /// connect mode only; rendezvous mode uses the discovery retry loop to
    /// pace reconnects).
    std::chrono::seconds reconnect_delay_{1};
    static constexpr std::chrono::seconds kMaxReconnectDelay{5};

    /** \brief Human-friendly formatter for byte counters. */
    std::string format_bytes(std::uint64_t bytes) const;
};

