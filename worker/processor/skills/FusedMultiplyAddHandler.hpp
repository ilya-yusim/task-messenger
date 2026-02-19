/**
 * @file worker/processor/skills/FusedMultiplyAddHandler.hpp
 * @brief Handler for fused multiply-add operations (skill_id = 4).
 *
 * Computes: result[i] = a[i] + c * b[i]
 */
#pragma once

#include "ISkillHandler.hpp"
#include "skill_task_generated.h"

namespace TaskMessenger::Skills {

/**
 * @brief Handler for fused multiply-add operations.
 *
 * Receives a FusedMultiplyAddRequest with vectors a, b and scalar c,
 * computes result[i] = a[i] + c * b[i], and returns a FusedMultiplyAddResponse.
 *
 * This handler supports the scalar-as-vector pattern where c is stored
 * as a single-element vector for direct pointer access.
 */
class FusedMultiplyAddHandler : public ISkillHandler {
public:
    static constexpr uint32_t kSkillId = 4;

    [[nodiscard]] uint32_t skill_id() const noexcept override { return kSkillId; }
    [[nodiscard]] const char* skill_name() const noexcept override { return "FusedMultiplyAdd"; }

    [[nodiscard]] bool process(
        std::span<const uint8_t> payload,
        std::vector<uint8_t>& response_out
    ) override {
        auto request = flatbuffers::GetRoot<FusedMultiplyAddRequest>(payload.data());
        if (!request || !request->operand_a() || !request->operand_b() || !request->scalar_c()) {
            return false;
        }

        auto vec_a = request->operand_a();
        auto vec_b = request->operand_b();
        auto scalar_c_vec = request->scalar_c();

        // Vectors must have the same size
        if (vec_a->size() != vec_b->size()) {
            return false;
        }

        // scalar_c should be a single-element vector
        if (scalar_c_vec->size() != 1) {
            return false;
        }

        flatbuffers::uoffset_t size = static_cast<flatbuffers::uoffset_t>(vec_a->size());
        double c = scalar_c_vec->Get(0);

        // Build response with direct write access
        flatbuffers::FlatBufferBuilder builder(64 + size * sizeof(double));

        double* result_ptr = nullptr;
        auto result_offset = builder.CreateUninitializedVector(size, &result_ptr);

        // Compute FMA: result[i] = a[i] + c * b[i]
        for (flatbuffers::uoffset_t i = 0; i < size; ++i) {
            result_ptr[i] = vec_a->Get(i) + c * vec_b->Get(i);
        }

        auto response = CreateFusedMultiplyAddResponse(builder, result_offset);
        builder.Finish(response);

        response_out.assign(
            builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()
        );

        return true;
    }
};

/**
 * @brief Handler for fused multiply-add with true scalar (skill_id = 5).
 *
 * Uses FusedMultiplyAddMutableRequest where scalar_c is a true scalar field.
 * This demonstrates the mutable API pattern.
 */
class FusedMultiplyAddMutableHandler : public ISkillHandler {
public:
    static constexpr uint32_t kSkillId = 5;

    [[nodiscard]] uint32_t skill_id() const noexcept override { return kSkillId; }
    [[nodiscard]] const char* skill_name() const noexcept override { return "FusedMultiplyAddMutable"; }

    [[nodiscard]] bool process(
        std::span<const uint8_t> payload,
        std::vector<uint8_t>& response_out
    ) override {
        auto request = flatbuffers::GetRoot<FusedMultiplyAddMutableRequest>(payload.data());
        if (!request || !request->operand_a() || !request->operand_b()) {
            return false;
        }

        auto vec_a = request->operand_a();
        auto vec_b = request->operand_b();
        double c = request->scalar_c();

        // Vectors must have the same size
        if (vec_a->size() != vec_b->size()) {
            return false;
        }

        flatbuffers::uoffset_t size = static_cast<flatbuffers::uoffset_t>(vec_a->size());

        // Build response with direct write access
        flatbuffers::FlatBufferBuilder builder(64 + size * sizeof(double));

        double* result_ptr = nullptr;
        auto result_offset = builder.CreateUninitializedVector(size, &result_ptr);

        // Compute FMA: result[i] = a[i] + c * b[i]
        for (flatbuffers::uoffset_t i = 0; i < size; ++i) {
            result_ptr[i] = vec_a->Get(i) + c * vec_b->Get(i);
        }

        auto response = CreateFusedMultiplyAddResponse(builder, result_offset);
        builder.Finish(response);

        response_out.assign(
            builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()
        );

        return true;
    }
};

} // namespace TaskMessenger::Skills
