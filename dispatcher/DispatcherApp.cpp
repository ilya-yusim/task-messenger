// DispatcherApp.cpp - Application harness for dispatcher infrastructure startup
#include "DispatcherApp.hpp"
#include "monitoring/MonitoringOptions.hpp"
#include "options/Options.hpp"

#include <csignal>
#include <string>

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

    // --- Stage 1: Build logging pipeline ---
    logger_ = std::make_shared<Logger>("DispatcherApp");
    auto stdout_sink = std::make_shared<StdoutSink>();
    stdout_sink->set_level(LogLevel::Info);
    logger_->add_sink(stdout_sink);

    // --- Stage 2: Parse CLI/JSON options ---
    // All options auto-register via static objects; no manual call needed.
    // Parse the command line and JSON config once for the entire process.
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

    // --- Stage 3: Bring up transport server subsystem ---
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

    // --- Stage 4: Install signal handlers ---
    install_signal_handlers();

    lifecycle_state_.store(LifecycleState::Running, std::memory_order_relaxed);

    return 0;
}

void DispatcherApp::stop() {
    lifecycle_state_.store(LifecycleState::Stopping, std::memory_order_relaxed);

    // Stop monitoring first because it depends on dispatcher-owned runtime objects.
    if (monitoring_service_) {
        logger_->info("Shutting down monitoring service...");
        monitoring_service_->stop();
        monitoring_service_.reset();
    }

    if (server_) {
        logger_->info("Shutting down server...");
        server_->stop();
        logger_->info("Server shut down successfully");
    }

    lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_relaxed);
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
