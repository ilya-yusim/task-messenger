#include "AutoRefillGenerator.hpp"
#include "dispatcher/DispatcherApp.hpp"
#include "message/GeneratorCoroutine.hpp"

#include <chrono>
#include <thread>
#include <vector>

bool AutoRefillGenerator::initialize(DispatcherApp& app) {
    task_gen_.set_app(&app);
    return true;
}

int AutoRefillGenerator::run(DispatcherApp& app) {
    auto logger = app.logger();

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

            auto new_coroutines = task_gen_.dispatch_parallel(
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

    return 0;
}

void AutoRefillGenerator::on_shutdown() {
    task_gen_.stop();
}
