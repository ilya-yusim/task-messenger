#include "AutoRefillGenerator.hpp"
#include "dispatcher/DispatcherApp.hpp"
#include "message/GeneratorCoroutine.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

bool AutoRefillGenerator::initialize(DispatcherApp& app) {
    task_gen_.set_app(&app);
    iterator_ = std::make_unique<SkillTestIterator>();
    return true;
}

int AutoRefillGenerator::run(DispatcherApp& app) {
    auto logger = app.logger();

    constexpr size_t LOW_THRESHOLD = 10;
    constexpr size_t REFILL_AMOUNT = 100;
    constexpr auto POLL_INTERVAL = std::chrono::seconds(1);

    logger->info("Starting in AUTO-REFILL mode");
    logger->info("Test combinations: " + std::to_string(iterator_->total_combinations()));

    // Keep coroutines alive across iterations
    std::vector<GeneratorCoroutine> pending_coroutines;

    while (!app.shutdown_requested()) {
        // Prune completed coroutines
        std::erase_if(pending_coroutines, [](const GeneratorCoroutine& coro) {
            return coro.done();
        });

        auto queue_size = app.task_queue_size();

        if (queue_size < LOW_THRESHOLD) {
            logger->info("Task queue low (" + std::to_string(queue_size) +
                        " tasks), dispatching " + std::to_string(REFILL_AMOUNT) + " with async coroutines");

            auto tasks = iterator_->next(static_cast<uint32_t>(REFILL_AMOUNT));
            auto new_coroutines = task_gen_.dispatch_tasks(
                std::move(tasks), &verifier_);

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
