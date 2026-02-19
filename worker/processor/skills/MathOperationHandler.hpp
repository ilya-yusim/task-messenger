/**
 * @file worker/processor/skills/MathOperationHandler.hpp
 * @brief Handler for the scalar math operation skill (skill_id = 2).
 */
#pragma once

#include "ISkillHandler.hpp"
#include "skill_task_generated.h"

#include <cmath>
#include <limits>

namespace TaskMessenger::Skills {

/**
 * @brief Handler for scalar math operations.
 *
 * Receives a MathOperationRequest with two operands and an operation type,
 * performs the calculation, and returns a MathOperationResponse.
 */
class MathOperationHandler : public ISkillHandler {
public:
    static constexpr uint32_t kSkillId = 2;

    [[nodiscard]] uint32_t skill_id() const noexcept override { return kSkillId; }
    [[nodiscard]] const char* skill_name() const noexcept override { return "MathOperation"; }

    [[nodiscard]] bool process(
        std::span<const uint8_t> payload,
        std::vector<uint8_t>& response_out
    ) override {
        auto request = flatbuffers::GetRoot<MathOperationRequest>(payload.data());
        if (!request) {
            return false;
        }

        double a = request->operand_a();
        double b = request->operand_b();
        MathOperation op = request->operation();

        double result = 0.0;
        bool overflow = false;

        switch (op) {
            case MathOperation_Add:
                result = a + b;
                overflow = std::isinf(result);
                break;
            case MathOperation_Subtract:
                result = a - b;
                overflow = std::isinf(result);
                break;
            case MathOperation_Multiply:
                result = a * b;
                overflow = std::isinf(result);
                break;
            case MathOperation_Divide:
                if (b == 0.0) {
                    overflow = true;
                    result = std::numeric_limits<double>::quiet_NaN();
                } else {
                    result = a / b;
                    overflow = std::isinf(result);
                }
                break;
            default:
                return false;  // Unknown operation
        }

        // Build response
        flatbuffers::FlatBufferBuilder builder(64);
        auto response = CreateMathOperationResponse(builder, result, overflow);
        builder.Finish(response);

        response_out.assign(
            builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()
        );

        return true;
    }
};

} // namespace TaskMessenger::Skills
