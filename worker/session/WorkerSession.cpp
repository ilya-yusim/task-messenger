/**
 * \file worker/session/WorkerSession.cpp
 * \brief Implementation of the worker session controller.
 */
#include "WorkerSession.hpp"
#include "worker/processor/TaskProcessor.hpp"
#include "worker/runtime/IRuntimeMode.hpp"
#include "worker/runtime/BlockingRuntime.hpp"
#include "worker/runtime/AsyncRuntime.hpp"
#include "rendezvous/RendezvousClient.hpp"
#include "rendezvous/RendezvousOptions.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstddef>
#include <algorithm>

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

WorkerSession::DiscoveryResult WorkerSession::try_discover_dispatcher() {
    if (!rendezvous_client_) {
        // Rendezvous disabled (or pre-start() state): caller uses opts.host/port.
        return DiscoveryResult::Disabled;
    }

    using namespace std::chrono_literals;
    {
        std::lock_guard<std::mutex> lk(status_mtx_);
        connection_status_ = "Discovering";
    }
    if (logger_) logger_->info("Rendezvous: discovery started for role=dispatcher");

    auto backoff = 1s;
    constexpr auto kMaxBackoff = 5s;

    // Interruptible sleep: returns true if slept the full duration, false on
    // shutdown/disconnect.
    auto interruptible_sleep = [this](std::chrono::seconds total) -> bool {
        const auto slice = std::chrono::milliseconds(100);
        const auto deadline = std::chrono::steady_clock::now() + total;
        while (std::chrono::steady_clock::now() < deadline) {
            if (shutdown_requested_.load(std::memory_order_relaxed)) return false;
            if (disconnect_requested_.load(std::memory_order_relaxed)) return false;
            std::this_thread::sleep_for(slice);
        }
        return true;
    };

    while (true) {
        if (shutdown_requested_.load(std::memory_order_relaxed) ||
            disconnect_requested_.load(std::memory_order_relaxed)) {
            return DiscoveryResult::Cancelled;
        }

        nlohmann::json result;
        bool ok = rendezvous_client_->discover_endpoint("dispatcher", result);

        if (ok) {
            const bool found = result.value("found", false);
            const bool stale = result.value("stale", false);
            std::string new_host = result.value("vn_host", "");
            int new_port = result.value("vn_port", 0);

            if (!found) {
                if (logger_) logger_->debug("Rendezvous: no dispatcher registered yet");
            } else if (stale) {
                if (logger_) logger_->debug("Rendezvous: dispatcher endpoint stale; waiting for refresh");
            } else if (new_host.empty() || new_port == 0) {
                if (logger_) logger_->debug("Rendezvous: invalid endpoint in discovery response");
            } else {
                if (logger_) logger_->info("Rendezvous: dispatcher resolved at " + new_host + ":" + std::to_string(new_port));
                if (new_host != host_ || new_port != port_) {
                    host_ = new_host;
                    port_ = new_port;
                    if (mode_ == WorkerMode::Blocking) {
                        runtime_ = std::make_shared<BlockingRuntime>(host_, port_, logger_);
                    } else {
                        runtime_ = std::make_shared<AsyncRuntime>(host_, port_, logger_);
                    }
                    return DiscoveryResult::Updated;
                }
                return DiscoveryResult::Unchanged;
            }
        } else {
            if (logger_) logger_->debug("Rendezvous: discovery exchange failed");
        }

        // Back off before retrying. Interruptible so shutdown/disconnect unblock us.
        if (!interruptible_sleep(backoff)) {
            return DiscoveryResult::Cancelled;
        }
        backoff = std::min(backoff * 2, kMaxBackoff);
    }
}

