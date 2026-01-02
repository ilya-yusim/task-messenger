#include "TaskMessagePool.hpp"
#include "transport/coro/CoroTask.hpp"

#include <iostream>
#include <utility>

/**
 * \file message/TaskMessagePool.cpp
 * \brief Implements the coroutine-aware task queue and awaitable bridge.
 * \ingroup message_module
 */

// Design overview:
// - await_ready tries to opportunistically grab a task without locking callers.
// - await_suspend records the awaiting coroutine and stores a pointer so the pool
//   can populate the result prior to resumption.
// - add_task/add_tasks prefer waking suspended sessions before growing the deque,
//   preserving FIFO order for both tasks and waiters.
// - shutdown() drains waiters and resumes them with empty TaskMessage instances.

bool TaskAwaitable::await_ready() {
    TaskMessage task;
    if (pool_->try_get_task_immediately(task)) {
        result_ = std::move(task);
        return true;
    }
    return false;
}

void TaskAwaitable::await_suspend(std::coroutine_handle<> handle) {
    // If pool is shut down, resume immediately with invalid task
    if (pool_->shutdown_.load()) {
        handle.resume();
        return;
    }

    std::unique_lock lock(pool_->mutex_);

    // Check again after acquiring lock if pool is shut down
    if (pool_->shutdown_.load()) {
        lock.unlock();
        handle.resume();
        return;
    }

    // Try to get a task
    if (!pool_->tasks_.empty()) {
        result_ = std::move(pool_->tasks_.front());
        pool_->tasks_.pop_front();
        lock.unlock();
        handle.resume();
        return;
    }

    // No task available, add to waiting sessions
    pool_->waiting_sessions_.push({handle, this});
}

void TaskMessagePool::add_task(TaskMessage task) {
    if (shutdown_.load()) {
        return;
    }

    std::unique_lock lock(mutex_);

    if (!waiting_sessions_.empty()) {
        auto waiter = waiting_sessions_.front();
        waiting_sessions_.pop();

        if (waiter.awaiter) {
            waiter.awaiter->result_ = std::move(task);
            lock.unlock();
            waiter.handle.resume();
        } else {
            tasks_.push_back(std::move(task));
            lock.unlock();
            waiter.handle.resume();
        }
    } else {
        tasks_.push_back(std::move(task));
    }
}

void TaskMessagePool::add_tasks(std::vector<TaskMessage> tasks) {
    if (shutdown_.load()) {
        return;
    }

    std::unique_lock lock(mutex_);

    for (auto& task : tasks) {
        if (!waiting_sessions_.empty()) {
            auto waiter = waiting_sessions_.front();
            waiting_sessions_.pop();

            if (waiter.awaiter) {
                waiter.awaiter->result_ = std::move(task);
                lock.unlock();
                waiter.handle.resume();
                lock.lock();
            } else {
                tasks_.push_back(std::move(task));
                lock.unlock();
                waiter.handle.resume();
                lock.lock();
            }
        } else {
            tasks_.push_back(std::move(task));
        }
    }
}

size_t TaskMessagePool::size() const {
    std::lock_guard lock(mutex_);
    return tasks_.size();
}

bool TaskMessagePool::empty() const {
    std::lock_guard lock(mutex_);
    return tasks_.empty();
}

void TaskMessagePool::shutdown() {
    bool expected = false;
    if (!shutdown_.compare_exchange_strong(expected, true)) {
        return;
    }

    std::queue<Waiter> waiters_to_resume;
    {
        std::lock_guard lock(mutex_);
        std::swap(waiters_to_resume, waiting_sessions_);
    }

    while (!waiters_to_resume.empty()) {
        auto waiter = waiters_to_resume.front();
        waiters_to_resume.pop();
        waiter.handle.resume();
    }
}

size_t TaskMessagePool::waiting_count() const {
    std::lock_guard lock(mutex_);
    return waiting_sessions_.size();
}

bool TaskMessagePool::try_get_task_immediately(TaskMessage& task) {
    if (shutdown_.load()) {
        return false;
    }

    std::lock_guard lock(mutex_);
    if (tasks_.empty()) {
        return false;
    }

    task = std::move(tasks_.front());
    tasks_.pop_front();
    return true;
}

void TaskMessagePool::resume_waiting_session() {
    if (!waiting_sessions_.empty()) {
        auto waiter = waiting_sessions_.front();
        waiting_sessions_.pop();
        waiter.handle.resume();
    }
}
