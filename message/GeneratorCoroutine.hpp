// GeneratorCoroutine.hpp - Coroutine return type for async generators
#pragma once

#include <coroutine>

/**
 * \brief Coroutine return type for async task generators.
 * \ingroup message_module
 * 
 * Owns the coroutine handle with these semantics:
 * - `initial_suspend = suspend_never`: starts execution immediately
 * - `final_suspend = suspend_always`: keeps frame alive until destruction
 * - Exceptions are swallowed (project policy)
 * 
 * Used by TaskGenerator::run_async_chain() and similar generator coroutines
 * that submit tasks and await responses.
 */
struct GeneratorCoroutine {
    struct promise_type {
        GeneratorCoroutine get_return_object() { 
            return GeneratorCoroutine{std::coroutine_handle<promise_type>::from_promise(*this)}; 
        }
        /// Start executing immediately on creation
        std::suspend_never initial_suspend() { return {}; }
        /// Suspend at final suspend; lifetime controlled by GeneratorCoroutine owner
        std::suspend_always final_suspend() noexcept { return {}; }
        /// Project policy: swallow exceptions escaping the coroutine body
        void unhandled_exception() {}
        /// No result to return
        void return_void() {}
    };
    
    std::coroutine_handle<promise_type> handle;
    
    /// Construct from an existing coroutine handle (takes ownership)
    explicit GeneratorCoroutine(std::coroutine_handle<promise_type> h) : handle(h) {}
    
    /// Destroys the coroutine frame if still present
    ~GeneratorCoroutine() { if (handle) handle.destroy(); }
    
    /// Move constructible; transfers handle ownership
    GeneratorCoroutine(GeneratorCoroutine&& other) noexcept : handle(other.handle) { 
        other.handle = nullptr; 
    }
    
    /// Move assignable; destroys current handle then takes ownership
    GeneratorCoroutine& operator=(GeneratorCoroutine&& other) noexcept { 
        if (this != &other) {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    
    GeneratorCoroutine(const GeneratorCoroutine&) = delete;
    GeneratorCoroutine& operator=(const GeneratorCoroutine&) = delete;
    
    /// True if the coroutine has reached final suspend
    [[nodiscard]] bool done() const { return handle.done(); }
    
    /// Resume the coroutine if not done
    void resume() { if (handle && !handle.done()) handle.resume(); }
    
    /// Access the underlying coroutine handle (do not destroy externally)
    [[nodiscard]] std::coroutine_handle<promise_type> get_handle() const { return handle; }
};
