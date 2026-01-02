// SessionManager.cpp - Implementation of multiple session management
#include "SessionManager.hpp"
#include <algorithm>

/**
 * \file manager/session/SessionManager.cpp
 * \brief Implements session orchestration and task pool fan-out.
 * \ingroup session_module
 */

namespace session {

SessionManager::SessionManager(std::shared_ptr<Logger> logger)
    : logger_(std::move(logger))
    , task_pool_(std::make_shared<TaskMessagePool>())
    , next_session_id_(1) {
    
    if (!logger_) {
        throw std::invalid_argument("SessionManager: logger cannot be null");
    }
    
    logger_->info("SessionManager: Created with default config ");
    logger_->info("SessionManager: Initialized task pool");
}

uint32_t SessionManager::create_session(std::shared_ptr<transport::CoroSocketAdapter> client_socket) {
    if (!client_socket) {
        logger_->error("SessionManager: Cannot create session with null client_socket");
        return 0;
    }
    
    uint32_t session_id = generate_session_id();
    
    try {
        // Create the session object with task pool
        auto session = std::make_shared<Session>(client_socket, session_id, logger_, task_pool_);
        
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
    
    logger_->info("SessionManager: Requested termination of " + std::to_string(count) + " sessions");
}

size_t SessionManager::cleanup_completed_sessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
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
    if (task_pool_.get() && !tasks.empty()) {
        logger_->info("SessionManager: Enqueuing " + std::to_string(tasks.size()) + " external tasks");
        task_pool_->add_tasks(std::move(tasks));
        logger_->info("SessionManager: Pool size now: " + std::to_string(task_pool_->size()));
    }
}

std::pair<size_t, size_t> SessionManager::get_task_pool_stats() const {
    if (task_pool_) {
        return {task_pool_->size(), task_pool_->waiting_count()};
    }
    return {0, 0};
}

void SessionManager::print_comprehensive_statistics() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    logger_->info("=== COMPREHENSIVE SESSION STATISTICS ===");
    
    if (active_sessions_.empty()) {
        logger_->info("No active sessions");
        auto [available_tasks, waiting_sessions] = get_task_pool_stats();
        logger_->info("Task Pool: " + std::to_string(available_tasks) + " tasks available");
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
    
    auto [available_tasks, waiting_sessions] = get_task_pool_stats();
    
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
    logger_->info("Task Pool: " + std::to_string(available_tasks) + " available, " + 
                  std::to_string(waiting_sessions) + " sessions waiting");
    logger_->info("========================================");
}

} // namespace session