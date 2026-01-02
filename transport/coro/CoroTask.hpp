/**
 * \file CoroTask.hpp
 * \brief Minimal C++20 coroutine task type used by the coro module.
 * \details Provides a lightweight Task<T> and Task<void> that own the coroutine
 * handle and define explicit suspend semantics: initial_suspend = suspend_never
 * to begin execution immediately and final_suspend = suspend_always to leave the
 * coroutine suspended at completion until the handle is destroyed.
 *
 * Exception policy: unhandled_exception() is a no-op, so exceptions escaping the
 * coroutine body are swallowed and not propagated via get_result(). This is
 * intentional for this project.
 */
// CoroTask.hpp - Basic coroutine task type  
#pragma once
#include <coroutine>
#include <utility>

/**
 * \defgroup coro_module Coroutine I/O Module
 * \brief Event loop, task types, and socket adapter for coroutine-based non-blocking I/O.
 * \details Provides the building blocks for coroutine-style networking: `Task<T>` wrappers,
 * `CoroIoContext` event loop, and `CoroSocketAdapter` awaitables over a pluggable `IAsyncStream`.
 */

/** \defgroup coro_task Task Types
 *  \ingroup coro_module
 *  \brief Minimal coroutine task wrappers and semantics.
 */

/** \addtogroup coro_task
 *  @{ */

/** \brief Simple coroutine task type for C++20 coroutines.
 *  \details Owns the coroutine handle; `initial_suspend = suspend_never` starts execution immediately.
 *  `final_suspend = suspend_always` keeps the frame alive until destruction. Exceptions are swallowed.
 *  \see transport::CoroIoContext \see transport::default_loop
 */
template<typename T = void>
struct Task {
    struct promise_type {
        /// Returns a Task that owns the coroutine handle
        Task<T> get_return_object() { 
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; 
        }
        /// Start executing immediately on creation
        std::suspend_never initial_suspend() { return {}; }
        /// Suspend at final suspend; lifetime controlled by Task owner
        std::suspend_always final_suspend() noexcept { return {}; }
        /// Project policy: swallow exceptions escaping the coroutine body
        void unhandled_exception() {}
        
        /// Store the result value for retrieval via Task::get_result()
        void return_value(T value) {
            result_ = std::move(value);
        }
        
        /// Access the stored result value
        T get_result() {
            return result_;
        }
        
    private:
        T result_{};
    };
    
    std::coroutine_handle<promise_type> h;
    
    /// Construct from an existing coroutine handle (Task takes ownership)
    explicit Task(std::coroutine_handle<promise_type> handle) : h(handle) {}
    /// Destroys the coroutine if still present (destroys frame)
    ~Task() { if (h) h.destroy(); }
    /// Move constructible; transfers handle ownership
    Task(Task&& other) : h(other.h) { other.h = nullptr; }
    /// Move assignable; destroys current handle then takes ownership
    Task& operator=(Task&& other) { 
        if (this != &other) {
            if (h) h.destroy();
            h = other.h;
            other.h = nullptr;
        }
        return *this;
    }
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    /// True if the coroutine has reached final suspend
    bool done() const { return h.done(); }
    /// Resume the coroutine if not done
    void resume() { if (h && !h.done()) h.resume(); }
    /// Access the underlying coroutine handle (do not destroy externally)
    std::coroutine_handle<promise_type> get_handle() const { return h; }
    
    /// Retrieve the result produced by the coroutine body
    T get_result() {
        return h.promise().get_result();
    }
};

/** \brief Specialization for `Task<void>` implementing the same lifetime semantics. */
template<>
struct Task<void> {
    struct promise_type {
        /// Returns a Task<void> that owns the coroutine handle
        Task<void> get_return_object() { 
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; 
        }
        /// Start executing immediately on creation
        std::suspend_never initial_suspend() { return {}; }
        /// Suspend at final suspend; lifetime controlled by Task owner
        std::suspend_always final_suspend() noexcept { return {}; }
        /// Project policy: swallow exceptions escaping the coroutine body
        void unhandled_exception() {}
        
        /// No result to return for void
        void return_void() {}
    };
    
    std::coroutine_handle<promise_type> h;
    
    /// Construct from an existing coroutine handle (Task takes ownership)
    explicit Task(std::coroutine_handle<promise_type> handle) : h(handle) {}
    /// Destroys the coroutine if still present (destroys frame)
    ~Task() { if (h) h.destroy(); }
    /// Move constructible; transfers handle ownership
    Task(Task&& other) : h(other.h) { other.h = nullptr; }
    /// Move assignable; destroys current handle then takes ownership
    Task& operator=(Task&& other) { 
        if (this != &other) {
            if (h) h.destroy();
            h = other.h;
            other.h = nullptr;
        }
        return *this;
    }
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    /// True if the coroutine has reached final suspend
    bool done() const { return h.done(); }
    /// Resume the coroutine if not done
    void resume() { if (h && !h.done()) h.resume(); }
    /// Access the underlying coroutine handle (do not destroy externally)
    std::coroutine_handle<promise_type> get_handle() const { return h; }
};

/** @} */
