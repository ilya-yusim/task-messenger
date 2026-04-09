// CancellationToken.hpp - Lightweight cooperative cancellation signal
#pragma once

#include <atomic>
#include <memory>

/**
 * \file message/CancellationToken.hpp
 * \brief Atomic cancellation flag shared between a session and its queue waiter.
 * \ingroup message_module
 *
 * The token is created by Session and passed into TaskQueueAwaitable.
 * When the session is terminated, it calls cancel().  The task queue
 * checks is_cancelled() before resuming any waiter, preventing
 * use-after-free on destroyed coroutine frames.
 */
class CancellationToken {
public:
    CancellationToken() = default;

    /// Signal cancellation.  Thread-safe, idempotent.
    void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }

    /// Check if cancellation has been signalled.
    bool is_cancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> cancelled_{false};
};
