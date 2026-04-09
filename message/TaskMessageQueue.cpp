#include "TaskMessageQueue.hpp"
#include "transport/coro/CoroTask.hpp"

#include <iostream>
#include <utility>

/**
 * \file message/TaskMessageQueue.cpp
 * \brief Implements the coroutine-aware task queue and awaitable bridge.
 * \ingroup message_module
 */

// Design overview:
// - await_ready tries to opportunistically grab a task without locking callers.
//   It also short-circuits if the associated CancellationToken is already set.
// - await_suspend records the awaiting coroutine and stores a pointer so the queue
//   can populate the result prior to resumption.  The CancellationToken is stored
//   alongside the handle so that add_task() can skip dead waiters (Option A) and
//   cancel_and_resume_waiter() can explicitly tear them down (Option B).
// - add_task/add_tasks prefer waking suspended sessions before growing the deque,
//   preserving FIFO order for both tasks and waiters.  Cancelled waiters are
//   silently discarded.
// - shutdown() drains waiters and resumes them with empty TaskMessage instances,
//   skipping any whose token is already cancelled.

bool TaskQueueAwaitable::await_ready() {
    // If already cancelled, return immediately so the coroutine can exit.
    if (token_ && token_->is_cancelled()) {
        return true;
    }
    TaskMessage task;
    if (queue_->try_get_task_immediately(task)) {
        result_ = std::move(task);
        return true;
    }
    return false;
}

void TaskQueueAwaitable::await_suspend(std::coroutine_handle<> handle) {
    // If queue is shut down or token is cancelled, resume immediately with invalid task
    if (queue_->shutdown_.load() || (token_ && token_->is_cancelled())) {
        handle.resume();
        return;
    }

    std::unique_lock lock(queue_->mutex_);

    // Re-check after acquiring lock
    if (queue_->shutdown_.load() || (token_ && token_->is_cancelled())) {
        lock.unlock();
        handle.resume();
        return;
    }

    // Try to get a task
    if (!queue_->tasks_.empty()) {
        result_ = std::move(queue_->tasks_.front());
        queue_->tasks_.pop_front();
        lock.unlock();
        handle.resume();
        return;
    }

    // No task available, add to waiting sessions
    queue_->waiting_workers_.push({handle, this, token_});
}

void TaskMessageQueue::add_task(TaskMessage task) {
    if (shutdown_.load()) {
        return;
    }

    std::unique_lock lock(mutex_);

    // Skip cancelled waiters (Option A: lazy discard)
    while (!waiting_workers_.empty()) {
        auto& front = waiting_workers_.front();
        if (front.token && front.token->is_cancelled()) {
            waiting_workers_.pop();
            continue;
        }
        // Found a live waiter — give it the task
        auto waiter = waiting_workers_.front();
        waiting_workers_.pop();

        if (waiter.awaiter) {
            waiter.awaiter->result_ = std::move(task);
            lock.unlock();
            waiter.handle.resume();
        } else {
            tasks_.push_back(std::move(task));
            lock.unlock();
            waiter.handle.resume();
        }
        return;
    }

    tasks_.push_back(std::move(task));
}

void TaskMessageQueue::add_tasks(std::vector<TaskMessage> tasks) {
    if (shutdown_.load()) {
        return;
    }

    std::unique_lock lock(mutex_);

    for (auto& task : tasks) {
        // Skip cancelled waiters
        while (!waiting_workers_.empty()) {
            auto& front = waiting_workers_.front();
            if (front.token && front.token->is_cancelled()) {
                waiting_workers_.pop();
                continue;
            }
            break;
        }

        if (!waiting_workers_.empty()) {
            auto waiter = waiting_workers_.front();
            waiting_workers_.pop();

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

size_t TaskMessageQueue::size() const {
    std::lock_guard lock(mutex_);
    return tasks_.size();
}

bool TaskMessageQueue::empty() const {
    std::lock_guard lock(mutex_);
    return tasks_.empty();
}

void TaskMessageQueue::shutdown() {
    bool expected = false;
    if (!shutdown_.compare_exchange_strong(expected, true)) {
        return;
    }

    std::queue<Waiter> waiters_to_resume;
    {
        std::lock_guard lock(mutex_);
        std::swap(waiters_to_resume, waiting_workers_);
    }

    while (!waiters_to_resume.empty()) {
        auto waiter = waiters_to_resume.front();
        waiters_to_resume.pop();
        // Skip cancelled waiters — their coroutine frames may already be destroyed
        if (waiter.token && waiter.token->is_cancelled()) {
            continue;
        }
        waiter.handle.resume();
    }
}

size_t TaskMessageQueue::waiting_workers_count() const {
    std::lock_guard lock(mutex_);
    return waiting_workers_.size();
}

bool TaskMessageQueue::try_get_task_immediately(TaskMessage& task) {
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

void TaskMessageQueue::resume_waiting_session() {
    if (!waiting_workers_.empty()) {
        auto waiter = waiting_workers_.front();
        waiting_workers_.pop();
        waiter.handle.resume();
    }
}

void TaskMessageQueue::drain_cancelled() {
    std::lock_guard lock(mutex_);
    std::queue<Waiter> remaining;
    while (!waiting_workers_.empty()) {
        auto w = std::move(waiting_workers_.front());
        waiting_workers_.pop();
        if (w.token && w.token->is_cancelled()) {
            continue; // discard without resuming
        }
        remaining.push(std::move(w));
    }
    waiting_workers_ = std::move(remaining);
}

void TaskMessageQueue::cancel_and_resume_waiter(const std::shared_ptr<CancellationToken>& token) {
    std::coroutine_handle<> to_resume{};
    {
        std::lock_guard lock(mutex_);
        std::queue<Waiter> remaining;
        while (!waiting_workers_.empty()) {
            auto w = std::move(waiting_workers_.front());
            waiting_workers_.pop();
            if (w.token.get() == token.get() && !to_resume) {
                // Found our target — fill with invalid TaskMessage so coroutine exits
                if (w.awaiter) {
                    w.awaiter->result_ = TaskMessage{};
                }
                to_resume = w.handle;
            } else {
                remaining.push(std::move(w));
            }
        }
        waiting_workers_ = std::move(remaining);
    }
    // Resume OUTSIDE the lock — the coroutine may call back into the queue
    if (to_resume) {
        to_resume.resume();
    }
}
