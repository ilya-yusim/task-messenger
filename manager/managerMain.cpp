// managerMain.cpp - Main AsyncTransportServer application for asynchronous transfer of tasks to workers.
#include "transport/AsyncTransportServer.hpp"
#include "TaskGenerator.hpp"
#include "options/Options.hpp"
#include "ManagerOptions.hpp"
#include "skills/registry/CompareUtils.hpp"
#include "skills/registry/SkillRegistry.hpp"


#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <iostream>
#include <limits>

// Global shutdown flag for signal handlers
static std::atomic<bool> shutdown_requested{false};

// Signal handler for graceful shutdown
static void signal_handler(int signum) {
    (void)signum; // Suppress unused parameter warning
    shutdown_requested.store(true, std::memory_order_relaxed);
}

// Monitoring thread function: auto-refill when pool size < 10
static void monitoring_thread_func(AsyncTransportServer* server, 
                                   DefaultTaskGenerator* generator,
                                   std::shared_ptr<Logger> logger) {
    constexpr size_t LOW_THRESHOLD = 10;
    constexpr size_t REFILL_AMOUNT = 100;
    constexpr auto POLL_INTERVAL = std::chrono::seconds(1);
    
    // Keep coroutines alive across iterations
    std::vector<GeneratorCoroutine> pending_coroutines;
    
    while (!shutdown_requested.load(std::memory_order_relaxed)) {
        // Prune completed coroutines
        std::erase_if(pending_coroutines, [](const GeneratorCoroutine& coro) {
            return coro.done();
        });
        
        auto [pool_size, waiting_sessions] = server->get_task_pool_stats();
        
        if (pool_size < LOW_THRESHOLD) {
            logger->info("Task pool low (" + std::to_string(pool_size) + 
                        " tasks), dispatching " + std::to_string(REFILL_AMOUNT) + " with async coroutines");
            
            // Dispatch tasks - don't wait for completion
            auto new_coroutines = generator->dispatch_parallel(
                server->task_pool(), 
                static_cast<uint32_t>(REFILL_AMOUNT));
            
            // Move new coroutines into persistent storage
            pending_coroutines.insert(pending_coroutines.end(),
                std::make_move_iterator(new_coroutines.begin()),
                std::make_move_iterator(new_coroutines.end()));
                
            logger->info("Dispatched " + std::to_string(REFILL_AMOUNT) + 
                        " tasks, " + std::to_string(pending_coroutines.size()) + " coroutines pending");
        }
        
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    
    logger->info("Monitoring thread received shutdown signal, " + 
                std::to_string(pending_coroutines.size()) + " coroutines still pending");
}

// Interactive mode function: prompt user for task count, wait for completion, print stats
static void interactive_mode_func(AsyncTransportServer* server, 
                                  DefaultTaskGenerator* generator,
                                  std::shared_ptr<Logger> logger) {
    constexpr auto POLL_INTERVAL = std::chrono::milliseconds(100);
    
    logger->info("=== Interactive Mode (Async Coroutine Dispatch) ===");
    logger->info("Enter number of tasks to add to queue (or 0 to quit)");
    
    while (!shutdown_requested.load(std::memory_order_relaxed)) {
        std::cout << "\nTasks> " << std::flush;
        
        int task_count = 0;
        std::cin >> task_count;
        
        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            logger->error("Invalid input. Please enter a number.");
            continue;
        }
        
        if (task_count <= 0) {
            logger->info("Exiting interactive mode...");
            shutdown_requested.store(true, std::memory_order_relaxed);
            break;
        }
        
        // Dispatch tasks in parallel using coroutines
        logger->info("Dispatching " + std::to_string(task_count) + " tasks with async coroutines...");
        auto pending_coroutines = generator->dispatch_parallel(
            server->task_pool(), 
            static_cast<uint32_t>(task_count));
        logger->info("All tasks submitted. Waiting for responses...");
        
        // Wait until all coroutines complete (responses received and processed)
        while (!DefaultTaskGenerator::all_done(pending_coroutines) && 
               !shutdown_requested.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(POLL_INTERVAL);
        }
        
        if (DefaultTaskGenerator::all_done(pending_coroutines)) {
            logger->info("All " + std::to_string(task_count) + " tasks completed!");
            logger->info("=== Manager Statistics ===");
            server->print_transporter_statistics();
        }
        
        // Coroutines are destroyed when pending_coroutines goes out of scope
    }
}

int main(int argc, char* argv[]) {

    // --- Stage 1: Build logging pipeline ---
    auto logger = std::make_shared<Logger>("AsyncTransportServer");
    auto stdout_sink = std::make_shared<StdoutSink>();
    stdout_sink->set_level(LogLevel::Info); // Set to Info for general output
    logger->add_sink(stdout_sink);

    // Log registered skills (verifies static initialization worked)
    auto& skill_registry = TaskMessenger::Skills::SkillRegistry::instance();
    logger->info("Registered skills: " + std::to_string(skill_registry.skill_count()));
     
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

        // Configure skill verification from manager options
        auto& compare_cfg = TaskMessenger::Skills::CompareConfig::defaults();
        compare_cfg.enabled = manager_opts::get_verify_enabled();
        compare_cfg.abs_epsilon = manager_opts::get_verify_epsilon();
        compare_cfg.rel_epsilon = manager_opts::get_verify_rel_epsilon();
        compare_cfg.inject_failure = manager_opts::get_verify_inject_failure();

        // --- Stage 3: Bring up transport server subsystem ---
        logger->info("Async Transport Server starting...");

        AsyncTransportServer server(logger);
        if (!server.start()) {
            logger->error("Failed to start Async Transport Server");
            return 3;
        }
        logger->info("Async Transport Server started successfully");

        // --- Stage 4: Production mode selection ---
        
        // Install signal handlers for graceful shutdown
        std::signal(SIGTERM, signal_handler);
        std::signal(SIGINT, signal_handler);
        
        DefaultTaskGenerator generator;
        // Set up response context for async coroutine dispatch
        generator.set_response_context(server.response_context());
        
        // Check if interactive mode is enabled
        bool interactive = manager_opts::get_interactive_mode();
        
        if (interactive) {
            // Interactive mode: prompt user for tasks
            logger->info("Starting in INTERACTIVE mode");
            interactive_mode_func(&server, &generator, logger);
        } else {
            // Auto-refill monitoring mode
            logger->info("Starting in AUTO-REFILL mode");
            
            // Launch monitoring thread
            std::thread monitor_thread(monitoring_thread_func, &server, &generator, logger);
            
            // Wait for shutdown signal
            monitor_thread.join();
        }
        
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