void WorkerSession::start() {
    const char* mode_str = (mode_ == WorkerMode::Blocking) ? "blocking" : "async";
    const bool rv_enabled = rendezvous_opts::get_enabled().value_or(false);

    // Strict rendezvous mode: validate config before any discovery.
    // No fallback to the configured dispatcher host/port when rendezvous is enabled.
    // The VN lease is acquired lazily by the first RendezvousClient socket connect.
    if (rv_enabled) {
        auto rv_host = rendezvous_opts::get_host();
        auto rv_port = rendezvous_opts::get_port();
        if (!rv_host || rv_host->empty() || !rv_port) {
            if (logger_) logger_->error("Rendezvous enabled but no host/port configured; exiting");
            std::lock_guard<std::mutex> lk(status_mtx_);
            connection_status_ = "Error";
            return;
        }

        // Persistent rendezvous client. Its first socket connect will acquire
        // the VN lease, so no explicit join step is needed here.
        rendezvous_client_ = std::make_shared<rendezvous::RendezvousClient>(
            *rv_host, *rv_port, logger_);
    }

    // Attempt rendezvous discovery before first connection.
    {
        auto disc = try_discover_dispatcher();
        if (disc == DiscoveryResult::Cancelled) {
            // Shutdown or UI disconnect fired during blocking discovery.
            if (disconnect_requested_.load(std::memory_order_relaxed) &&
                !shutdown_requested_.load(std::memory_order_relaxed)) {
                // Option (a): treat as disconnect — clear start request, idle.
                disconnect_requested_.store(false, std::memory_order_relaxed);
                start_requested_.store(false, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lk(status_mtx_);
                connection_status_ = "Disconnected";
            }
            // shutdown_requested_ (if set) will exit the while loop below.
        }
    }

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
            
            // Connection successful — reset backoff
            reconnect_delay_ = std::chrono::seconds{1};
            {
                std::lock_guard<std::mutex> lk(status_mtx_);
                connection_status_ = "Connected";
            }
            
            if (logger_) {
                std::string local_ep = runtime_->get_local_endpoint();
                logger_->info("Runtime: connected to dispatcher at " + local_ep);
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
                // Was this a user-requested disconnect (socket closed externally)?
                if (runtime_->was_disconnect_requested()) {
                    disconnect_requested_.store(false, std::memory_order_relaxed);
                    start_requested_.store(false, std::memory_order_relaxed);
                    reconnect_delay_ = std::chrono::seconds{1};
                    {
                        std::lock_guard<std::mutex> lk(status_mtx_);
                        connection_status_ = "Disconnected";
                    }
                    if (logger_) logger_->info("Runtime disconnected; awaiting next start request");
                    continue;
                }

                // Genuine I/O error (dispatcher offline, connection lost, etc.)
                if (logger_) logger_->error("Runtime: run_loop returned error; will reconnect in "
                    + std::to_string(reconnect_delay_.count()) + "s");

                // Close the dirty socket so the next connect() starts fresh
                runtime_->disconnect();

                {
                    std::lock_guard<std::mutex> lk(status_mtx_);
                    connection_status_ = "Disconnected";
                }

                // Re-discover dispatcher in case it moved to a new endpoint.
                // In rendezvous mode this blocks (with backoff) until resolved
                // or cancelled; it replaces the direct-connect reconnect sleep.
                auto disc = try_discover_dispatcher();
                if (disc == DiscoveryResult::Cancelled) {
                    if (disconnect_requested_.load(std::memory_order_relaxed) &&
                        !shutdown_requested_.load(std::memory_order_relaxed)) {
                        disconnect_requested_.store(false, std::memory_order_relaxed);
                        start_requested_.store(false, std::memory_order_relaxed);
                        std::lock_guard<std::mutex> lk(status_mtx_);
                        connection_status_ = "Disconnected";
                    }
                    continue;
                }

                // Re-arm so the outer loop retries connection
                start_requested_.store(true, std::memory_order_relaxed);

                if (disc == DiscoveryResult::Disabled) {
                    // Direct-connect mode: pace reconnect attempts.
                    std::this_thread::sleep_for(reconnect_delay_);
                    reconnect_delay_ = std::min(reconnect_delay_ * 2, kMaxReconnectDelay);
                }
                continue;
            } else {
                // Run loop exited cleanly (paused) — reset backoff
                reconnect_delay_ = std::chrono::seconds{1};
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
            start_requested_.store(false, std::memory_order_relaxed); // Ensure we do not reconnect until user requests start
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
    if (rendezvous_client_) {
        rendezvous_client_->cancel();  // Interrupt any blocked discover_endpoint()
    }
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
    if (rendezvous_client_) {
        rendezvous_client_->cancel();  // Abort discovery if it's currently blocked
    }
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
