// Session.hpp - Individual session lifecycle management
#pragma once

#include "SessionStats.hpp"
#include "../../message/TaskMessagePool.hpp"
#include "transport/coro/CoroSocketAdapter.hpp"
#include "transport/coro/CoroTask.hpp"
#include "logger.hpp"
#include <memory>
#include <string>
#include <atomic>

namespace session {

/**
 * \defgroup session_module Session Management Module
 * \brief Coordinates manager-side session lifecycles and task delivery.
 */

/**
 * \file manager/session/Session.hpp
 * \brief Defines the per-connection coroutine session wrapper.
 * \ingroup session_module
 */

/**
 * \brief Session state enumeration.
 * \ingroup session_module
 */
enum class SessionState {
    INITIALIZING,
    ACTIVE,
    COMPLETING,
    TERMINATED,
    ERROR_STATE
};

/**
 * \brief Represents a single client session with task processing lifecycle.
 * \ingroup session_module
 *
 * Responsibilities:
 * - Connection management and cleanup.
 * - Task send/receive loop that interacts with `TaskMessagePool`.
 * - Statistics tracking for latency and throughput reporting.
 */
class Session {
public:
    /**
     * \brief Create a new session for a client connection.
     * \param client_socket Connected transport adapter.
     * \param session_id Unique identifier for this session.
     * \param logger Logger instance for session events.
     * \param shared_task_pool Shared task pool supplying work units.
     */
    Session(std::shared_ptr<transport::CoroSocketAdapter> client_socket,
            uint32_t session_id,
            std::shared_ptr<Logger> logger,
            std::shared_ptr<TaskMessagePool> shared_task_pool);

    /**
     * \brief Start session processing.
     *
     * Launches the coroutine loop and returns immediately; the session
     * runs until completion or termination is requested.
     */
    void run();

    /**
     * \brief Check if the session coroutine finished (success or error).
     */
    bool is_done() const;

    /**
     * \brief Get the current session state as a descriptive string.
     */
    std::string get_state() const {
        switch (state_.load()) {
            case SessionState::INITIALIZING: return "INITIALIZING";
            case SessionState::ACTIVE: return "ACTIVE";
            case SessionState::COMPLETING: return "COMPLETING";
            case SessionState::TERMINATED: return "TERMINATED";
            case SessionState::ERROR_STATE: return "ERROR";
            default: return "UNKNOWN";
        }
    }

    /**
     * \brief Get current session statistics snapshot.
     */
    SessionStats get_stats() const { return stats_; }

    /**
     * \brief Get the session ID.
     */
    uint32_t get_session_id() const { return session_id_; }

    /**
     * \brief Get the client endpoint information.
     */
    std::string get_client_endpoint() const;

    /**
     * \brief Check if the session is still active.
     */
    bool is_active() const;

    /**
     * \brief Request session termination.
     */
    void request_termination();

    /**
     * \brief Check if the session task is completed.
     */
    bool is_completed() const;

private:
    // Core session data
    std::shared_ptr<transport::CoroSocketAdapter> client_socket_;
    uint32_t session_id_;
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<TaskMessagePool> shared_task_pool_;  // Shared across all sessions
    
    // Session state
    std::atomic<SessionState> state_;
    SessionStats stats_;
    std::atomic<bool> termination_requested_;
    
    // Coroutine management (internal implementation detail)
    std::unique_ptr<Task<void>> session_coroutine_;
    
    // Session lifecycle helpers
    void initialize_session();
    void finalize_session();
    void update_state(SessionState new_state);
    void record_task_sent();
    void record_task_completed();
    void record_task_failed();
    
    // Internal coroutine implementation
    Task<void> run_coroutine();
    
};

} // namespace session