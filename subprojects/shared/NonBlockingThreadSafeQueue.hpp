#ifndef NON_BLOCKING_THREAD_SAFE_QUEUE_HPP
#define NON_BLOCKING_THREAD_SAFE_QUEUE_HPP

#include <queue>
#include <mutex>
#include <optional>

/**
 * @brief A non-blocking, thread-safe FIFO queue.
 *
 * This class provides a thread-safe wrapper around a standard FIFO queue.
 * It uses a mutex to ensure that only one thread can access the queue at a time.
 * Unlike blocking queues, this implementation provides a non-blocking `tryPop` method.
 *
 * @tparam T The type of elements stored in the queue.
 */
template <typename T>
class NonBlockingThreadSafeQueue {
public:
    NonBlockingThreadSafeQueue() = default;

    /**
     * @brief Adds an element to the back of the queue.
     *
     * This method is thread-safe.
     *
     * @param value The element to add to the queue.
     */
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
    }

    /**
     * @brief Removes and returns the front element of the queue, if available.
     *
     * This method is thread-safe and non-blocking. If the queue is empty, it returns std::nullopt.
     *
     * @return std::optional<T> The front element of the queue, or std::nullopt if the queue is empty.
     */
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = queue_.front();
        queue_.pop();
        return value;
    }

    /**
     * @brief Checks if the queue is empty.
     *
     * This method is thread-safe.
     *
     * @return bool True if the queue is empty, false otherwise.
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief Returns the size of the queue.
     *
     * This method is thread-safe.
     *
     * @return std::size_t The number of elements in the queue.
     */
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
};

#endif // NON_BLOCKING_THREAD_SAFE_QUEUE_HPP