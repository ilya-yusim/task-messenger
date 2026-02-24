/**
 * @file skills/builtins/MathOperationPayload.hpp
 * @brief Payload factory for MathOperation skill.
 */
#pragma once

#include "skills/registry/IPayloadFactory.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/SkillIds.hpp"
#include "MathOperationSkill_generated.h"

#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace TaskMessenger::Skills {

/**
 * @brief Buffer pointers for MathOperation skill (templated on constness).
 * @tparam Const If true, provides read-only pointers; if false, provides mutable pointers.
 * 
 * For scalar operations, use the generated mutate_*() methods directly
 * via get_mutable_request() instead of these pointers on the write side.
 */
template<bool Const>
struct MathOperationPtrs {
    using DoublePtr = std::conditional_t<Const, const double*, double*>;
    
    DoublePtr a;            ///< First operand (nullptr on write side, use mutate methods)
    DoublePtr b;            ///< Second operand (nullptr on write side, use mutate methods)
    MathOperation operation;  ///< Operation type
};

/// @brief Mutable buffer pointers for writing MathOperation request.
using MathOperationBufferPtrs = MathOperationPtrs<false>;

/// @brief Read-only view pointers for decoding MathOperation request.
using MathOperationViewPtrs = MathOperationPtrs<true>;

/**
 * @brief Decoded MathOperation request with scalar storage.
 * 
 * Stores scalar values (no pointer into buffer for FlatBuffer scalar fields).
 * This struct must outlive any use of its pointers.
 */
struct MathOperationDecodedRequest {
    double a_storage;           ///< First operand storage
    double b_storage;           ///< Second operand storage
    const double* a = &a_storage;  ///< Pointer to first operand
    const double* b = &b_storage;  ///< Pointer to second operand
    MathOperation operation;    ///< Operation type
    
    /// @brief Get view pointers (for uniform access pattern).
    [[nodiscard]] MathOperationViewPtrs ptrs() const noexcept {
        return MathOperationViewPtrs{.a = a, .b = b, .operation = operation};
    }
};

/// @brief Typed payload buffer for MathOperation skill.
using MathOperationPayload = PayloadBuffer<MathOperationBufferPtrs>;

/**
 * @brief Payload factory for scalar math operations.
 *
 * Creates FlatBuffers payloads for MathOperationRequest.
 * Uses --gen-mutable for direct field mutation.
 */
class MathOperationPayloadFactory : public IPayloadFactory {
public:
    [[nodiscard]] uint32_t skill_id() const noexcept override {
        return SkillIds::MathOperation;
    }

    /**
     * @brief Create a payload buffer with mutable field access.
     * 
     * Returns a MathOperationPayload. Use get_mutable_request() with
     * mutate_operand_a(), mutate_operand_b(), and mutate_operation() to update values.
     * 
     * @param a Initial first operand.
     * @param b Initial second operand.
     * @param op Initial operation type.
     * @return MathOperationPayload with ownership (use mutate methods for changes).
     */
    [[nodiscard]] static MathOperationPayload create_payload_buffer(
        double a = 0.0, 
        double b = 0.0, 
        MathOperation op = MathOperation_Add
    ) {
        flatbuffers::FlatBufferBuilder builder(64);
        
        auto request = CreateMathOperationRequest(builder, a, b, op);
        builder.Finish(request);
        
        // For scalar fields, use mutate methods via get_mutable_request()
        MathOperationBufferPtrs ptrs{
            .a = nullptr,  // Use get_mutable_request()->mutate_operand_a()
            .b = nullptr   // Use get_mutable_request()->mutate_operand_b()
        };
        
        return MathOperationPayload(builder.Release(), ptrs, SkillIds::MathOperation);
    }

    /**
     * @brief Get mutable access to the request for changing field values.
     * 
     * @param payload The payload to get mutable access to.
     * @return Mutable pointer to the MathOperationRequest.
     */
    [[nodiscard]] static MathOperationRequest* get_mutable_request(
        MathOperationPayload& payload
    ) noexcept {
        return flatbuffers::GetMutableRoot<MathOperationRequest>(payload.mutable_data());
    }

    /**
     * @brief Create a simple payload (one-off, no typed pointers needed).
     * @param a First operand.
     * @param b Second operand.
     * @param op Operation type (Add, Subtract, Multiply, Divide).
     * @return SimplePayload with buffer ownership.
     */
    [[nodiscard]] static SimplePayload create_payload(double a, double b, MathOperation op) {
        flatbuffers::FlatBufferBuilder builder(64);
        
        auto request = CreateMathOperationRequest(builder, a, b, op);
        builder.Finish(request);
        
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::MathOperation);
    }

    /**
     * @brief Decode a request payload into typed view pointers.
     * 
     * Extracts scalar values into a DecodedRequest.
     * 
     * @param payload Raw payload bytes from TaskMessage.
     * @return Decoded request with typed pointers, or nullopt if validation fails.
     */
    [[nodiscard]] static std::optional<MathOperationDecodedRequest> decode_request(
        std::span<const uint8_t> payload
    ) noexcept {
        auto request = flatbuffers::GetRoot<MathOperationRequest>(payload.data());
        if (!request) {
            return std::nullopt;
        }

        return MathOperationDecodedRequest{
            .a_storage = request->operand_a(),
            .b_storage = request->operand_b(),
            .operation = request->operation()
        };
    }

    /**
     * @brief Create a response buffer for scalar math result.
     * 
     * @param result Initial result value.
     * @param overflow Initial overflow flag.
     * @return SimplePayload with the response.
     */
    [[nodiscard]] static SimplePayload create_response_buffer(
        double result = 0.0,
        bool overflow = false
    ) {
        flatbuffers::FlatBufferBuilder builder(64);
        auto response = CreateMathOperationResponse(builder, result, overflow);
        builder.Finish(response);
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::MathOperation);
    }
};

} // namespace TaskMessenger::Skills
