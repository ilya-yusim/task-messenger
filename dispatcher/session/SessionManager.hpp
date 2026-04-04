// SessionManager.hpp - Manages multiple concurrent sessions
#pragma once

#include "Session.hpp"
#include "SessionStats.hpp"
#include "../../message/TaskMessagePool.hpp"
#include "transport/coro/CoroSocketAdapter.hpp"
#include "transport/coro/CoroTask.hpp"
#include "logger.hpp"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <chrono>

namespace session {

enum class DispatcherMonitoringState {
    Connecting,
    AssignedActive,
    AssignedIdle,
    AssignedStalled,
    Unassigned,
    Unknown
};

struct WorkerMonitoringSnapshot {
    uint64_t worker_node_id = 0;
    uint32_t session_id = 0;
    std::string remote_endpoint;
    DispatcherMonitoringState dispatcher_state = DispatcherMonitoringState::Unknown;
    SessionStats stats;
    int64_t last_seen_dispatcher_ts_ms = 0;
    bool dispatcher_fresh = false;
};

/**
 * \defgroup session_module Session Management Module
 * \ingroup task_messenger_dispatcher
 * \brief Session lifecycle coordination running inside the Dispatcher process.
 */

/**
 * \file dispatcher/session/SessionManager.hpp
 * \brief Declares the manager for concurrent coroutine sessions.
 * \ingroup session_module
 */

/**
 * \brief Manages multiple concurrent client sessions.
 * \ingroup session_module
 *
 * Responsibilities:
 * - Create `Session` objects per accepted connection.
 * - Track active sessions, stats, and lifecycle transitions.
 * - Provide enqueue/inspection helpers for the shared `TaskMessagePool`.
 */
class SessionManager {
public:
    /**
     * \brief Create a new session manager.
     * \param logger Logger instance for manager events.
     */
    SessionManager(std::shared_ptr<Logger> logger);

    /**
     * \brief Create and start a new session for a client connection.
     * \param client_socket Connected client socket.
     * \return New session identifier.
     */
    uint32_t create_session(std::shared_ptr<transport::CoroSocketAdapter> client_socket);

    /**
     * \brief Get the number of currently active sessions.
     */
    size_t get_active_session_count() const;

    /**
    * \brief Get information about all active sessions.
    * \return Vector of session info strings.
     */
    std::vector<std::string> get_session_info() const;

    /**
    * \brief Request termination of a specific session.
    * \param session_id The ID of the session to terminate.
    * \return true if session was found and termination requested.
     */
    bool terminate_session(uint32_t session_id);

    /**
    * \brief Request termination of all active sessions.
     */
    void terminate_all_sessions();

    /**
    * \brief Clean up completed sessions (call periodically).
    * \return Number of sessions cleaned up.
     */
    size_t cleanup_completed_sessions();

    /**
    * \brief Check if a specific session exists and is active.
     */
    bool has_active_session(uint32_t session_id) const;

    /**
    * \brief Get statistics for a specific session.
    * \param session_id The session ID to query.
    * \return Session statistics, or nullopt if session not found.
     */
    std::optional<SessionStats> get_session_stats(uint32_t session_id) const;


    /**
    * \brief Enqueue externally generated tasks into the task pool.
     */
    void enqueue_tasks(std::vector<TaskMessage> tasks);

    /**
    * \brief Get task pool statistics.
    * \return Current task pool size and waiting session count.
     */
    std::pair<size_t, size_t> get_task_pool_stats() const;

    /**
     * \brief Get the shared task pool for async task submission.
     * \return Shared pointer to the TaskMessagePool.
     */
    std::shared_ptr<TaskMessagePool> task_pool() const { return task_pool_; }

    /**
    * \brief Print comprehensive statistics for all sessions and the task pool.
     */
    void print_comprehensive_statistics() const;

    /**
     * \brief Build per-worker monitoring snapshots for dashboard/proxy layers.
     */
    std::vector<WorkerMonitoringSnapshot> get_worker_monitoring_snapshot(
        std::chrono::milliseconds freshness_window = std::chrono::seconds(15)) const;

private:
    // Core manager data
    std::shared_ptr<Logger> logger_;
    // Task management
    std::shared_ptr<TaskMessagePool> task_pool_;
    
    // Session tracking
    std::atomic<uint32_t> next_session_id_;
    
    // Active sessions storage (thread-safe access required)
    mutable std::mutex sessions_mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<Session>> active_sessions_;

    // Helper methods
    uint32_t generate_session_id();
    void log_session_event(const std::string& event, uint32_t session_id, const std::string& details = "");
};

} // namespace session