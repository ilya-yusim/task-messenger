/**
 * \file TaskGenerator.hpp
 * \ingroup task_messenger_manager
 * \brief Interfaces and mock implementation for feeding tasks into Task Messenger.
 */
#pragma once

#include "message/TaskMessagePool.hpp"
#include "message/ResponseContext.hpp"
#include "message/TaskSubmitAwaitable.hpp"
#include "message/GeneratorCoroutine.hpp"
#include "skills/builtins/VectorMathSkill.hpp"
#include "skills/builtins/FusedMultiplyAddSkill.hpp"
#include "skills/builtins/MathOperationSkill.hpp"
#include "skills/builtins/StringReversalSkill.hpp"
#include "skills/registry/PayloadBuffer.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace TaskMessenger::Skills;

// TaskIdGenerator - simple atomic counter for unique task IDs
/**
 * \ingroup task_messenger_manager
 * \brief Atomic counter helper for issuing unique demo task IDs.
 */
class TaskIdGenerator {
public:
    TaskIdGenerator() = default;
    ~TaskIdGenerator() = default;

    TaskIdGenerator(const TaskIdGenerator&) = delete;
    TaskIdGenerator& operator=(const TaskIdGenerator&) = delete;

    TaskIdGenerator(TaskIdGenerator&&) = delete;
    TaskIdGenerator& operator=(TaskIdGenerator&&) = delete;

    uint32_t get_next_id() {
        uint32_t next = counter_.fetch_add(1, std::memory_order_relaxed);
        if (next == 0) {
            next = counter_.fetch_add(1, std::memory_order_relaxed);
        }
        return next;
    }

private:
    std::atomic<uint32_t> counter_{1};
};

/**
 * \ingroup task_messenger_manager
 * \brief Contract for components that supply tasks to the manager.
 */
class ITaskGenerator {
public:
    virtual ~ITaskGenerator() = default;

    /**
     * \brief Signal shutdown so generators stop producing work.
     */
    virtual void stop() = 0;
};

/**
 * \ingroup task_messenger_manager
 * \brief Mock generator that demonstrates how applications push tasks into Task Messenger.
 * 
 * Uses typed buffer creation (create_payload_buffer()) for zero-copy buffer
 * ownership transfer and direct data access.
 */
class DefaultTaskGenerator : public ITaskGenerator {
public:
    DefaultTaskGenerator() = default;
    ~DefaultTaskGenerator() override = default;

    /** \brief Halt further task production. */
    void stop() override;

    bool is_stopped() const { return stopped_.load(); }

    /**
     * \brief Set vector size for VectorMath and FusedMultiplyAdd payloads.
     * @param size Number of elements in vector operands (default: 1024).
     */
    void set_vector_size(size_t size) { vector_size_ = size; }

    /**
     * \brief Get current vector size setting.
     */
    size_t vector_size() const { return vector_size_; }

    /**
     * \brief Set response context for async task generation.
     * \param ctx ResponseContext for coroutine resumption
     */
    void set_response_context(std::shared_ptr<ResponseContext> ctx) {
        response_ctx_ = std::move(ctx);
    }

    /**
     * \brief Get the response context.
     */
    std::shared_ptr<ResponseContext> response_context() const { return response_ctx_; }

    /**
     * \brief Coroutine-based async task generator that awaits responses.
     * \param pool Task message pool to enqueue tasks
     * \param initial_count Number of tasks to submit in the chain
     * \return Coroutine task that can be started on ResponseContext
     * 
     * This coroutine submits tasks one at a time, awaits each response,
     * and can process the response to decide on follow-up actions.
     * Resumes on ResponseContext worker threads.
     */
    GeneratorCoroutine run_async_chain(std::shared_ptr<TaskMessagePool> pool,
                                       uint32_t initial_count);

    /**
     * \brief Dispatch N tasks in parallel, each with its own coroutine.
     * \param pool Task message pool to enqueue tasks
     * \param count Number of tasks to dispatch
     * \return Vector of coroutine handles (must be kept alive until all complete)
     * 
     * All tasks are submitted immediately. Each coroutine suspends after
     * submitting its task. As responses arrive from workers, ResponseContext
     * resumes each coroutine to process its response in parallel.
     */
    std::vector<GeneratorCoroutine> dispatch_parallel(
        std::shared_ptr<TaskMessagePool> pool,
        uint32_t count);

    /**
     * \brief Check if all coroutines in a batch have completed.
     * \param coroutines Vector of coroutine handles to check
     * \return true if all coroutines are done
     */
    static bool all_done(const std::vector<GeneratorCoroutine>& coroutines);

private:
    /**
     * \brief Process a single task: submit, await response, process result.
     * \param pool Task message pool
     * \param task_id Unique task identifier
     * \param skill_id Skill to invoke
     * \return Coroutine that suspends until response arrives
     * 
     * This is the per-task coroutine launched by dispatch_parallel().
     * Runs on ResponseContext threads after response arrives.
     */
    GeneratorCoroutine process_single_task(
        std::shared_ptr<TaskMessagePool> pool,
        uint32_t task_id,
        uint32_t skill_id);

    /**
     * \brief Generate task request and response buffers using typed buffer creation.
     * \return Pair of (request_buffer, response_buffer).
     */
    std::pair<std::unique_ptr<PayloadBufferBase>, std::unique_ptr<PayloadBufferBase>> 
    generate_task_data_typed(uint32_t skill_id);

    TaskIdGenerator task_id_generator_;
    std::atomic<bool> stopped_{false};
    size_t vector_size_ = 1024;  ///< Default vector size for vector operations
    std::shared_ptr<ResponseContext> response_ctx_;  ///< Context for async coroutine resumption
};
