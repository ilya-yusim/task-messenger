// autoRefillMain.cpp - Auto-refill generator: monitors task pool and
// automatically dispatches new tasks when the pool falls below a threshold.
#include "manager/ManagerApp.hpp"
#include "generators/common/TaskGenerator.hpp"
#include "generators/common/GeneratorOptions.hpp"
#include "skills/registry/CompareUtils.hpp"
#include "skills/registry/SkillRegistry.hpp"

#include <chrono>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    ManagerApp app;
    int rc = app.start(argc, argv);
    if (rc != 0) {
        return (rc == 1) ? 0 : rc;
    }

    auto logger = app.logger();

    // Log registered skills
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

    constexpr size_t LOW_THRESHOLD = 10;
    constexpr size_t REFILL_AMOUNT = 100;
    constexpr auto POLL_INTERVAL = std::chrono::seconds(1);

    logger->info("Starting in AUTO-REFILL mode");

    // Keep coroutines alive across iterations
    std::vector<GeneratorCoroutine> pending_coroutines;

    while (!app.shutdown_requested()) {
        // Prune completed coroutines
        std::erase_if(pending_coroutines, [](const GeneratorCoroutine& coro) {
            return coro.done();
        });

        auto pool_size = app.task_pool_size();

        if (pool_size < LOW_THRESHOLD) {
            logger->info("Task pool low (" + std::to_string(pool_size) +
                        " tasks), dispatching " + std::to_string(REFILL_AMOUNT) + " with async coroutines");

            auto new_coroutines = generator.dispatch_parallel(
                static_cast<uint32_t>(REFILL_AMOUNT));

            pending_coroutines.insert(pending_coroutines.end(),
                std::make_move_iterator(new_coroutines.begin()),
                std::make_move_iterator(new_coroutines.end()));

            logger->info("Dispatched " + std::to_string(REFILL_AMOUNT) +
                        " tasks, " + std::to_string(pending_coroutines.size()) + " coroutines pending");
        }

        std::this_thread::sleep_for(POLL_INTERVAL);
    }

    logger->info("Shutdown signal received, " +
                std::to_string(pending_coroutines.size()) + " coroutines still pending");

    app.stop();
    return 0;
}
