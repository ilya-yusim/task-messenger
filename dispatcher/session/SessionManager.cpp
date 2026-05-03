// SessionManager.cpp - Implementation of multiple session management
#include "SessionManager.hpp"
#include <algorithm>

namespace {
constexpr std::size_t kRecentDisconnectHistoryCap = 100;

int64_t to_epoch_ms(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}
} // namespace

/**
 * \file dispatcher/session/SessionManager.cpp
 * \brief Implements session orchestration and task queue fan-out.
 * \ingroup session_module
 */

namespace session {

SessionManager::SessionManager(std::shared_ptr<Logger> logger)
    : logger_(std::move(logger))
    , task_queue_(std::make_shared<TaskMessageQueue>())
    , next_session_id_(1) {
    
    if (!logger_) {
        throw std::invalid_argument("SessionManager: logger cannot be null");
    }
    
    logger_->info("SessionManager: Created with default config ");
    logger_->info("SessionManager: Initialized task queue");
}

uint32_t SessionManager::create_session(std::shared_ptr<transport::CoroSocketAdapter> client_socket) {
    if (!client_socket) {
        logger_->error("SessionManager: Cannot create session with null client_socket");
        return 0;
    }
    
    uint32_t session_id = generate_session_id();
    
    try {
        // Create the session object with task queue
        auto session = std::make_shared<Session>(client_socket, session_id, logger_, task_queue_);
        
        // Store the session
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            active_sessions_[session_id] = session;
        }
        
        // Start the session task - it will manage its own coroutine internally
        session->run();

        return session_id;
        
    } catch (const std::exception& e) {
        logger_->error("SessionManager: Failed to create session " + std::to_string(session_id) + ": " + e.what());
        return 0;
    }
}

size_t SessionManager::get_active_session_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return active_sessions_.size();
}

std::vector<std::string> SessionManager::get_session_info() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<std::string> info;
    
    for (const auto& [session_id, session] : active_sessions_) {
        if (session) {
            std::string session_info = "Session " + std::to_string(session_id) + 
                                     ": " + session->get_client_endpoint() + 
                                     " [" + session->get_state() + "]";
            info.push_back(session_info);
        }
    }
    
    return info;
}

bool SessionManager::terminate_session(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it != active_sessions_.end() && it->second) {
        it->second->request_termination();
        return true;
    }
    
    logger_->warning("SessionManager: Cannot terminate session " + std::to_string(session_id) + " - not found");
    return false;
}

void SessionManager::terminate_all_sessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    size_t count = 0;
    for (const auto& [session_id, session] : active_sessions_) {
        if (session) {
            session->request_termination();
            count++;
        }
    }
    
    // Force-resume any coroutines still suspended in the task queue so they
    // reach final_suspend and can be safely destroyed.
    for (const auto& [session_id, session] : active_sessions_) {
        if (session && session->is_awaiting_teardown()) {
            session->cancel_queue_wait();
        }
    }
    
    logger_->info("SessionManager: Requested termination of " + std::to_string(count) + " sessions");
}

