// interactiveMain.cpp - Interactive generator: prompts user for task count,
// dispatches via submit_task(), waits for completion, prints stats.
#include "manager/ManagerApp.hpp"
#include "generators/common/TaskGenerator.hpp"
#include "generators/common/GeneratorOptions.hpp"
#include "skills/registry/CompareUtils.hpp"
#include "skills/registry/SkillRegistry.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

int main(int argc, char* argv[]) {
    ManagerApp app;
    int rc = app.start(argc, argv);
    if (rc != 0) {
        // 1 = help/version (exit 0), 2+ = error
        return (rc == 1) ? 0 : rc;
    }

    auto logger = app.logger();

    // Log registered skills (verifies static initialization worked)
    auto& skill_registry = TaskMessenger::Skills::SkillRegistry::instance();
    logger->info("Registered skills: " + std::to_string(skill_registry.skill_count()));

    // Configure skill verification from generator options
    auto& compare_cfg = TaskMessenger::Skills::CompareConfig::defaults();
    compare_cfg.enabled = generator_opts::get_verify_enabled();
    compare_cfg.abs_epsilon = generator_opts::get_verify_epsilon();
    compare_cfg.rel_epsilon = generator_opts::get_verify_rel_epsilon();
    compare_cfg.inject_failure = generator_opts::get_verify_inject_failure();

    TaskGenerator generator;
    generator.set_app(&app);

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
        auto pending_coroutines = generator.dispatch_parallel(
            static_cast<uint32_t>(task_count));
        logger->info("All tasks submitted. Waiting for responses...");

        // Wait until all coroutines complete (responses received and processed)
        while (!TaskGenerator::all_done(pending_coroutines) &&
               !app.shutdown_requested()) {
            std::this_thread::sleep_for(POLL_INTERVAL);
        }

        if (TaskGenerator::all_done(pending_coroutines)) {
            logger->info("All " + std::to_string(task_count) + " tasks completed!");
            logger->info("=== Manager Statistics ===");
            app.print_statistics();
        }
    }

    app.stop();
    return 0;
}
