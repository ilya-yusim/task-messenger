#ifndef THREAD_SAFE_QUEUE_HPP
#define THREAD_SAFE_QUEUE_HPP

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

/**
 * @brief A thread-safe FIFO queue.
 *
 * This class provides a thread-safe wrapper around a standard FIFO queue.
 * It uses a mutex to ensure that only one thread can access the queue at a time,
 * and a condition variable to notify waiting threads when an element is added to the queue.
 *
 * @tparam T The type of elements stored in the queue.
 */
template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() : shutdown_(false) {}

    /**
     * @brief Adds an element to the back of the queue.
     *
     * This method is thread-safe and will notify one waiting thread, if any.
     *
     * @param value The element to add to the queue.
     */
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
        condition_.notify_one();
    }

    /**
     * @brief Removes and returns the front element of the queue.
     *
     * This method is thread-safe. If the queue is empty, it returns std::nullopt.
     *
     * @return std::optional<T> The front element of the queue, or std::nullopt if the queue is empty.
     */
    std::optional<T> pop() {  
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() { return shutdown_ || !queue_.empty(); });
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
     * @brief Notifies all waiting threads.
     *
     * This method is thread-safe and will notify all waiting threads.
     */
    void notifyAll() {
        condition_.notify_all();
    }

    /**
     * @brief Shuts down the queue and notifies all waiting threads.
     *
     * This method is thread-safe and will notify all waiting threads to stop waiting.
     */
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        condition_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::condition_variable condition_;
    bool shutdown_;
};

#endif // THREAD_SAFE_QUEUE_HPP