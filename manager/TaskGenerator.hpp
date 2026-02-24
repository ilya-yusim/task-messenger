/**
 * \file TaskGenerator.hpp
 * \ingroup task_messenger_manager
 * \brief Interfaces and mock implementation for feeding tasks into Task Messenger.
 */
#pragma once

#include "message/TaskMessagePool.hpp"
#include "skills/builtins/VectorMathPayload.hpp"
#include "skills/builtins/FusedMultiplyAddPayload.hpp"
#include "skills/builtins/MathOperationPayload.hpp"
#include "skills/builtins/StringReversalPayload.hpp"
#include "skills/registry/PayloadBuffer.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

using namespace TaskMessenger::Skills;

// TaskIdGenerator - simple atomic counter for unique task IDs
/**
 * \ingroup task_messenger_manager
 * \brief Atomic counter helper for issuing unique demo task IDs.
 */
class TaskIdGenerator {
public:
    TaskIdGenerator() = default;
    ~TaskIdGenerator() = default;

    TaskIdGenerator(const TaskIdGenerator&) = delete;
    TaskIdGenerator& operator=(const TaskIdGenerator&) = delete;

    TaskIdGenerator(TaskIdGenerator&&) = delete;
    TaskIdGenerator& operator=(TaskIdGenerator&&) = delete;

    uint32_t get_next_id() {
        uint32_t next = counter_.fetch_add(1, std::memory_order_relaxed);
        if (next == 0) {
            next = counter_.fetch_add(1, std::memory_order_relaxed);
        }
        return next;
    }

private:
    std::atomic<uint32_t> counter_{1};
};

/**
 * \ingroup task_messenger_manager
 * \brief Contract for components that supply tasks to the manager.
 */
class ITaskGenerator {
public:
    virtual ~ITaskGenerator() = default;

    /**
     * \brief Populate the shared pool with freshly created tasks.
     */
    virtual void generate_tasks(std::shared_ptr<TaskMessagePool> pool, uint32_t count) = 0;

    /**
     * \brief Produce demo tasks without enqueuing them.
     */
    virtual std::vector<TaskMessage> make_tasks(uint32_t count) = 0;

    /**
     * \brief Signal shutdown so generators stop producing work.
     */
    virtual void stop() = 0;
};

/**
 * \ingroup task_messenger_manager
 * \brief Mock generator that demonstrates how applications push tasks into Task Messenger.
 * 
 * Supports two modes:
 * - One-off: Uses create_payload() for each task (default)
 * - Typed buffer: Uses create_payload_buffer() for typed data access 
 *   (call init_payload_templates() first)
 */
class DefaultTaskGenerator : public ITaskGenerator {
public:
    DefaultTaskGenerator() = default;
    ~DefaultTaskGenerator() override = default;

    /** \brief Enqueue demo tasks into the shared pool. */
    void generate_tasks(std::shared_ptr<TaskMessagePool> pool, uint32_t count) override;
    /** \brief Build a vector of demo tasks for inspection or manual enqueueing. */
    std::vector<TaskMessage> make_tasks(uint32_t count) override;
    /** \brief Halt further task production. */
    void stop() override;

    bool is_stopped() const { return stopped_.load(); }

    /**
     * \brief Enable typed buffer mode for the specified vector size.
     * 
     * After calling this, tasks will be generated using create_payload_buffer()
     * for typed data access and zero-copy buffer ownership transfer.
     * 
     * @param vector_size Size for vector operands in VectorMath and FusedMultiplyAdd.
     */
    void init_payload_templates(size_t vector_size);

    /**
     * \brief Check if typed buffer mode is enabled.
     */
    bool templates_initialized() const { return templates_initialized_; }

private:
    /**
     * \brief Generate task payload using simple one-off creation.
     */
    std::unique_ptr<PayloadBufferBase> generate_task_data_oneoff(uint32_t skill_id);

    /**
     * \brief Generate task payload using typed buffer creation.
     */
    std::unique_ptr<PayloadBufferBase> generate_task_data_typed(uint32_t skill_id);

    TaskIdGenerator task_id_generator_;
    std::atomic<bool> stopped_{false};

    // Template state
    bool templates_initialized_ = false;
    size_t template_vector_size_ = 0;
};
