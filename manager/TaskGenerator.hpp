/**
 * \file TaskGenerator.hpp
 * \ingroup task_messenger_manager
 * \brief Interfaces and mock implementation for feeding tasks into Task Messenger.
 */
#pragma once

#include "message/TaskMessagePool.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

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

private:
    std::string generate_task_data(uint32_t task_id, uint32_t skill_id);

    TaskIdGenerator task_id_generator_;
    std::atomic<bool> stopped_{false};
};
