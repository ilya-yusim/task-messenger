/**
 * \file TaskGenerator.hpp
 * \brief Shared task generation logic for generator executables.
 *
 * Provides dispatch_parallel(), process_single_task(), and task data generation.
 * Generators create a TaskGenerator, set the ManagerApp, and call
 * dispatch_parallel() to submit batches of tasks via ManagerApp::submit_task().
 */
#pragma once

#include "TaskIdGenerator.hpp"

#include "message/GeneratorCoroutine.hpp"
#include "skills/registry/PayloadBuffer.hpp"

#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

class ManagerApp;

class TaskGenerator {
public:
    TaskGenerator() = default;
    ~TaskGenerator() = default;

    void stop();
    bool is_stopped() const { return stopped_.load(); }

    void set_vector_size(size_t size) { vector_size_ = size; }
    size_t vector_size() const { return vector_size_; }

    void set_app(ManagerApp* app) { app_ = app; }

    /**
     * \brief Dispatch N tasks in parallel, each with its own coroutine.
     * \param count Number of tasks to dispatch
     * \return Vector of coroutine handles (must be kept alive until all complete)
     */
    std::vector<GeneratorCoroutine> dispatch_parallel(uint32_t count);

    /**
     * \brief Check if all coroutines in a batch have completed.
     */
    static bool all_done(const std::vector<GeneratorCoroutine>& coroutines);

private:
    GeneratorCoroutine process_single_task(
        uint32_t task_id,
        uint32_t skill_id);

    std::pair<std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase>,
              std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase>>
    generate_task_data_typed(uint32_t skill_id);

    TaskIdGenerator task_id_generator_;
    std::atomic<bool> stopped_{false};
    size_t vector_size_ = 1024;
    ManagerApp* app_ = nullptr;
};
