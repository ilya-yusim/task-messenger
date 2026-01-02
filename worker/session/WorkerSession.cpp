/**
 * \file worker/session/WorkerSession.cpp
 * \brief Implementation of the worker session controller.
 */
#include "WorkerSession.hpp"
#include "worker/processor/TaskProcessor.hpp"
#include "worker/runtime/IRuntimeMode.hpp"
#include "worker/runtime/BlockingRuntime.hpp"
#include "worker/runtime/AsyncRuntime.hpp"
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstddef>

WorkerSession::WorkerSession(const WorkerOptions& opts, std::shared_ptr<Logger> logger)
    : logger_(logger)
    , processor_(std::move(logger)) // Passing logger to TaskProcessor constructor.
    , host_(opts.host)
    , port_(opts.port)
    , mode_(opts.mode)
{
    // Create runtime based on mode, encapsulating IRuntimeMode behind WorkerSession
    if (mode_ == WorkerMode::Blocking) {
        runtime_ = std::make_shared<BlockingRuntime>(host_, port_, logger_);
    } else {
        runtime_ = std::make_shared<AsyncRuntime>(host_, port_, logger_);
    }
}

void WorkerSession::start() {
    const char* mode_str = (mode_ == WorkerMode::Blocking) ? "blocking" : "async";
    
    // Control thread loop: respond to start/pause/disconnect/shutdown requests
    while (!shutdown_requested_.load(std::memory_order_relaxed)) {
        // Determine if we need a connection
        bool need_connection = !runtime_->is_connected();
        
        if (start_requested_.load(std::memory_order_relaxed) && need_connection) {
            {
                std::lock_guard<std::mutex> lk(status_mtx_);
                connection_status_ = "Connecting";
            }
            
            if (logger_) {
                logger_->info("Worker starting (mode=" + std::string(mode_str) + "), target=" + host_ + ":" + std::to_string(port_) + ", completed=0");
            }
            
            if (!runtime_->connect()) {
                // Connection failed; check if shutdown requested before retrying
                if (shutdown_requested_.load(std::memory_order_relaxed)) {
                    if (logger_) logger_->info("Runtime: shutdown requested during connect; exiting");
                    break;
                }
                // Brief backoff then retry while start still requested
                if (logger_) logger_->warning("Runtime: connect failed; retrying in 1s");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            
            // Connection successful
            {
                std::lock_guard<std::mutex> lk(status_mtx_);
                connection_status_ = "Connected";
            }
            
            if (logger_) {
                std::string local_ep = runtime_->get_local_endpoint();
                logger_->info("Runtime: connected to manager at " + local_ep);
            }
        }

        // Run loop only when start requested and we have a connection
        if (start_requested_.load(std::memory_order_relaxed) && runtime_->is_connected()) {
            if (logger_) logger_->info("Runtime: starting.");

            // Consume start request for this run
            start_requested_.store(false, std::memory_order_relaxed);

            {
                std::lock_guard<std::mutex> lk(status_mtx_);
                connection_status_ = "Running";
            }

            bool success = runtime_->run_loop(processor_);
            
            if (!success) {
                if (logger_) logger_->error("Runtime: run_loop returned error");
            } else {
                // Run loop exited cleanly (paused)
                {
                    std::lock_guard<std::mutex> lk(status_mtx_);
                    connection_status_ = "Paused";
                }
                if (logger_) logger_->info("Runtime paused; awaiting next start request");
            }
        }

        // Check for disconnect request (can happen while running or paused)
        if (disconnect_requested_.load(std::memory_order_relaxed)) {
            if (logger_) logger_->info("Runtime: disconnect requested; closing connection");
            
            runtime_->disconnect();
            disconnect_requested_.store(false, std::memory_order_relaxed);
            
            {
                std::lock_guard<std::mutex> lk(status_mtx_);
                connection_status_ = "Disconnected";
            }
            
            if (logger_) logger_->info("Runtime disconnected; awaiting next start request");
            continue;
        }

        // Idle wait; avoid busy spin
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Shutdown requested: release socket to leave ZeroTier network
    if (logger_) logger_->info("Runtime: shutdown in progress; closing socket");
    runtime_->release();
    {
        std::lock_guard<std::mutex> lk(status_mtx_);
        connection_status_ = "Stopped";
    }
}

// IWorkerService implementation
int WorkerSession::GetTaskCount() {
    return runtime_ ? runtime_->get_task_count() : 0;
}

std::string WorkerSession::GetConnectionStatus() {
    std::lock_guard<std::mutex> lk(status_mtx_);
    return connection_status_;
}

std::string WorkerSession::GetBytesSent() {
    const auto bytes = runtime_ ? runtime_->get_bytes_sent() : 0ULL;
    return format_bytes(bytes);
}

std::string WorkerSession::GetBytesReceived() {
    const auto bytes = runtime_ ? runtime_->get_bytes_received() : 0ULL;
    return format_bytes(bytes);
}

int WorkerSession::GetNumberOfLogLines() {
    return logger_ ? logger_->get_number_of_lines() : 0;
}

std::vector<std::string> WorkerSession::GetLogLines(int start, int count) {
    return logger_ ? logger_->get_lines(start, count) : std::vector<std::string>{};
}

void WorkerSession::shutdown() {
    shutdown_requested_.store(true, std::memory_order_relaxed);
    disconnect_requested_.store(true, std::memory_order_relaxed);
    if (runtime_) {
        runtime_->shutdown();  // Interrupt blocking operations and leave network
    }
}

void WorkerSession::start_runtime() {
    start_requested_.store(true, std::memory_order_relaxed);
}

void WorkerSession::pause_runtime() {
    if (runtime_) runtime_->pause();
}

void WorkerSession::disconnect_runtime() {
    disconnect_requested_.store(true, std::memory_order_relaxed);
    if (runtime_) {
        runtime_->disconnect();  // Close socket to interrupt blocking I/O
    }
}

std::string WorkerSession::format_bytes(std::uint64_t bytes) const {
    // Simple IEC-like formatting (powers of 1024) so UI/log output remains legible
    constexpr std::size_t unit_count = 5;
    static constexpr const char* units[unit_count] = {"B", "KB", "MB", "GB", "TB"};

    double value = static_cast<double>(bytes);
    std::size_t unit_index = 0;
    while (value >= 1024.0 && unit_index + 1 < unit_count) {
        value /= 1024.0;
        ++unit_index;
    }

    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    if (value >= 100.0 || unit_index == 0) {
        oss.precision(0);
    } else {
        oss.precision(1);
    }
    oss << value << units[unit_index];
    return oss.str();
}
