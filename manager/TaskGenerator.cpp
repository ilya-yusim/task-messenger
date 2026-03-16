#include "TaskGenerator.hpp"
#include "skills/registry/SkillRegistry.hpp"
#include "skills/registry/SkillIds.hpp"

#include <utility>

using namespace TaskMessenger::Skills;

/** \ingroup task_messenger_manager */
void DefaultTaskGenerator::stop() {
    stopped_.store(true);
}

/** \ingroup task_messenger_manager */
GeneratorCoroutine DefaultTaskGenerator::run_async_chain(
    std::shared_ptr<TaskMessagePool> pool,
    uint32_t initial_count) 
{
    if (!pool || !response_ctx_) {
        co_return;
    }

    for (uint32_t i = 0; i < initial_count && !stopped_.load(); ++i) {
        uint32_t task_id = task_id_generator_.get_next_id();
        // Cycle through all available skills (1 to MaxSkillId)
        uint32_t skill_id = (i % SkillIds::Count) + 1;

        auto [request, response] = generate_task_data_typed(skill_id);
        
        // Submit task and await response
        auto& result = co_await submit_task(pool, response_ctx_, task_id, 
                                             std::move(request), std::move(response));
        
        if (result.is_success()) {
            // Response received successfully
            // Process result.body_span() here to decide follow-up actions
            // For now, just continue to next task
        } else {
            // Task failed - could implement retry logic here
        }
    }
    
    co_return;
}

/** \ingroup task_messenger_manager */
GeneratorCoroutine DefaultTaskGenerator::process_single_task(
    std::shared_ptr<TaskMessagePool> pool,
    uint32_t task_id,
    uint32_t skill_id) 
{
    if (!pool || !response_ctx_) {
        co_return;
    }

    auto [request, response] = generate_task_data_typed(skill_id);
    
    // Submit task and await response - coroutine suspends here
    auto& result = co_await submit_task(pool, response_ctx_, task_id, 
                                         std::move(request), std::move(response));
    
    if (result.is_success()) {
        // Response received successfully - process on ResponseContext thread
        // Access result.body_span() for response data
        // Can generate follow-up tasks here if needed
    } else {
        // Task failed
    }
    
    co_return;
}

/** \ingroup task_messenger_manager */
std::vector<GeneratorCoroutine> DefaultTaskGenerator::dispatch_parallel(
    std::shared_ptr<TaskMessagePool> pool,
    uint32_t count) 
{
    std::vector<GeneratorCoroutine> coroutines;
    
    if (!pool || !response_ctx_ || count == 0) {
        return coroutines;
    }

    coroutines.reserve(count);
    
    for (uint32_t i = 0; i < count && !stopped_.load(); ++i) {
        uint32_t task_id = task_id_generator_.get_next_id();
        // Cycle through all available skills (1 to MaxSkillId)
        uint32_t skill_id = (i % SkillIds::Count) + 1;
        
        // Launch coroutine - starts immediately, submits task, then suspends
        coroutines.emplace_back(process_single_task(pool, task_id, skill_id));
    }
    
    // All tasks submitted, coroutines suspended waiting for responses
    // Caller must keep this vector alive until all_done() returns true
    return coroutines;
}

/** \ingroup task_messenger_manager */
bool DefaultTaskGenerator::all_done(const std::vector<GeneratorCoroutine>& coroutines) {
    for (const auto& coro : coroutines) {
        if (!coro.done()) {
            return false;
        }
    }
    return true;
}

/** \ingroup task_messenger_manager */
std::pair<std::unique_ptr<PayloadBufferBase>, std::unique_ptr<PayloadBufferBase>> 
DefaultTaskGenerator::generate_task_data_typed(uint32_t skill_id) {
    switch (skill_id) {
        case SkillIds::StringReversal: {
            // StringReversal doesn't benefit from typed buffers (variable-length string)
            auto request = std::make_unique<SimplePayload>(
                StringReversalSkill::create_request("Hello, World!"));
            auto response = std::make_unique<SimplePayload>(
                StringReversalSkill::create_response(13));  // "Hello, World!" is 13 chars
            return {std::move(request), std::move(response)};
        }
        case SkillIds::MathOperation: {
            // Create typed buffer with initial values
            auto request = std::make_unique<MathOperationPayload>(
                MathOperationSkill::create_request(42.0, 13.0, MathOperation_Add));
            auto response = std::make_unique<MathOperationResponseBuffer>(
                MathOperationSkill::create_response());
            return {std::move(request), std::move(response)};
        }
        case SkillIds::VectorMath: {
            // Create typed buffer, write directly into spans
            auto request = std::make_unique<VectorMathPayload>(
                VectorMathSkill::create_request(vector_size_));
            for (size_t i = 0; i < vector_size_; ++i) {
                request->ptrs().a[i] = static_cast<double>(i + 1);
                request->ptrs().b[i] = static_cast<double>(i + 4);
            }
            // VectorMath operation is set during create_request
            auto response = std::make_unique<VectorMathResponseBuffer>(
                VectorMathSkill::create_response(vector_size_));
            return {std::move(request), std::move(response)};
        }
        case SkillIds::FusedMultiplyAdd: {
            // Create typed buffer, write directly into spans and scalar pointer
            auto request = std::make_unique<FusedMultiplyAddPayload>(
                FusedMultiplyAddSkill::create_request(vector_size_));
            for (size_t i = 0; i < vector_size_; ++i) {
                request->ptrs().a[i] = static_cast<double>(i + 1);
                request->ptrs().b[i] = static_cast<double>(i + 4);
            }
            *request->ptrs().c = 2.0;
            auto response = std::make_unique<FusedMultiplyAddResponseBuffer>(
                FusedMultiplyAddSkill::create_response(vector_size_));
            return {std::move(request), std::move(response)};
        }
        default: {
            // Fallback: use MathOperation
            auto request = std::make_unique<MathOperationPayload>(
                MathOperationSkill::create_request(0.0, 0.0, MathOperation_Add));
            auto response = std::make_unique<MathOperationResponseBuffer>(
                MathOperationSkill::create_response());
            return {std::move(request), std::move(response)};
        }
    }
}

