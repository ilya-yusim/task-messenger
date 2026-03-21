// TaskCompletionSource.hpp - Shared state for coroutine-based task completion
#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <memory>

// Forward declaration to avoid circular dependency
class ResponseContext;

/**
 * \brief Shared state for coroutine-based task completion.
 * \ingroup message_module
 * 
 * Bridges the awaiting generator coroutine and the completing Session.
 * The generator stores its coroutine handle here, and the Session fills
 * response data and posts to ResponseContext for resumption.
 * 
 * Follows the .NET TaskCompletionSource pattern: this is the "producer" side
 * that triggers completion from external code.
 */
struct TaskCompletionSource {
    /// Coroutine handle to resume when response arrives
    std::coroutine_handle<> awaiting_handle{nullptr};
    
    /// ResponseContext where completion will be posted
    std::shared_ptr<ResponseContext> response_context;
    
    // Response data (filled by Session when response arrives)
    uint32_t response_task_id{0};
    uint32_t response_skill_id{0};
    std::vector<std::byte> response_body;
    bool completed{false};
    bool success{false};
    
    /**
     * \brief Complete the task and post resumption to ResponseContext.
     * \param task_id Response task ID
     * \param skill_id Response skill ID  
     * \param body Response body data (copied)
     * \param is_success Whether the task completed successfully
     * 
     * Called by Session on its thread. The actual coroutine resumption
     * happens on a ResponseContext worker thread.
     */
    void complete(uint32_t task_id, uint32_t skill_id, 
                  std::span<const std::byte> body, bool is_success);
    
    // Convenience accessors for resumed coroutine
    [[nodiscard]] bool is_valid() const noexcept { return response_task_id != 0; }
    [[nodiscard]] bool is_completed() const noexcept { return completed; }
    [[nodiscard]] bool is_success() const noexcept { return success; }
    
    [[nodiscard]] std::span<const std::byte> body_span() const noexcept {
        return {response_body.data(), response_body.size()};
    }
    
    [[nodiscard]] std::span<const uint8_t> body_span_u8() const noexcept {
        return {reinterpret_cast<const uint8_t*>(response_body.data()), response_body.size()};
    }
};
