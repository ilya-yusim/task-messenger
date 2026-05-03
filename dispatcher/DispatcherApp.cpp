// DispatcherApp.cpp - Application harness for dispatcher infrastructure startup
#include "DispatcherApp.hpp"
#include "monitoring/MonitoringOptions.hpp"
#include "monitoring/SnapshotReporter.hpp"
#include "options/Options.hpp"
#include "processUtils.hpp"
#include "rendezvous/RendezvousClient.hpp"
#include "rendezvous/RendezvousOptions.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <string>
#include <thread>

std::atomic<bool> DispatcherApp::s_shutdown_requested{false};

void DispatcherApp::signal_handler(int signum) {
    (void)signum;
    s_shutdown_requested.store(true, std::memory_order_relaxed);
}

DispatcherApp::DispatcherApp() = default;
DispatcherApp::~DispatcherApp() = default;

int DispatcherApp::start(int argc, char* argv[]) {
    lifecycle_state_.store(LifecycleState::Starting, std::memory_order_relaxed);

    // Reset shutdown flag so re-starts (e.g., in tests) don't exit immediately
    s_shutdown_requested.store(false, std::memory_order_relaxed);

    // Build the logging pipeline.
    logger_ = std::make_shared<Logger>("DispatcherApp");
    auto stdout_sink = std::make_shared<StdoutSink>();
    stdout_sink->set_level(LogLevel::Info);
    logger_->add_sink(stdout_sink);

    // Parse CLI/JSON options. All options auto-register via static objects;
    // no manual call is needed. The command line and JSON config are parsed
    // once for the entire process.
    std::string opts_err;
    auto parse_res = shared_opts::Options::load_and_parse(argc, argv, opts_err);
    if (parse_res == shared_opts::Options::ParseResult::Help ||
        parse_res == shared_opts::Options::ParseResult::Version) {
        lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_relaxed);
        return 1; // help/version already printed — caller should exit(0)
    }
    if (parse_res == shared_opts::Options::ParseResult::Error) {
        logger_->error(std::string("Failed to parse options: ") + opts_err);
        lifecycle_state_.store(LifecycleState::Error, std::memory_order_relaxed);
        return 2;
    }

    // Bring up the transport server subsystem.
    logger_->info("Transport server starting...");
    start_time_ = std::chrono::steady_clock::now();

    server_ = std::make_unique<AsyncTransportServer>(logger_);
    if (!server_->start()) {
        logger_->error("Failed to start transport server");
        lifecycle_state_.store(LifecycleState::Error, std::memory_order_relaxed);
        return 3;
    }
    logger_->info("Transport server started successfully");

    // Monitoring API is optional and non-fatal for dispatcher task execution.
    if (monitoring_opts::get_enabled().value_or(true)) {
        monitoring_service_ = std::make_unique<monitoring::MonitoringService>(
            logger_,
            *server_,
            [this]() { return uptime_seconds(); },
            [this]() { return lifecycle_state_string(); });
        if (!monitoring_service_->start()) {
            logger_->warning("Monitoring service failed to start; continuing without monitoring API");
            monitoring_service_.reset();
        }
    } else {
        logger_->info("Monitoring service disabled by configuration");
    }

    // Register with the rendezvous service if configured (optional, non-fatal).
    if (rendezvous_opts::get_enabled().value_or(false)) {
        auto rv_host = rendezvous_opts::get_host().value_or(std::string{});
        auto rv_port = rendezvous_opts::get_port().value_or(8088);
        auto rv_snapshot_port = rendezvous_opts::get_snapshot_port().value_or(8089);

        if (!rv_host.empty()) {
            rendezvous_client_ = std::make_shared<rendezvous::RendezvousClient>(
                rv_host, rv_port, logger_);

            // Snapshot relay uses a dedicated VN port so a long-lived snapshot
            // connection can't block registration/discovery exchanges.
            snapshot_reporter_ = std::make_shared<monitoring::SnapshotReporter>(
                rv_host, rv_snapshot_port, logger_);

            // Share the reporter with monitoring service immediately; the
            // reporter thread tolerates transient failures while the
            // rendezvous server is unreachable.
            if (monitoring_service_) {
                monitoring_service_->set_snapshot_reporter(snapshot_reporter_);
            }

            // Determine this dispatcher's VN-visible address and listen port
            // from the transport server's bound local endpoint (transport-agnostic).
            std::string vn_ip;
            int listen_port = 0;
            if (auto stream = server_->server_stream()) {
                auto ep = stream->local_endpoint(); // "ip:port"
                if (auto colon = ep.rfind(':'); colon != std::string::npos) {
                    vn_ip = ep.substr(0, colon);
                    listen_port = std::stoi(ep.substr(colon + 1));
                }
            }

            if (!vn_ip.empty()) {
                // Run registration on a background thread so startup is not blocked
                // by rendezvous availability. The loop retries until success or
                // shutdown is requested; tasks queued in the meantime simply wait
                // for a worker to connect.
                registration_thread_ = std::thread(
                    &DispatcherApp::registration_loop, this,
                    std::move(rv_host), rv_port, std::move(vn_ip), listen_port);
            } else {
                logger_->warning("Could not determine VN IP address; skipping rendezvous registration");
            }
        } else {
            logger_->warning("Rendezvous enabled but no host configured; skipping");
        }
    }

    // Install signal handlers.
    install_signal_handlers();

    lifecycle_state_.store(LifecycleState::Running, std::memory_order_relaxed);

    return 0;
}

