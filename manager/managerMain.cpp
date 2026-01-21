// managerMain.cpp - Main AsyncTransportServer application for asynchronous transfer of tasks to workers.
#include "transport/AsyncTransportServer.hpp"
#include "TaskGenerator.hpp"
#include "options/Options.hpp"

#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>

// Global shutdown flag for signal handlers
static std::atomic<bool> shutdown_requested{false};

// Signal handler for graceful shutdown
static void signal_handler(int signum) {
    shutdown_requested.store(true, std::memory_order_relaxed);
}

// Monitoring thread function: auto-refill when pool size < 10
static void monitoring_thread_func(AsyncTransportServer* server, 
                                   DefaultTaskGenerator* generator,
                                   std::shared_ptr<Logger> logger) {
    constexpr size_t LOW_THRESHOLD = 10;
    constexpr size_t REFILL_AMOUNT = 100;
    constexpr auto POLL_INTERVAL = std::chrono::seconds(1);
    
    while (!shutdown_requested.load(std::memory_order_relaxed)) {
        auto [pool_size, waiting_sessions] = server->get_task_pool_stats();
        
        if (pool_size < LOW_THRESHOLD) {
            logger->info("Task pool low (" + std::to_string(pool_size) + 
                        " tasks), generating " + std::to_string(REFILL_AMOUNT) + " more");
            
            auto tasks = generator->make_tasks(REFILL_AMOUNT);
            server->enqueue_tasks(std::move(tasks));
            
            logger->info("Refill complete, pool now has " + 
                        std::to_string(server->get_task_pool_stats().first) + " tasks");
        }
        
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    
    logger->info("Monitoring thread received shutdown signal");
}

int main(int argc, char* argv[]) {

    // --- Stage 1: Build logging pipeline ---
    auto logger = std::make_shared<Logger>("AsyncTransportServer");
    auto stdout_sink = std::make_shared<StdoutSink>();
    stdout_sink->set_level(LogLevel::Info); // Set to Info for general output
    logger->add_sink(stdout_sink);

    try {

        // --- Stage 2: Parse CLI/JSON options ---

        // Note, all options auto-register via static objects; no manual call needed
        // Parse the command line and JSON config once for the entire process
        std::string opts_err;
        auto parse_res = shared_opts::Options::load_and_parse(argc, argv, opts_err);
        if (parse_res == shared_opts::Options::ParseResult::Help || parse_res == shared_opts::Options::ParseResult::Version) {
            return 0; // help/version already printed
        } else if (parse_res == shared_opts::Options::ParseResult::Error) {
            logger->error(std::string("Failed to parse options: ") + opts_err);
            return 2;
        }

        // --- Stage 3: Bring up transport server subsystem ---
        logger->info("Async Transport Server starting...");

        AsyncTransportServer server(logger);
        if (!server.start()) {
            logger->error("Failed to start Async Transport Server");
            return 3;
        }
        logger->info("Async Transport Server started successfully");

        // --- Stage 4: Production mode with auto-refill monitoring ---
        
        // Install signal handlers for graceful shutdown
        std::signal(SIGTERM, signal_handler);
        std::signal(SIGINT, signal_handler);
        
        DefaultTaskGenerator generator;
        
        // Generate initial batch of tasks
        logger->info("Generating initial batch of 100 tasks");
        auto initial_tasks = generator.make_tasks(100);
        server.enqueue_tasks(std::move(initial_tasks));
        logger->info("Initial task batch enqueued");
        
        // Launch monitoring thread
        std::thread monitor_thread(monitoring_thread_func, &server, &generator, logger);
        
        // Wait for shutdown signal
        monitor_thread.join();
        
        // Clean shutdown
        logger->info("Shutting down server...");
        server.stop();

    } catch (const std::exception& e) {
        logger->error("Exception in Async Transport Server main loop: " + std::string(e.what()));
        return 1;
    }

    // --- Stage 5: Final shutdown log ---
    logger->info("Async Transport Server shut down successfully");

    return 0;
}
