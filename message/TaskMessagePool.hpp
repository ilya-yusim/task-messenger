// TaskMessagePool.hpp - Thread-safe task pool with awaitable interface
#pragma once

#include "TaskMessage.hpp"
#include "transport/coro/CoroTask.hpp"

#include <queue>
#include <deque>
#include <mutex>
#include <coroutine>
#include <optional>
#include <atomic>
#include <vector>

/**
 * \file message/TaskMessagePool.hpp
 * \brief Awaitable task queue shared by manager sessions.
 * \ingroup message_module
 */

// Forward declaration
class TaskMessagePool;

/**
 * \brief Awaitable for retrieving the next task from the pool.
 * \ingroup message_module
 *
 * This awaitable suspends the calling coroutine if no tasks are available
 * and resumes it when a task becomes available or the pool shuts down.
 */
class TaskAwaitable {
private:
    TaskMessagePool* pool_;
    std::optional<TaskMessage> result_;

public:
    explicit TaskAwaitable(TaskMessagePool* pool) : pool_(pool) {}

    // Awaitable interface
    bool await_ready();
    void await_suspend(std::coroutine_handle<> handle);
    TaskMessage await_resume() {
        if (result_.has_value())
            return std::move(result_.value());
        else
            return TaskMessage{};
    }

    // Allow TaskMessagePool direct access to result_
    friend class TaskMessagePool;
};

/**
 * \brief Thread-safe task pool consumed by manager sessions.
 * \ingroup message_module
 *
 * Provides an awaitable interface for getting tasks. Sessions can call
 * co_await get_next_task() which will suspend if no tasks are available
 * and resume when tasks are added to the pool.
 */
class TaskMessagePool {
private:
    mutable std::mutex mutex_;
    std::deque<TaskMessage> tasks_;
    struct Waiter {
        std::coroutine_handle<> handle;
        TaskAwaitable* awaiter; // pointer to suspended awaiter to fill result before resuming
    };
    std::queue<Waiter> waiting_sessions_;
    std::atomic<bool> shutdown_{false};

    friend class TaskAwaitable;

public:
    TaskMessagePool() = default;
    ~TaskMessagePool() = default;

    // Non-copyable, non-movable
    TaskMessagePool(const TaskMessagePool&) = delete;
    TaskMessagePool& operator=(const TaskMessagePool&) = delete;
    TaskMessagePool(TaskMessagePool&&) = delete;
    TaskMessagePool& operator=(TaskMessagePool&&) = delete;

    /**
     * @brief Get next task (awaitable - suspends if no tasks available)
     * @return TaskAwaitable that can be co_awaited
     *
     * Usage: auto task = co_await task_pool->get_next_task();
     */
    TaskAwaitable get_next_task() {
        return TaskAwaitable(this);
    }

    /**
     * @brief Add task to pool (wakes up waiting sessions)
     * @param task Task to add to the pool
     */
    void add_task(TaskMessage task);

    /**
     * @brief Add multiple tasks to pool (wakes up waiting sessions efficiently)
     * @param tasks Vector of tasks to add to the pool
     */
    void add_tasks(std::vector<TaskMessage> tasks);

    /**
     * @brief Get current task count
     * @return Number of tasks currently in the pool
     */
    size_t size() const;

    /**
     * @brief Check if pool is empty
     * @return true if no tasks are available
     */
    bool empty() const;

    /**
     * @brief Shutdown pool (wake up all waiting sessions)
     *
     * After shutdown, no new tasks will be accepted and all waiting
     * sessions will be woken up with invalid tasks.
     */
    void shutdown();

    /**
     * @brief Check if pool is shutting down
     * @return true if shutdown() has been called
     */
    bool is_shutdown() const {
        return shutdown_.load();
    }

    /**
     * @brief Get number of sessions waiting for tasks
     * @return Number of suspended coroutines waiting for tasks
     */
    size_t waiting_count() const;

private:
    /**
     * @brief Try to get a task immediately without suspending
     * @param task Output parameter for the task
     * @return true if task was available, false if pool is empty
     */
    bool try_get_task_immediately(TaskMessage& task);

    /**
     * @brief Resume one waiting session (called when task is added)
     */
    void resume_waiting_session();
};
