/**
 * \file worker/ui/IWorkerService.hpp
 * \brief Contract exposed by \c WorkerSession to UI components.
 */
#pragma once
#include "processUtils.hpp"
#include <string>
#include <vector>

/** \brief Service interface consumed by the worker UI. */
class IWorkerService {
public:
    virtual ~IWorkerService() = default;
    // Lifecycle control
    /** \brief Start the worker session on the current thread. */
    virtual void start() = 0;
    /** \brief Request a graceful shutdown. */
    virtual void shutdown() = 0;
    /** \brief Start or resume the runtime loop. */
    virtual void start_runtime() = 0;
    /** \brief Pause the runtime loop while keeping transport resources. */
    virtual void pause_runtime() = 0;
    /** \brief Disconnect the runtime from the manager. */
    virtual void disconnect_runtime() = 0;
    
    // Metrics
    /** \brief Report completed task count. */
    virtual int GetTaskCount() = 0;
    /** \brief Retrieve textual connection status. */
    virtual std::string GetConnectionStatus() = 0;
    /** \brief Retrieve formatted bytes-sent string. */
    virtual std::string GetBytesSent() = 0;
    /** \brief Retrieve formatted bytes-received string. */
    virtual std::string GetBytesReceived() = 0;

    // Log access
    /** \brief Retrieve total number of log lines available. */
    virtual int GetNumberOfLogLines() = 0;
    /**
     * \brief Fetch a slice of log lines for display.
     * \param start Zero-based offset within the log buffer.
     * \param count Maximum number of lines desired.
     */
    virtual std::vector<std::string> GetLogLines(int start, int count) = 0;

    
    /** \brief Capture current process resource usage metrics. */
    ProcessUsage GetProcessUsage() {
        return ProcessUtils::get_process_usage();
    }
};