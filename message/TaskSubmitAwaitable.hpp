// TaskSubmitAwaitable.hpp - Awaitable for submitting a task and waiting for response
#pragma once

#include "TaskMessage.hpp"
#include "TaskMessageQueue.hpp"
#include "TaskCompletionSource.hpp"
#include "ResponseContext.hpp"

#include <coroutine>
#include <memory>

/**
 * \brief Awaitable for submitting a task and waiting for its response.
 * \ingroup message_module
 * 
 * Generator coroutine suspends until response arrives.
 * Resumption happens on a ResponseContext worker thread.
 * 
 * Usage:
 *   auto [request, response] = generate_task_data_typed(skill_id);
 *   auto& result = co_await submit_task(queue, response_ctx, task_id, 
 *                                        std::move(request), std::move(response));
 *   if (result.is_success()) { ... }
 */
class TaskSubmitAwaitable {
public:
    /**
     * \brief Construct an awaitable for task submission with optional pre-allocated response buffer.
     * \param queue Task message queue. Holds the tasks to be processed.
     * \param response_ctx ResponseContext where completion will be posted
     * \param task_id Unique task identifier
     * \param request Request payload buffer (ownership transferred)
     * \param response_buffer Pre-allocated response buffer (ownership transferred), or nullptr
     */
    TaskSubmitAwaitable(std::shared_ptr<TaskMessageQueue> queue,
                        std::shared_ptr<ResponseContext> response_ctx,
                        uint32_t task_id,
                        std::unique_ptr<PayloadBufferBase> request,
                        std::unique_ptr<PayloadBufferBase> response_buffer = nullptr)
        : queue_(std::move(queue))
        , response_ctx_(std::move(response_ctx))
        , task_id_(task_id)
        , request_(std::move(request))
        , response_buffer_(std::move(response_buffer))
        , completion_source_(std::make_shared<TaskCompletionSource>()) {}
    
    /**
     * \brief Never ready immediately - always suspend to enqueue task.
     */
    bool await_ready() const noexcept { return false; }
    
    /**
     * \brief Suspend and enqueue the task with completion source.
     * \param handle Coroutine handle to resume when response arrives
     */
    void await_suspend(std::coroutine_handle<> handle) {
        // Store handle and context in completion source
        completion_source_->awaiting_handle = handle;
        completion_source_->response_context = response_ctx_;
        
        // Create TaskMessage with completion source attached
        // Use 3-arg constructor when response buffer is provided
        TaskMessage task = response_buffer_ 
            ? TaskMessage(task_id_, std::move(request_), std::move(response_buffer_))
            : TaskMessage(task_id_, std::move(request_));
        task.set_completion_source(completion_source_);
        
        // Enqueue the task - Session will pick it up
        queue_->add_task(std::move(task));
    }
    
    /**
     * \brief Return reference to completion source with response data.
     * \return Reference to TaskCompletionSource containing response
     */
    TaskCompletionSource& await_resume() {
        return *completion_source_;
    }

private:
    std::shared_ptr<TaskMessageQueue> queue_;
    std::shared_ptr<ResponseContext> response_ctx_;
    uint32_t task_id_;
    std::unique_ptr<PayloadBufferBase> request_;
    std::unique_ptr<PayloadBufferBase> response_buffer_;
    std::shared_ptr<TaskCompletionSource> completion_source_;
};

/**
 * \brief Convenience function to create a submit awaitable with optional pre-allocated response buffer.
 * \param queue Task message queue. Holds the tasks to be processed.
 * \param response_ctx ResponseContext where completion will be posted
 * \param task_id Unique task identifier
 * \param request Request payload buffer (ownership transferred)
 * \param response_buffer Pre-allocated response buffer (ownership transferred), or nullptr
 * \return TaskSubmitAwaitable that can be co_awaited
 * 
 * Usage:
 *   auto [request, response] = generate_task_data_typed(skill_id);
 *   auto& result = co_await submit_task(queue, response_ctx, task_id, 
 *                                        std::move(request), std::move(response));
 */
inline TaskSubmitAwaitable submit_task(
    std::shared_ptr<TaskMessageQueue> queue,
    std::shared_ptr<ResponseContext> response_ctx,
    uint32_t task_id,
    std::unique_ptr<PayloadBufferBase> request,
    std::unique_ptr<PayloadBufferBase> response_buffer = nullptr) 
{
    return TaskSubmitAwaitable(std::move(queue), std::move(response_ctx), 
                                task_id, std::move(request), std::move(response_buffer));
}
