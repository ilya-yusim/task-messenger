#include "AsyncTransportServer.hpp"
#include "../session/SessionManager.hpp"
#include "processUtils.hpp"
#include <utility>

// Accessors provided by transporter options provider (auto-registered once)
namespace transport_server_opts { std::optional<std::string> get_listen_host(); std::optional<int> get_listen_port(); std::optional<int> get_io_threads(); }

AsyncTransportServer::AsyncTransportServer(std::shared_ptr<Logger> logger)
    : logger_(std::move(logger)), running_(false) {
    session_manager_ = std::make_unique<session::SessionManager>(logger_);
}

AsyncTransportServer::~AsyncTransportServer() = default;

bool AsyncTransportServer::start(const std::string& host, int port, int backlog) {
    if (running_) return true;
    running_ = true;

    io_ = std::make_shared<transport::CoroIoContext>();
    io_->set_logger(logger_);
    // Determine IO threads from options (fallback to 1 if not set or invalid)
    size_t threads = 1;
    if (auto opt = transport_server_opts::get_io_threads()) {
        if (*opt > 0) threads = static_cast<size_t>(*opt);
    }
    io_->start(threads);
    io_guard_.emplace(io_->make_work_guard());

    server_socket_ = transport::CoroSocketAdapter::create_server(logger_, io_);

    if (!server_socket_->start_listening(host, port, backlog)) {
        if (logger_) {
            logger_->error("AsyncTransportServer: failed to start listening on " + host + ":" + std::to_string(port));
        }
        running_ = false;
        io_guard_.reset();
        io_->stop();
        return false;
    }

    // Store endpoint for shutdown wake-up
    listen_host_ = host;
    listen_port_ = port;

    // Initialize maintenance timestamp and start dedicated acceptor thread
    last_maintenance_run_ = std::chrono::steady_clock::now();
    start_acceptor_thread();
    logger_->info("AsyncTransportServer: listening on " + host + ":" + std::to_string(port) + ", io_threads=" + std::to_string(threads));
    return true;
}

bool AsyncTransportServer::start(int backlog) {
    auto host = transport_server_opts::get_listen_host().value_or(std::string("0.0.0.0"));
    auto port = transport_server_opts::get_listen_port().value_or(8080);
    if (logger_) logger_->info("AsyncTransportServer: resolved listen endpoint " + host + ":" + std::to_string(port));
    return start(host, port, backlog);
}

void AsyncTransportServer::stop() noexcept {
    running_ = false;
    // Wake-up connect disabled; rely on periodic timeout from blocking_accept
    // Join acceptor BEFORE closing the listening socket to avoid races inside lwIP
    if (acceptor_thread_.joinable()) acceptor_thread_.join();
    // Now it's safe to close the listening socket
    if (server_socket_) {
        std::error_code ignore;
        try { server_socket_->close(); } catch (...) {}
    }
    io_guard_.reset();
    if (io_) io_->stop();
    cleanup_closed_connections();
    session_manager_->cleanup_completed_sessions();
    logger_->info("AsyncTransportServer: stopped");
}

void AsyncTransportServer::enqueue_tasks(std::vector<TaskMessage> tasks) {
    session_manager_->enqueue_tasks(std::move(tasks));
    // Opportunistic maintenance in case there are no incoming accepts
    maybe_run_maintenance();
}

std::pair<size_t, size_t> AsyncTransportServer::get_task_pool_stats() const {
    return session_manager_->get_task_pool_stats();
}

void AsyncTransportServer::print_transporter_statistics() const noexcept {
    try {
        if (io_) {
            const auto total = io_->get_total_operations_processed();
            const auto per_thread = io_->get_operations_processed_per_thread();
            if (logger_) {
                std::string msg = "IO stats: total=" + std::to_string(total) + ", per-thread=[";
                for (size_t i = 0; i < per_thread.size(); ++i) {
                    msg += (i ? ", " : "") + std::to_string(i) + ":" + std::to_string(per_thread[i]);
                }
                msg += "]";
                logger_->info(msg);
            }
            io_->log_detailed_statistics();
        }
        if (session_manager_) {
            session_manager_->print_comprehensive_statistics();
        }
    } catch (const std::exception& e) {
        if (logger_) logger_->error(std::string("print_transporter_statistics error: ") + e.what());
    } catch (...) {
        if (logger_) logger_->error("print_transporter_statistics unknown error");
    }
}

void AsyncTransportServer::start_acceptor_thread() {
    acceptor_thread_ = std::thread([this]() {
        try { ProcessUtils::set_current_thread_name("TransporterAcceptor"); } catch (...) {}
        // Accept Loop Design:
        //   We use a timed blocking accept (500ms default) offered by the socket implementation.
        //   This leverages the TCP/IP stack's internal sleep/wake behavior instead of manual
        //   polling with try_accept + ad hoc delays. The result: near-zero CPU usage when idle,
        //   simple logic here, and bounded shutdown latency (< timeout). Transient wake-ups
        //   (timeout, would-block, aborted, shutdown) return nullptr with error cleared, allowing
        //   us to re-check the running_ flag and proceed without noisy logs.
        //   Tuning: Increase timeout to reduce wake frequency (lower CPU), decrease for faster
        //   shutdown reaction. 500ms is a pragmatic middle ground.
        while (running_) {
            try {
                std::error_code ec;
                auto client = server_socket_->blocking_accept(ec, std::chrono::milliseconds(500));
                if (!client) { 
                    if (ec && running_) {
                        if (logger_) logger_->error(std::string("AsyncTransportServer: accept error: ") + ec.message());
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    continue;
                }
                // If we're stopping, close the just-accepted connection and exit loop
                if (!running_) {
                    try { client->close(); } catch (...) {}
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(connections_mutex_);
                    active_connections_.push_back(client);
                }
                (void)session_manager_->create_session(client);
                maybe_run_maintenance();
            } catch (const std::exception& e) {
                if (logger_) logger_->error(std::string("AsyncTransportServer: accept exception: ") + e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });
}

// Wake-up helper removed: periodic timeout in blocking_accept handles shutdown responsiveness.

void AsyncTransportServer::maybe_run_maintenance() noexcept {
    using namespace std::chrono;
    constexpr auto interval = std::chrono::milliseconds(2000);
    const auto now = steady_clock::now();
    if (now - last_maintenance_run_ >= interval) {
        session_manager_->cleanup_completed_sessions();
        cleanup_closed_connections();
        last_maintenance_run_ = now;
    }
}

void AsyncTransportServer::cleanup_closed_connections() noexcept {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto before = active_connections_.size();
    active_connections_.erase(
        std::remove_if(active_connections_.begin(), active_connections_.end(),
                        [](const std::shared_ptr<transport::CoroSocketAdapter>& c) {
                            return !c || !c->is_open();
                        }),
        active_connections_.end());
    auto cleaned = before - active_connections_.size();
    if (cleaned) {
        logger_->debug("AsyncTransportServer: cleaned " + std::to_string(cleaned) + " closed connections");
    }
}


