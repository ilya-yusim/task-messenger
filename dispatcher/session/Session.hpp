// Session.hpp - Individual session lifecycle management
#pragma once

#include "SessionStats.hpp"
#include "../../message/TaskMessageQueue.hpp"
#include "transport/coro/CoroSocketAdapter.hpp"
#include "transport/coro/CoroTask.hpp"
#include "logger.hpp"
#include <memory>
#include <string>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace session {

/**
 * \defgroup session_module Session Management Module
 * \brief Coordinates dispatcher-side session lifecycles and task delivery.
 */

/**
 * \file dispatcher/session/Session.hpp
 * \brief Defines the per-connection coroutine session wrapper.
 * \ingroup session_module
 */

/**
 * \brief Session state enumeration.
 * \ingroup session_module
 */
enum class SessionState {
    INITIALIZING,
    WAITING_FOR_TASK,
    PROCESSING_TASK,
    ACTIVE,
    COMPLETING,
    TERMINATED,
    ERROR_STATE
};

enum class SessionDisconnectReason {
    None,
    RemoteClosed,
    ConnectionReset,
    ConnectionAborted,
    NotConnected,
    BadFileDescriptor,
    GreetingReadFailed,
    GreetingInvalid,
    TransportError,
    LocalTermination,
    Unknown
};

/**
 * \brief Represents a single client session with task processing lifecycle.
 * \ingroup session_module
 *
 * Responsibilities:
 * - Connection management and cleanup.
 * - Task send/receive loop that interacts with `TaskMessageQueue`.
 * - Statistics tracking for latency and throughput reporting.
 */
class Session {
public:
    /**
     * \brief Create a new session for a client connection.
     * \param client_socket Connected transport adapter.
     * \param session_id Unique identifier for this session.
     * \param logger Logger instance for session events.
        * \param shared_task_queue Shared task queue supplying work units.
     */
    Session(std::shared_ptr<transport::CoroSocketAdapter> client_socket,
            uint32_t session_id,
            std::shared_ptr<Logger> logger,
            std::shared_ptr<TaskMessageQueue> shared_task_queue);

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
            case SessionState::WAITING_FOR_TASK: return "WAITING_FOR_TASK";
            case SessionState::PROCESSING_TASK: return "PROCESSING_TASK";
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
     * \brief Get immutable endpoint cached at session creation time.
     */
    const std::string& cached_remote_endpoint() const { return cached_remote_endpoint_; }

    /**
     * \brief Get worker node ID learned from greeting (0 if unavailable).
     */
    uint64_t get_worker_node_id() const { return worker_node_id_.load(std::memory_order_relaxed); }

    SessionDisconnectReason disconnect_reason() const {
        return disconnect_reason_.load(std::memory_order_relaxed);
    }

    std::chrono::system_clock::time_point disconnected_at() const {
        return disconnected_at_.load(std::memory_order_relaxed);
    }

    /**
     * \brief Get current raw session state enum.
     */
    SessionState state() const { return state_.load(std::memory_order_relaxed); }

    /**
     * \brief Last dispatcher-observed activity timestamp.
     */
    std::chrono::system_clock::time_point get_last_seen_dispatcher() const {
        return last_seen_dispatcher_.load(std::memory_order_relaxed);
    }

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

    /**
     * \brief Probe the socket to detect a disconnected peer.
     *
     * Performs a non-blocking 1-byte read.  If the peer has closed
     * the connection (FIN / RST) the session is moved to TERMINATED.
     * Safe to call while the session coroutine is suspended in
     * WAITING_FOR_TASK; a no-op in any other state.
     *
     * \return true if a disconnect was detected and the session was terminated.
     */
    bool probe_connection_liveness();

    /**
     * \brief Remove this session's waiter from the task queue and resume
     *        its coroutine so it can reach final_suspend.
     *
     * Call when the session is in a terminal state but the coroutine is
     * still suspended (is_awaiting_teardown() returns true).  The
     * coroutine runs synchronously on the caller's thread.
     */
    void cancel_queue_wait();

    /**
     * \brief True when the session state is terminal but the coroutine
     *        has not yet reached final_suspend.
     */
    bool is_awaiting_teardown() const;

private:
    // Core session data
    std::shared_ptr<transport::CoroSocketAdapter> client_socket_;
    uint32_t session_id_;
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<TaskMessageQueue> shared_task_queue_;  // Shared across all sessions
    std::shared_ptr<CancellationToken> cancel_token_;       // Shared with TaskQueueAwaitable
    std::string cached_remote_endpoint_;
    
    // Session state
    std::atomic<SessionState> state_;
    SessionStats stats_;
    std::atomic<bool> termination_requested_;
    std::atomic<uint64_t> worker_node_id_{0};
    std::atomic<std::chrono::system_clock::time_point> last_seen_dispatcher_{};
    std::atomic<SessionDisconnectReason> disconnect_reason_{SessionDisconnectReason::None};
    std::atomic<std::chrono::system_clock::time_point> disconnected_at_{};
    
    // Coroutine management (internal implementation detail)
    std::unique_ptr<Task<void>> session_coroutine_;
    
    // Session lifecycle helpers
    void initialize_session();
    void finalize_session();
    void update_state(SessionState new_state);
    void touch_last_seen_dispatcher();
    void mark_disconnected(SessionDisconnectReason reason);
    void record_task_sent();
    void record_task_completed();
    void record_task_failed();
    
    // Internal coroutine implementation
    Task<void> run_coroutine();
    
};

} // namespace session