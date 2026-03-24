#include "TaskGenerator.hpp"
#include "dispatcher/DispatcherApp.hpp"

using namespace TaskMessenger::Skills;

void TaskGenerator::set_app(DispatcherApp* app) {
    app_ = app;
}

void TaskGenerator::stop() {
    stopped_.store(true);
}

GeneratorCoroutine TaskGenerator::process_single_task(
    uint32_t task_id,
    uint32_t skill_id,
    std::unique_ptr<PayloadBufferBase> request,
    std::unique_ptr<PayloadBufferBase> response,
    std::unique_ptr<PayloadBufferBase> request_copy)
{
    if (!app_) {
        co_return;
    }

    // Submit task and await response - coroutine suspends here
    auto& result = co_await app_->submit_task(task_id,
                                               std::move(request), std::move(response));

    if (result.is_success() && request_copy) {
        (void)VerificationHelper::verify(
            task_id, skill_id, request_copy->span(), result.body_span_u8());
    }

    co_return;
}

std::vector<GeneratorCoroutine> TaskGenerator::dispatch_tasks(
    std::vector<TestTaskData> tasks,
    VerificationHelper* verifier)
{
    std::vector<GeneratorCoroutine> coroutines;

    if (!app_ || tasks.empty()) {
        return coroutines;
    }

    coroutines.reserve(tasks.size());

    for (auto& task : tasks) {
        if (stopped_.load()) {
            break;
        }

        uint32_t task_id = task_id_generator_.get_next_id();

        // Clone request for verification at the dispatch site
        std::unique_ptr<PayloadBufferBase> request_copy;
        if (verifier && task.request) {
            request_copy = verifier->clone_request(*task.request);
        }

        coroutines.emplace_back(process_single_task(
            task_id,
            task.skill_id,
            std::move(task.request),
            std::move(task.response),
            std::move(request_copy)));
    }

    return coroutines;
}

bool TaskGenerator::all_done(const std::vector<GeneratorCoroutine>& coroutines) {
    for (const auto& coro : coroutines) {
        if (!coro.done()) {
            return false;
        }
    }
    return true;
}