size_t SessionManager::cleanup_completed_sessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    const auto now = std::chrono::system_clock::now();

    // Probe idle sessions for peer disconnects.
    for (auto& [id, session] : active_sessions_) {
        if (session) {
            session->probe_connection_liveness();
        }
    }

    // Resume coroutines that are in a terminal state but still suspended in
    // the task queue. Each coroutine runs synchronously on this thread and
    // reaches final_suspend, making is_done() true.
    for (auto& [id, session] : active_sessions_) {
        if (session && session->is_awaiting_teardown()) {
            session->cancel_queue_wait();
        }
    }

    // Discard any remaining cancelled waiter entries from the queue to
    // prevent unbounded growth.
    if (task_queue_) {
        task_queue_->drain_cancelled();
    }
    
    size_t cleaned_up = 0;
    
    // Find completed sessions
    auto session_it = active_sessions_.begin();
    while (session_it != active_sessions_.end()) {
        uint32_t session_id = session_it->first;
        auto session = session_it->second;
        
        if (!session || session->is_completed()) {
            // Log final stats before cleanup
            if (session) {
                auto stats = session->get_stats();
                if (logger_) {
                    logger_->info("SessionManager: Completed session " + std::to_string(session_id) + 
                                  " - Tasks: " + std::to_string(stats.tasks_sent) + 
                                  ", Success rate: " + std::to_string(stats.get_success_rate()) + "%");
                }

                RecentDisconnectSnapshot rec{};
                rec.worker_node_id = session->get_worker_node_id();
                rec.session_id = session_id;
                rec.remote_endpoint = session->cached_remote_endpoint();
                rec.reason = disconnect_reason_to_string(session->disconnect_reason());
                const auto disconnected_at = session->disconnected_at();
                rec.disconnected_ts_ms = to_epoch_ms(
                    disconnected_at.time_since_epoch().count() > 0 ? disconnected_at : now);
                // Keep most-recent events at the front for dashboard-first consumption.
                recent_disconnects_.insert(recent_disconnects_.begin(), std::move(rec));
                if (recent_disconnects_.size() > kRecentDisconnectHistoryCap) {
                    recent_disconnects_.resize(kRecentDisconnectHistoryCap);
                }
            }
            
            // Remove from active sessions
            session_it = active_sessions_.erase(session_it);
            
            cleaned_up++;
        } else {
            ++session_it;
        }
    }
    
    if (cleaned_up > 0) {
        logger_->debug("SessionManager: Cleaned up " + std::to_string(cleaned_up) + " completed sessions");
    }

    return cleaned_up;
}

bool SessionManager::has_active_session(uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    return it != active_sessions_.end() && it->second && it->second->is_active();
}

std::optional<SessionStats> SessionManager::get_session_stats(uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it != active_sessions_.end() && it->second) {
        return it->second->get_stats();
    }
    
    return std::nullopt;
}

uint32_t SessionManager::generate_session_id() {
    return next_session_id_.fetch_add(1);
}

void SessionManager::enqueue_tasks(std::vector<TaskMessage> tasks) {
    if (task_queue_.get() && !tasks.empty()) {
        logger_->info("SessionManager: Enqueuing " + std::to_string(tasks.size()) + " external tasks");
        task_queue_->add_tasks(std::move(tasks));
        logger_->info("SessionManager: Task queue size now: " + std::to_string(task_queue_->size()));
    }
}

size_t SessionManager::get_task_queue_size() const {
    return task_queue_ ? task_queue_->size() : 0;
}

size_t SessionManager::get_task_queue_waiting_workers_count() const {
    return task_queue_ ? task_queue_->waiting_workers_count() : 0;
}

