/**
 * @file worker/processor/skills/VectorMathHandler.hpp
 * @brief Handler for element-wise vector math operations (skill_id = 3).
 */
#pragma once

#include "ISkillHandler.hpp"
#include "skill_task_generated.h"

#include <cmath>

namespace TaskMessenger::Skills {

/**
 * @brief Handler for element-wise vector math operations.
 *
 * Receives a VectorMathRequest with two operand vectors and an operation,
 * performs element-wise calculation, and returns a VectorMathResponse.
 *
 * Demonstrates direct buffer write access with CreateUninitializedVector.
 */
class VectorMathHandler : public ISkillHandler {
public:
    static constexpr uint32_t kSkillId = 3;

    [[nodiscard]] uint32_t skill_id() const noexcept override { return kSkillId; }
    [[nodiscard]] const char* skill_name() const noexcept override { return "VectorMath"; }

    [[nodiscard]] bool process(
        std::span<const uint8_t> payload,
        std::vector<uint8_t>& response_out
    ) override {
        auto request = flatbuffers::GetRoot<VectorMathRequest>(payload.data());
        if (!request || !request->operand_a() || !request->operand_b()) {
            return false;
        }

        auto vec_a = request->operand_a();
        auto vec_b = request->operand_b();

        // Vectors must have the same size
        if (vec_a->size() != vec_b->size()) {
            return false;
        }

        flatbuffers::uoffset_t size = static_cast<flatbuffers::uoffset_t>(vec_a->size());
        MathOperation op = request->operation();

        // Build response with direct write access
        flatbuffers::FlatBufferBuilder builder(64 + size * sizeof(double));

        // Allocate uninitialized vector for direct write
        double* result_ptr = nullptr;
        auto result_offset = builder.CreateUninitializedVector(size, &result_ptr);

        // Compute results directly into the buffer
        for (flatbuffers::uoffset_t i = 0; i < size; ++i) {
            double a = vec_a->Get(i);
            double b = vec_b->Get(i);

            switch (op) {
                case MathOperation_Add:
                    result_ptr[i] = a + b;
                    break;
                case MathOperation_Subtract:
                    result_ptr[i] = a - b;
                    break;
                case MathOperation_Multiply:
                    result_ptr[i] = a * b;
                    break;
                case MathOperation_Divide:
                    result_ptr[i] = (b != 0.0) ? a / b : std::numeric_limits<double>::quiet_NaN();
                    break;
                default:
                    return false;
            }
        }

        auto response = CreateVectorMathResponse(builder, result_offset);
        builder.Finish(response);

        response_out.assign(
            builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()
        );

        return true;
    }
};

} // namespace TaskMessenger::Skills
