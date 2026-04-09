// TaskMessageQueue.hpp - Thread-safe task queue with awaitable interface
#pragma once

#include "TaskMessage.hpp"
#include "CancellationToken.hpp"
#include "transport/coro/CoroTask.hpp"

#include <queue>
#include <deque>
#include <mutex>
#include <coroutine>
#include <optional>
#include <atomic>
#include <memory>
#include <vector>

/**
 * \file message/TaskMessageQueue.hpp
 * \brief Awaitable task queue shared by dispatcher sessions.
 * \ingroup message_module
 */

// Forward declaration
class TaskMessageQueue;

/**
 * \brief Awaitable for retrieving the next task from the queue.
 * \ingroup message_module
 *
 * This awaitable suspends the calling coroutine if no tasks are available
 * and resumes it when a task becomes available or the queue shuts down.
 */
class TaskQueueAwaitable {
private:
    TaskMessageQueue* queue_;
    std::optional<TaskMessage> result_;
    std::shared_ptr<CancellationToken> token_;

public:
    TaskQueueAwaitable(TaskMessageQueue* queue, std::shared_ptr<CancellationToken> token)
        : queue_(queue), token_(std::move(token)) {}

    // Awaitable interface
    bool await_ready();
    void await_suspend(std::coroutine_handle<> handle);
    TaskMessage await_resume() {
        if (result_.has_value())
            return std::move(result_.value());
        else
            return TaskMessage{};
    }

    // Allow TaskMessageQueue direct access to result_
    friend class TaskMessageQueue;
};

/**
 * \brief Thread-safe task queue consumed by dispatcher sessions.
 * \ingroup message_module
 *
 * Provides an awaitable interface for getting tasks. Sessions can call
 * co_await get_next_task() which will suspend if no tasks are available
 * and resume when tasks are added to the queue.
 */
class TaskMessageQueue {
private:
    mutable std::mutex mutex_;
    std::deque<TaskMessage> tasks_;
    struct Waiter {
        std::coroutine_handle<> handle;
        TaskQueueAwaitable* awaiter; // pointer to suspended awaiter to fill result before resuming
        std::shared_ptr<CancellationToken> token;
    };
    std::queue<Waiter> waiting_workers_;
    std::atomic<bool> shutdown_{false};

    friend class TaskQueueAwaitable;

public:
    TaskMessageQueue() = default;
    ~TaskMessageQueue() = default;

    // Non-copyable, non-movable
    TaskMessageQueue(const TaskMessageQueue&) = delete;
    TaskMessageQueue& operator=(const TaskMessageQueue&) = delete;
    TaskMessageQueue(TaskMessageQueue&&) = delete;
    TaskMessageQueue& operator=(TaskMessageQueue&&) = delete;

    /**
     * @brief Get next task (awaitable - suspends if no tasks available)
     * @param token Cancellation token for cooperative teardown.
     * @return TaskQueueAwaitable that can be co_awaited
     *
     * Usage: auto task = co_await task_queue->get_next_task(cancel_token_);
     */
    TaskQueueAwaitable get_next_task(std::shared_ptr<CancellationToken> token) {
        return TaskQueueAwaitable(this, std::move(token));
    }

    /**
        * @brief Add task to queue (wakes up waiting sessions)
        * @param task Task to add to the queue
     */
    void add_task(TaskMessage task);

    /**
        * @brief Add multiple tasks to queue (wakes up waiting sessions efficiently)
        * @param tasks Vector of tasks to add to the queue
     */
    void add_tasks(std::vector<TaskMessage> tasks);

    /**
        * @brief Get current task count
        * @return Number of tasks currently in the queue
     */
    size_t size() const;

    /**
        * @brief Check if queue is empty
     * @return true if no tasks are available
     */
    bool empty() const;

    /**
        * @brief Shutdown queue (wake up all waiting sessions)
     *
     * After shutdown, no new tasks will be accepted and all waiting
     * sessions will be woken up with invalid tasks.
     */
    void shutdown();

    /**
        * @brief Check if queue is shutting down
     * @return true if shutdown() has been called
     */
    bool is_shutdown() const {
        return shutdown_.load();
    }

    /**
     * @brief Get number of sessions waiting for tasks
     * @return Number of suspended coroutines waiting for tasks
     */
    size_t waiting_workers_count() const;

    /**
     * @brief Remove cancelled waiters from the queue without resuming them.
     *
     * Housekeeping: prevents unbounded growth of stale entries when no new
     * tasks arrive to trigger the lazy-skip path in add_task().
     */
    void drain_cancelled();

    /**
     * @brief Find the waiter associated with \p token, remove it from the
     *        queue, fill it with an invalid TaskMessage, and resume it.
     *
     * The resumed coroutine runs synchronously on the caller's thread and
     * is expected to observe the invalid task and exit its processing loop.
     * If the waiter has already been dequeued (by add_task or shutdown),
     * this is a safe no-op.
     */
    void cancel_and_resume_waiter(const std::shared_ptr<CancellationToken>& token);

private:
    /**
     * @brief Try to get a task immediately without suspending
     * @param task Output parameter for the task
        * @return true if task was available, false if queue is empty
     */
    bool try_get_task_immediately(TaskMessage& task);

    /**
     * @brief Resume one waiting session (called when task is added)
     */
    void resume_waiting_session();
};