void SessionManager::print_comprehensive_statistics() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    logger_->info("=== COMPREHENSIVE SESSION STATISTICS ===");
    
    if (active_sessions_.empty()) {
        logger_->info("No active sessions");
        const auto available_tasks = get_task_queue_size();
        logger_->info("Task Queue: " + std::to_string(available_tasks) + " tasks available");
        logger_->info("========================================");
        return;
    }
    
    // Per-session details and collect summary stats
    size_t total_sent = 0, total_completed = 0, total_failed = 0;
    size_t total_bytes_sent = 0, total_bytes_received = 0;
    std::chrono::nanoseconds total_roundtrip_ns{0};
    uint32_t total_timed_tasks = 0;
    for (const auto& [session_id, session] : active_sessions_) {
        if (session) {
            auto stats = session->get_stats();
            auto duration = stats.get_duration().count();
            
            logger_->info("Session " + std::to_string(session_id) + ":");
            logger_->info("  Endpoint: " + session->get_client_endpoint());
            logger_->info("  State: " + session->get_state());
            logger_->info("  Duration: " + std::to_string(duration) + " seconds");
            logger_->info("  Tasks Sent: " + std::to_string(stats.tasks_sent));
            logger_->info("  Tasks Completed: " + std::to_string(stats.tasks_completed));
            logger_->info("  Tasks Failed: " + std::to_string(stats.tasks_failed));
            logger_->info("  Success Rate: " + std::to_string(stats.get_success_rate()) + "%");
            logger_->info("  Throughput: " + std::to_string(
                duration > 0 ? stats.tasks_completed / duration : 0) + " tasks/sec");
            logger_->info("  Bytes: sent=" + std::to_string(stats.bytes_sent) + ", recv=" + std::to_string(stats.bytes_received));
            const double total_rt_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(stats.total_task_roundtrip_time).count();
            const double last_rt_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(stats.last_task_roundtrip_time).count();
            const double avg_rt_ms = stats.get_avg_roundtrip_ms();
            logger_->info("  Timed Tasks: " + std::to_string(stats.timed_tasks));
            logger_->info("  Roundtrip (ms): total=" + std::to_string(total_rt_ms) + ", avg=" + std::to_string(avg_rt_ms) + ", last=" + std::to_string(last_rt_ms));
            
            // Accumulate totals
            total_sent += stats.tasks_sent;
            total_completed += stats.tasks_completed;
            total_failed += stats.tasks_failed;
            total_bytes_sent += stats.bytes_sent;
            total_bytes_received += stats.bytes_received;
            total_roundtrip_ns += stats.total_task_roundtrip_time;
            total_timed_tasks += stats.timed_tasks;
        }
    }
    
    const auto available_tasks = get_task_queue_size();
    const auto waiting_sessions = get_task_queue_waiting_workers_count();
    
    logger_->info("=== SUMMARY ===");
    logger_->info("Total Sessions: " + std::to_string(active_sessions_.size()));
    logger_->info("Total Tasks Sent: " + std::to_string(total_sent));
    logger_->info("Total Tasks Completed: " + std::to_string(total_completed));
    logger_->info("Total Tasks Failed: " + std::to_string(total_failed));
    logger_->info("Overall Success Rate: " + std::to_string(
        total_sent > 0 ? (total_completed * 100) / total_sent : 0) + "%");
    const double total_rt_ms_all = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(total_roundtrip_ns).count();
    const double overall_avg_rt_ms = total_timed_tasks > 0 ? (total_rt_ms_all / total_timed_tasks) : 0.0;
    logger_->info("Bytes: total sent=" + std::to_string(total_bytes_sent) + ", total recv=" + std::to_string(total_bytes_received));
    logger_->info("Roundtrip (ms): total=" + std::to_string(total_rt_ms_all) + ", overall avg=" + std::to_string(overall_avg_rt_ms) + ", timed tasks=" + std::to_string(total_timed_tasks));
    logger_->info("Task Queue: " + std::to_string(available_tasks) + " available, " + 
                  std::to_string(waiting_sessions) + " sessions waiting");
    logger_->info("========================================");
}

std::vector<WorkerMonitoringSnapshot> SessionManager::get_worker_monitoring_snapshot(
    std::chrono::milliseconds freshness_window) const {
    std::vector<WorkerMonitoringSnapshot> out;
    const auto now = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    out.reserve(active_sessions_.size());

    for (const auto& [session_id, session] : active_sessions_) {
        if (!session) {
            continue;
        }

        WorkerMonitoringSnapshot row{};
        row.worker_node_id = session->get_worker_node_id();
        row.session_id = session_id;
        row.remote_endpoint = session->get_client_endpoint();
        row.worker_state = session->state();
        row.stats = session->get_stats();

        const auto last_seen = session->get_last_seen_dispatcher();
        row.last_seen_dispatcher_ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         last_seen.time_since_epoch())
                                         .count();
        row.dispatcher_fresh = (now - last_seen) <= freshness_window;

        out.push_back(std::move(row));
    }

    return out;
}

std::vector<RecentDisconnectSnapshot> SessionManager::get_recent_disconnects_snapshot() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return recent_disconnects_;
}

std::string SessionManager::disconnect_reason_to_string(SessionDisconnectReason reason) {
    switch (reason) {
    case SessionDisconnectReason::None:
        return "none";
    case SessionDisconnectReason::RemoteClosed:
        return "remote_closed";
    case SessionDisconnectReason::ConnectionReset:
        return "connection_reset";
    case SessionDisconnectReason::ConnectionAborted:
        return "connection_aborted";
    case SessionDisconnectReason::NotConnected:
        return "not_connected";
    case SessionDisconnectReason::BadFileDescriptor:
        return "bad_file_descriptor";
    case SessionDisconnectReason::GreetingReadFailed:
        return "greeting_read_failed";
    case SessionDisconnectReason::GreetingInvalid:
        return "greeting_invalid";
    case SessionDisconnectReason::TransportError:
        return "transport_error";
    case SessionDisconnectReason::LocalTermination:
        return "local_termination";
    case SessionDisconnectReason::Unknown:
    default:
        return "unknown";
    }
}

} // namespace session