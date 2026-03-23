// DispatcherApp.cpp - Application harness for dispatcher infrastructure startup
#include "DispatcherApp.hpp"
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
        return 1; // help/version already printed — caller should exit(0)
    }
    if (parse_res == shared_opts::Options::ParseResult::Error) {
        logger_->error(std::string("Failed to parse options: ") + opts_err);
        return 2;
    }

    // --- Stage 3: Bring up transport server subsystem ---
    logger_->info("Transport server starting...");

    server_ = std::make_unique<AsyncTransportServer>(logger_);
    if (!server_->start()) {
        logger_->error("Failed to start transport server");
        return 3;
    }
    logger_->info("Transport server started successfully");

    // --- Stage 4: Install signal handlers ---
    install_signal_handlers();

    return 0;
}

void DispatcherApp::stop() {
    if (server_) {
        logger_->info("Shutting down server...");
        server_->stop();
        logger_->info("Server shut down successfully");
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
    return ::submit_task(server_->task_pool(), server_->response_context(),
                         task_id, std::move(request), std::move(response_buffer));
}

size_t DispatcherApp::task_pool_size() const {
    return server_ ? server_->task_pool()->size() : 0;
}

void DispatcherApp::print_statistics() const {
    if (server_) {
        server_->print_transporter_statistics();
    }
}

std::shared_ptr<Logger> DispatcherApp::logger() const {
    return logger_;
}

void DispatcherApp::install_signal_handlers() {
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
}
