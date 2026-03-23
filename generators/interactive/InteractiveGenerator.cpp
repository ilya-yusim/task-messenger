#include "InteractiveGenerator.hpp"
#include "dispatcher/DispatcherApp.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

bool InteractiveGenerator::initialize(DispatcherApp& app) {
    task_gen_.set_app(&app);
    return true;
}

int InteractiveGenerator::run(DispatcherApp& app) {
    auto logger = app.logger();
    constexpr auto POLL_INTERVAL = std::chrono::milliseconds(100);

    logger->info("=== Interactive Mode (Async Coroutine Dispatch) ===");
    logger->info("Enter number of tasks to add to queue (or 0 to quit)");

    while (!app.shutdown_requested()) {
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
            app.request_shutdown();
            break;
        }

        // Dispatch tasks in parallel using coroutines
        logger->info("Dispatching " + std::to_string(task_count) + " tasks with async coroutines...");
        auto pending_coroutines = task_gen_.dispatch_parallel(
            static_cast<uint32_t>(task_count));
        logger->info("All tasks submitted. Waiting for responses...");

        // Wait until all coroutines complete (responses received and processed)
        while (!TaskGenerator::all_done(pending_coroutines) &&
               !app.shutdown_requested()) {
            std::this_thread::sleep_for(POLL_INTERVAL);
        }

        if (TaskGenerator::all_done(pending_coroutines)) {
            logger->info("All " + std::to_string(task_count) + " tasks completed!");
            logger->info("=== Dispatcher Statistics ===");
            app.print_statistics();
        }
    }

    return 0;
}

void InteractiveGenerator::on_shutdown() {
    task_gen_.stop();
}
