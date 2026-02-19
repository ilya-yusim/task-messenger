// Session.cpp - Session lifecycle and task handling implementation
#include "Session.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>

/**
 * \file manager/session/Session.cpp
 * \brief Implements coroutine-driven session state machine.
 * \ingroup session_module
 */

namespace session {

Session::Session(std::shared_ptr<transport::CoroSocketAdapter> client_socket,
                 uint32_t session_id,
                 std::shared_ptr<Logger> logger,
                 std::shared_ptr<TaskMessagePool> shared_task_pool)
    : client_socket_(std::move(client_socket))
    , session_id_(session_id)
    , logger_(std::move(logger))
    , shared_task_pool_(std::move(shared_task_pool))
    , state_(SessionState::INITIALIZING)
    , termination_requested_(false) {
    if (!client_socket_ || !logger_ || !shared_task_pool_) {
        throw std::invalid_argument("Session: client_socket, logger, and shared_task_pool cannot be null");
    }
    
    // Initialize statistics
    stats_.start_time = std::chrono::steady_clock::now();
    stats_.tasks_sent = 0;
    stats_.tasks_completed = 0;
    stats_.tasks_failed = 0;
}

void Session::run() {
    initialize_session();
    // Start the session coroutine and store the handle to keep it alive
    session_coroutine_ = std::make_unique<Task<void>>(run_coroutine());
}

bool Session::is_done() const {
    return session_coroutine_ && session_coroutine_->done();
}

Task<void> Session::run_coroutine() {
    try {
        update_state(SessionState::ACTIVE);
        
        // Main task processing loop - pick up tasks from pool
        while (is_active() && !termination_requested_.load()) {
            TaskMessage task; // Declare outside try block so we can requeue on error
            bool task_acquired = false;

            try {
                logger_->debug("Session " + std::to_string(session_id_) + ": Awaiting task from shared pool");

                // Get next task from shared pool (suspends if empty)
                task = co_await shared_task_pool_->get_next_task();
                task_acquired = true;
                
                // Check if we got a valid task (pool might be shutting down)
                if (!task.is_valid()) {
                    logger_->info("Session " + std::to_string(session_id_) + ": No more tasks available or pool shutting down");
                    break;
                }

                // Send the task to the worker
                record_task_sent();

                auto wire_header = task.header_view();
                const auto payload_bytes = task.payload_bytes();
                assert(wire_header.body_size == payload_bytes.size() &&
                       "TaskMessage invariant broke: header/body size mismatch");

                logger_->debug(
                    "Session " + std::to_string(session_id_) +
                    ": Sending task " + std::to_string(task.task_id()) +
                    " (" + std::to_string(payload_bytes.size()) + " bytes payload)");

                // Start timing just before we begin network I/O to exclude pool wait time
                auto rt_start = std::chrono::steady_clock::now();

                // Scatter-send: send header and payload separately (TCP_NODELAY enabled)
                const auto [header_span, payload_span] = task.wire_bytes();
                co_await client_socket_->async_write(header_span.data(), header_span.size());
                if (!payload_span.empty()) {
                    co_await client_socket_->async_write(payload_span.data(), payload_span.size());
                }
                stats_.bytes_sent += header_span.size() + payload_span.size();

                // Receive response header from worker
                TaskHeader response{};
                co_await client_socket_->async_read_header(&response, sizeof(response));
                stats_.bytes_received += sizeof(response);

                if (response.task_id != task.task_id()) {
                    logger_->warning("Session " + std::to_string(session_id_) + ": Response task ID mismatch. Expected: " +
                                     std::to_string(task.task_id()) + ", Got: " + std::to_string(response.task_id));
                    record_task_failed();
                    // Requeue the task to shared pool since we didn't get a proper response
                    shared_task_pool_->add_task(task);
                    continue;
                }

                // Receive response body if present
                std::string response_body;
                if (response.body_size > 0) {
                    response_body.resize(response.body_size);
                    co_await client_socket_->async_read(response_body.data(), response_body.size());
                    stats_.bytes_received += response_body.size();
                }

                // Measure full task roundtrip time
                auto rt_end = std::chrono::steady_clock::now();
                const auto rt_span = std::chrono::duration_cast<std::chrono::nanoseconds>(rt_end - rt_start);
                stats_.total_task_roundtrip_time += rt_span;
                stats_.last_task_roundtrip_time = rt_span;
                stats_.timed_tasks += 1;

                // Check response success
                if (response.skill_id == wire_header.skill_id) {
                    record_task_completed();
                    logger_->debug("Session " + std::to_string(session_id_) +
                                    ": Task " + std::to_string(task.task_id()) + " completed successfully");
                } else {
                    record_task_failed();
                    logger_->warning("Session " + std::to_string(session_id_) + ": Task " + std::to_string(task.task_id()) +
                                     " received mismatched skill_id (expected " + std::to_string(wire_header.skill_id) +
                                     ", got " + std::to_string(response.skill_id) + ")");
                    // Requeue the task to shared pool for retry
                    shared_task_pool_->add_task(task);
                    continue;
                }

                logger_->debug("Session " + std::to_string(session_id_) +
                                ": Worker response skill_id " + std::to_string(response.skill_id) +
                                ", payload " + std::to_string(response.body_size) + " bytes");
            } catch (const std::system_error& e) {
                // Requeue task to shared pool if we acquired it but failed during I/O
                if (task_acquired && task.is_valid()) {
                    logger_->warning("Session " + std::to_string(session_id_) + ": I/O error for task " +
                                     std::to_string(task.task_id()) + ", requeuing: " + std::string(e.what()));
                    shared_task_pool_->add_task(task);
                    record_task_failed();
                }

                if (e.code() == std::errc::not_connected ||
                    e.code() == std::errc::connection_reset ||
                    e.code() == std::errc::connection_aborted ||
                    e.code() == std::errc::bad_file_descriptor) {
                    logger_->info("Session " + std::to_string(session_id_) + ": Connection lost: " + std::string(e.what()));
                    update_state(SessionState::TERMINATED);
                    finalize_session();
                    co_return;
                }

                logger_->error("Session " + std::to_string(session_id_) + ": I/O error: " + std::string(e.what()));
                update_state(SessionState::ERROR_STATE);
                finalize_session();
                co_return;
            } catch (const std::exception& e) {
                // Requeue task to shared pool if we acquired it but failed during processing
                if (task_acquired && task.is_valid()) {
                    logger_->warning("Session " + std::to_string(session_id_) + ": Exception for task " +
                                     std::to_string(task.task_id()) + ", requeuing: " + std::string(e.what()));
                    shared_task_pool_->add_task(task);
                    record_task_failed();
                }
                logger_->error("Session " + std::to_string(session_id_) + ": Exception processing task: " + std::string(e.what()));
                
                // Continue processing further tasks -- don't terminate session on a single task error
                continue;
            }
        }

        update_state(SessionState::TERMINATED);
        logger_->info("Session " + std::to_string(session_id_) + ": Task processing loop completed");
        
        finalize_session();
    } catch (const std::exception& e) {
        logger_->error("Session " + std::to_string(session_id_) + ": run coroutine failed: " + std::string(e.what()));
        update_state(SessionState::ERROR_STATE);
        finalize_session();
    }

    co_return;
}

std::string Session::get_client_endpoint() const {
    if (!client_socket_) {
        return "disconnected";
    }
    auto endpoint = client_socket_->remote_endpoint();
    return endpoint.empty() ? std::string("unknown") : endpoint;
}

bool Session::is_active() const {
    const auto state = state_.load();
    return (state == SessionState::ACTIVE || state == SessionState::COMPLETING) &&
           client_socket_ && client_socket_->is_open() && !termination_requested_.load();
}

void Session::request_termination() {
    termination_requested_.store(true);
    update_state(SessionState::COMPLETING);
    if (client_socket_) {
        client_socket_->shutdown();
    }
}

bool Session::is_completed() const {
    const auto state = state_.load();
    return state == SessionState::TERMINATED || state == SessionState::ERROR_STATE;
}

void Session::initialize_session() {
    stats_.start_time = std::chrono::steady_clock::now();
    stats_.tasks_sent = 0;
    stats_.tasks_completed = 0;
    stats_.tasks_failed = 0;
    stats_.bytes_sent = 0;
    stats_.bytes_received = 0;
    stats_.total_task_roundtrip_time = std::chrono::nanoseconds{0};
    stats_.last_task_roundtrip_time = std::chrono::nanoseconds{0};
    stats_.timed_tasks = 0;
}

void Session::finalize_session() {
    if (client_socket_) {
        client_socket_->shutdown();
        client_socket_->close();
    }
    
    // Compute timing summary (ms)
    const double total_rt_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(stats_.total_task_roundtrip_time).count();
    const double last_rt_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(stats_.last_task_roundtrip_time).count();
    const double avg_rt_ms = stats_.get_avg_roundtrip_ms();

    logger_->info("Session " + std::to_string(session_id_) + ": Finalized. Stats - Sent: " + 
                  std::to_string(stats_.tasks_sent) + ", Completed: " + std::to_string(stats_.tasks_completed) + 
                  ", Failed: " + std::to_string(stats_.tasks_failed) + 
                  ", Success Rate: " + std::to_string(stats_.get_success_rate()) + "%" +
                  ", Timed Tasks: " + std::to_string(stats_.timed_tasks) +
                  ", Roundtrip (ms): total=" + std::to_string(total_rt_ms) +
                  ", avg=" + std::to_string(avg_rt_ms) +
                  ", last=" + std::to_string(last_rt_ms) +
                  ", Bytes: sent=" + std::to_string(stats_.bytes_sent) +
                  ", recv=" + std::to_string(stats_.bytes_received));
}

void Session::update_state(SessionState new_state) {
    state_.store(new_state);
}

void Session::record_task_sent() {
    stats_.tasks_sent++;
}

void Session::record_task_completed() {
    stats_.tasks_completed++;
}

void Session::record_task_failed() {
    stats_.tasks_failed++;
}

} // namespace session