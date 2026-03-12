// ResponseContext.hpp - Multi-threaded executor for processing task responses
#pragma once

#include "TaskCompletionSource.hpp"
#include "processUtils.hpp"
#include "logger.hpp"

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <coroutine>
#include <string>

/**
 * \brief Multi-threaded executor for processing task responses.
 * \ingroup message_module
 * 
 * Session threads post completed responses here. Worker threads
 * block when the queue is empty and wake when work arrives.
 * Generator coroutines resume on one of the worker threads.
 */
class ResponseContext {
public:
    /**
     * \brief Construct with optional name prefix for worker threads.
     * \param name_prefix Prefix for thread names (default: "ResponseWorker")
     * \param logger Optional logger for info/warning/error messages
     */
    explicit ResponseContext(std::string name_prefix = "ResponseWorker",
                             std::shared_ptr<Logger> logger = nullptr)
        : name_prefix_(std::move(name_prefix))
        , logger_(std::move(logger)) {}
    
    ~ResponseContext() { stop(); }
    
    // Non-copyable
    ResponseContext(const ResponseContext&) = delete;
    ResponseContext& operator=(const ResponseContext&) = delete;
    
    /**
     * \brief Start with a single response processing thread.
     */
    void start() { start(1); }
    
    /**
     * \brief Start with multiple response processing threads.
     * \param thread_count Number of worker threads (minimum 1)
     */
    void start(size_t thread_count) {
        if (thread_count == 0) thread_count = 1;
        
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;  // Already running
        }
        
        thread_count_.store(thread_count);
        workers_.reserve(thread_count);
        
        for (size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this, i] { 
                try {
                    ProcessUtils::set_current_thread_name(name_prefix_ + "-" + std::to_string(i));
                } catch (...) {
                    // Best-effort thread naming
                }
                worker_loop(i); 
            });
        }
        
        if (logger_) {
            logger_->info("ResponseContext: Started with " + std::to_string(thread_count) + " worker thread(s)");
        }
    }
    
    /**
     * \brief Stop all worker threads and wait for them to finish.
     */
    void stop() {
        {
            std::lock_guard lock(mutex_);
            if (!running_) return;
            running_ = false;
        }
        
        cv_.notify_all();
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
        
        if (logger_) {
            logger_->info("ResponseContext: Stopped");
        }
    }
    
    /**
     * \brief Post a coroutine handle to be resumed on a worker thread.
     * \param handle The coroutine to resume
     * 
     * Thread-safe. Called by Session when response arrives.
     */
    void post(std::coroutine_handle<> handle) {
        {
            std::lock_guard lock(mutex_);
            if (!running_) return;
            pending_.push(handle);
        }
        cv_.notify_one();
    }
    
    /**
     * \brief Check if running.
     */
    bool is_running() const { return running_.load(); }
    
    /**
     * \brief Get pending response count.
     */
    size_t pending_count() const {
        std::lock_guard lock(mutex_);
        return pending_.size();
    }
    
    /**
     * \brief Get number of worker threads.
     */
    size_t thread_count() const { return thread_count_.load(); }
    
    /**
     * \brief Get number of threads currently blocked waiting for work.
     */
    size_t idle_thread_count() const { return idle_count_.load(); }
    
    /**
     * \brief Set logger after construction.
     */
    void set_logger(std::shared_ptr<Logger> logger) { logger_ = std::move(logger); }

private:
    void worker_loop([[maybe_unused]] size_t thread_index) {
        while (true) {
            std::coroutine_handle<> handle;
            
            {
                std::unique_lock lock(mutex_);
                
                ++idle_count_;
                
                cv_.wait(lock, [this] {
                    return !pending_.empty() || !running_;
                });
                
                --idle_count_;
                
                if (!running_ && pending_.empty()) {
                    break;
                }
                
                if (pending_.empty()) continue;
                
                handle = pending_.front();
                pending_.pop();
            }
            
            process_handle(handle);
        }
    }
    
    void process_handle(std::coroutine_handle<> handle) {
        try {
            if (!handle) {
                if (logger_) {
                    logger_->warning("ResponseContext: Attempted to resume null coroutine handle");
                }
                return;
            }
            
            if (handle.done()) {
                // Generator coroutine was destroyed before response arrived
                if (logger_) {
                    logger_->warning("ResponseContext: Attempted to resume completed/destroyed coroutine");
                }
                return;
            }
            
            handle.resume();
            
        } catch (const std::exception& e) {
            if (logger_) {
                logger_->error("ResponseContext: Exception in worker: " + std::string(e.what()));
            }
        } catch (...) {
            if (logger_) {
                logger_->error("ResponseContext: Unknown exception in worker");
            }
        }
    }
    
    std::string name_prefix_;
    std::shared_ptr<Logger> logger_;
    std::queue<std::coroutine_handle<>> pending_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> thread_count_{0};
    std::atomic<size_t> idle_count_{0};
    std::vector<std::thread> workers_;
};

// Now we can implement TaskCompletionSource::complete() which depends on ResponseContext
inline void TaskCompletionSource::complete(uint32_t task_id, uint32_t skill_id, 
                                           std::span<const std::byte> body, bool is_success) {
    // Fill response data (still on Session thread)
    this->response_task_id = task_id;
    this->response_skill_id = skill_id;
    this->response_body.assign(body.begin(), body.end());
    this->success = is_success;
    this->completed = true;
    
    // Post resumption to response thread
    if (this->response_context && this->awaiting_handle) {
        this->response_context->post(this->awaiting_handle);
    } else if (this->awaiting_handle && !this->awaiting_handle.done()) {
        // Fallback: inline resume (not recommended for production)
        this->awaiting_handle.resume();
    }
}
