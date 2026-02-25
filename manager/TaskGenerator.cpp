#include "TaskGenerator.hpp"
#include "skills/registry/SkillRegistry.hpp"
#include "skills/registry/SkillIds.hpp"

#include <utility>

using namespace TaskMessenger::Skills;

/** \ingroup task_messenger_manager */
void DefaultTaskGenerator::generate_tasks(std::shared_ptr<TaskMessagePool> pool, uint32_t count) {
    if (!pool || stopped_.load()) {
        return;
    }

    auto tasks = make_tasks(count);
    if (!tasks.empty()) {
        pool->add_tasks(std::move(tasks));
    }
}

/** \ingroup task_messenger_manager */
std::vector<TaskMessage> DefaultTaskGenerator::make_tasks(uint32_t count) {
    std::vector<TaskMessage> out;
    if (stopped_.load() || count == 0) {
        return out;
    }

    out.reserve(count);
    for (uint32_t i = 0; i < count && !stopped_.load(); ++i) {
        uint32_t task_id = task_id_generator_.get_next_id();
        // Cycle through all available skills (1 to MaxSkillId)
        uint32_t skill_id = (i % SkillIds::Count) + 1;

        auto buffer = generate_task_data_typed(skill_id);
        out.emplace_back(task_id, std::move(buffer));
    }

    return out;
}

/** \ingroup task_messenger_manager */
void DefaultTaskGenerator::stop() {
    stopped_.store(true);
}

/** \ingroup task_messenger_manager */
std::unique_ptr<PayloadBufferBase> DefaultTaskGenerator::generate_task_data_typed(uint32_t skill_id) {
    switch (skill_id) {
        case SkillIds::StringReversal: {
            // StringReversal doesn't benefit from typed buffers (variable-length string)
            return std::make_unique<SimplePayload>(
                StringReversalPayloadFactory::create_payload("Hello, World!"));
        }
        case SkillIds::MathOperation: {
            // Create typed buffer with initial values
            return std::make_unique<MathOperationPayload>(
                MathOperationPayloadFactory::create_payload_buffer(42.0, 13.0, MathOperation_Add));
        }
        case SkillIds::VectorMath: {
            // Create typed buffer, write directly into spans
            auto payload = std::make_unique<VectorMathPayload>(
                VectorMathPayloadFactory::create_payload_buffer(vector_size_));
            for (size_t i = 0; i < vector_size_; ++i) {
                payload->ptrs().a[i] = static_cast<double>(i + 1);
                payload->ptrs().b[i] = static_cast<double>(i + 4);
            }
            auto* req = VectorMathPayloadFactory::get_mutable_request(*payload);
            req->mutate_operation(MathOperation_Add);
            return payload;
        }
        case SkillIds::FusedMultiplyAdd: {
            // Create typed buffer, write directly into spans and scalar pointer
            auto payload = std::make_unique<FusedMultiplyAddPayload>(
                FusedMultiplyAddPayloadFactory::create_payload_buffer(vector_size_));
            for (size_t i = 0; i < vector_size_; ++i) {
                payload->ptrs().a[i] = static_cast<double>(i + 1);
                payload->ptrs().b[i] = static_cast<double>(i + 4);
            }
            *payload->ptrs().c = 2.0;
            return payload;
        }
        case SkillIds::FusedMultiplyAddMutable: {
            // Create typed buffer, write into spans, use mutate for scalar
            auto payload = std::make_unique<FusedMultiplyAddPayload>(
                FusedMultiplyAddMutablePayloadFactory::create_payload_buffer(vector_size_));
            for (size_t i = 0; i < vector_size_; ++i) {
                payload->ptrs().a[i] = static_cast<double>(i + 1);
                payload->ptrs().b[i] = static_cast<double>(i + 4);
            }
            auto* req = FusedMultiplyAddMutablePayloadFactory::get_mutable_request(*payload);
            req->mutate_scalar_c(2.0);
            return payload;
        }
        default: {
            // Fallback: use MathOperation
            return std::make_unique<MathOperationPayload>(
                MathOperationPayloadFactory::create_payload_buffer(0.0, 0.0, MathOperation_Add));
        }
    }
}

