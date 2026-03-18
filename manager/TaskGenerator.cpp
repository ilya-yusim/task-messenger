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
        
        // Clone request for verification after response arrives
        auto request_copy = request->clone();
        
        // Submit task and await response
        auto& result = co_await submit_task(pool, response_ctx_, task_id, 
                                             std::move(request), std::move(response));
        
        if (result.is_success()) {
            // Verify worker's response against locally computed result
            auto verification = SkillRegistry::instance().verify_response(
                skill_id, request_copy->span(), result.body_span_u8());
            // verification.passed indicates success/failure
            // verification.message contains diagnostic info on failure
            (void)verification;  // TODO: Log or report verification result
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
    
    // Clone request for verification after response arrives
    auto request_copy = request->clone();
    
    // Submit task and await response - coroutine suspends here
    auto& result = co_await submit_task(pool, response_ctx_, task_id, 
                                         std::move(request), std::move(response));
    
    if (result.is_success()) {
        // Verify worker's response against locally computed result
        auto verification = SkillRegistry::instance().verify_response(
            skill_id, request_copy->span(), result.body_span_u8());
        // verification.passed indicates success/failure
        // verification.message contains diagnostic info on failure
        (void)verification;  // TODO: Log or report verification result
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
    auto& registry = SkillRegistry::instance();
    
    auto request = registry.create_test_request_buffer(skill_id, 0);
    if (!request) {
        // Fallback to MathOperation if skill not found
        request = registry.create_test_request_buffer(SkillIds::MathOperation, 0);
        if (!request) {
            return {nullptr, nullptr};
        }
        skill_id = SkillIds::MathOperation;
    }
    
    auto response = registry.create_response_buffer(skill_id, request->span());
    return {std::move(request), std::move(response)};
}