void DispatcherApp::stop() {
    lifecycle_state_.store(LifecycleState::Stopping, std::memory_order_relaxed);

    // Signal shutdown to all background loops.
    s_shutdown_requested.store(true, std::memory_order_relaxed);

    // Cancel both rendezvous clients first: this interrupts any in-flight
    // I/O in the registration thread (RendezvousClient) and in the monitoring
    // reporter thread (SnapshotReporter). Subsequent calls return immediately,
    // so neither thread can block shutdown.
    if (rendezvous_client_) {
        rendezvous_client_->cancel();
    }
    if (snapshot_reporter_) {
        snapshot_reporter_->cancel();
    }

    // Stop monitoring before joining the registration thread. The reporter
    // thread ticks every second and would otherwise keep calling report()
    // until its own join, which happens inside monitoring_service_->stop().
    if (monitoring_service_) {
        logger_->info("Shutting down monitoring service...");
        monitoring_service_->stop();
        monitoring_service_.reset();
    }

    // Join the registration thread now that no other thread is touching the
    // rendezvous client.
    if (registration_thread_.joinable()) {
        registration_thread_.join();
    }

    // Best-effort unregister from rendezvous before tearing down transport.
    if (rendezvous_client_) {
        if (registered_.load(std::memory_order_acquire)) {
            try {
                rendezvous_client_->unregister_endpoint("dispatcher", "default");
            } catch (...) {}
        }
        rendezvous_client_.reset();
    }
    snapshot_reporter_.reset();

    if (server_) {
        logger_->info("Shutting down server...");
        server_->stop();
        logger_->info("Server shut down successfully");
    }

    lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_relaxed);
}

void DispatcherApp::registration_loop(std::string rv_host, int rv_port,
                                      std::string vn_ip, int listen_port) {
    try { ProcessUtils::set_current_thread_name("RV-Registration"); } catch (...) {}

    using namespace std::chrono_literals;
    auto delay = 1s;
    constexpr auto max_delay = 10s;

    while (!s_shutdown_requested.load(std::memory_order_relaxed)) {
        bool ok = false;
        try {
            ok = rendezvous_client_ &&
                 rendezvous_client_->register_endpoint(
                     "dispatcher", "default", vn_ip, listen_port);
        } catch (...) {
            ok = false;
        }

        if (ok) {
            registered_.store(true, std::memory_order_release);
            if (logger_) {
                logger_->info("Registered with rendezvous service at "
                              + rv_host + ":" + std::to_string(rv_port));
            }
            return;
        }

        if (logger_) {
            logger_->warning("Rendezvous registration failed; retrying in "
                             + std::to_string(delay.count()) + "s");
        }

        // Sleep in short slices so shutdown is observed promptly.
        const auto deadline = std::chrono::steady_clock::now() + delay;
        while (std::chrono::steady_clock::now() < deadline) {
            if (s_shutdown_requested.load(std::memory_order_relaxed)) return;
            std::this_thread::sleep_for(100ms);
        }

        delay = std::min(delay * 2, max_delay);
    }
}

bool DispatcherApp::shutdown_requested() const {
    return s_shutdown_requested.load(std::memory_order_relaxed);
}

void DispatcherApp::request_shutdown() {
    s_shutdown_requested.store(true, std::memory_order_relaxed);
}

TaskSubmitAwaitable DispatcherApp::submit_task(
    uint32_t task_id,
    std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> request,
    std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> response_buffer) {
    return ::submit_task(server_->task_queue(), server_->response_context(),
                         task_id, std::move(request), std::move(response_buffer));
}

size_t DispatcherApp::task_queue_size() const {
    return server_ ? server_->task_queue()->size() : 0;
}

void DispatcherApp::print_statistics() const {
    if (server_) {
        server_->print_transporter_statistics();
    }
}

std::shared_ptr<Logger> DispatcherApp::logger() const {
    return logger_;
}

uint64_t DispatcherApp::uptime_seconds() const {
    const auto elapsed = std::chrono::steady_clock::now() - start_time_;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
}

DispatcherApp::LifecycleState DispatcherApp::lifecycle_state() const {
    const auto base_state = lifecycle_state_.load(std::memory_order_relaxed);
    if (base_state != LifecycleState::Running || !server_) {
        return base_state;
    }

    const auto queued_tasks = server_->get_task_queue_size();
    if (queued_tasks == 0) {
        return LifecycleState::NoTasks;
    }

    const auto active_sessions = server_->get_active_session_count();
    if (active_sessions == 0) {
        return LifecycleState::NoWorkers;
    }

    return LifecycleState::Running;
}

std::string DispatcherApp::lifecycle_state_string() const {
    switch (lifecycle_state()) {
    case LifecycleState::Starting:
        return "starting";
    case LifecycleState::Running:
        return "running";
    case LifecycleState::NoTasks:
        return "no_tasks";
    case LifecycleState::NoWorkers:
        return "no_workers";
    case LifecycleState::Stopping:
        return "stopping";
    case LifecycleState::Stopped:
        return "stopped";
    case LifecycleState::Error:
        return "error";
    default:
        return "unknown";
    }
}

void DispatcherApp::install_signal_handlers() {
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
}
