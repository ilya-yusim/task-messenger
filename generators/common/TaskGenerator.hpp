/**
 * \file TaskGenerator.hpp
 * \brief Common task dispatch logic for generator executables.
 *
 * Provides dispatch_tasks() and coroutine lifecycle management.
 * Generators create a TaskGenerator, set the DispatcherApp, and call
 * dispatch_tasks() with pre-built TestTaskData to submit batches of tasks.
 */
#pragma once

#include "SkillTestIterator.hpp"
#include "TaskIdGenerator.hpp"
#include "VerificationHelper.hpp"

#include "message/GeneratorCoroutine.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

class DispatcherApp;

class TaskGenerator {
public:
    TaskGenerator() = default;
    ~TaskGenerator() = default;

    void stop();
    bool is_stopped() const { return stopped_.load(); }

    void set_app(DispatcherApp* app);

    /**
     * \brief Dispatch pre-built tasks, each with its own coroutine.
     *
     * Assigns task IDs, optionally clones requests for verification,
     * and launches one coroutine per task.
     *
     * \param tasks  Pre-built request/response pairs (consumed via move).
     * \param verifier  Optional verification helper (nullptr to skip verification).
     * \return Vector of coroutine handles (must be kept alive until all complete).
     */
    std::vector<GeneratorCoroutine> dispatch_tasks(
        std::vector<TestTaskData> tasks,
        VerificationHelper* verifier = nullptr);

    /**
     * \brief Check if all coroutines in a batch have completed.
     */
    static bool all_done(const std::vector<GeneratorCoroutine>& coroutines);

private:
    GeneratorCoroutine process_single_task(
        uint32_t task_id,
        uint32_t skill_id,
        std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> request,
        std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> response,
        std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> request_copy);

    TaskIdGenerator task_id_generator_;
    std::atomic<bool> stopped_{false};
    DispatcherApp* app_ = nullptr;
};
