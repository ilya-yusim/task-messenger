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
#include "logger.hpp"
#include "worker/ui/IWorkerService.hpp"
#include "worker/processor/TaskProcessor.hpp"
#include "worker/WorkerOptions.hpp"

class IRuntimeMode;

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

    /** \brief Human-friendly formatter for byte counters. */
    std::string format_bytes(std::uint64_t bytes) const;
};

