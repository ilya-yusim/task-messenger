/**
 * @file skills/builtins/MathOperationSkill.hpp
 * @brief MathOperation skill - scalar math operations.
 *
 * Performs scalar math operations (add, subtract, multiply, divide)
 * using the unified Skill<Derived> pattern.
 */
#pragma once

#include "skills/registry/Skill.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/SkillIds.hpp"
#include "MathOperationSkill_generated.h"

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <span>

namespace TaskMessenger::Skills {

// Forward declaration
class MathOperationSkill;

/**
 * @brief Buffer pointers for MathOperation request.
 *
 * Uses scalar-as-vector pattern: a, b stored as single-element vectors
 * for uniform pointer-based access.
 */
struct MathOperationRequestPtrs {
    double* a;               ///< Pointer to operand a (single-element vector)
    double* b;               ///< Pointer to operand b (single-element vector)
    MathOperation operation; ///< Operation type
};

/**
 * @brief Buffer pointers for MathOperation response.
 *
 * All fields use scalar-as-vector pattern for uniform pointer access.
 */
struct MathOperationResponsePtrs {
    double* result;    ///< Pointer to result (single-element vector)
    uint8_t* overflow; ///< Pointer to overflow flag (single-element [bool] vector, stored as uint8_t)
};

/// @brief Typed payload buffer for MathOperation request.
using MathOperationPayload = PayloadBuffer<MathOperationRequestPtrs>;

/// @brief Typed payload buffer for MathOperation response.
using MathOperationResponseBuffer = PayloadBuffer<MathOperationResponsePtrs>;

/**
 * @brief MathOperation skill implementation.
 *
 * Performs scalar math operations: add, subtract, multiply, divide.
 *
 * This class combines the handler and payload factory into a single unit.
 * Skill developers only implement compute() - base class handles scatter logic.
 */
class MathOperationSkill : public Skill<MathOperationSkill> {
public:
    // =========================================================================
    // Required type aliases for Skill<Derived>
    // =========================================================================
    using RequestPtrs = MathOperationRequestPtrs;
    using ResponsePtrs = MathOperationResponsePtrs;
    
    /// Skill identifier
    static constexpr uint32_t kSkillId = SkillIds::MathOperation;

    // =========================================================================
    // Scatter methods (required by Skill base class)
    // =========================================================================

    /**
     * @brief Decode request payload into typed pointers.
     * @param payload Raw FlatBuffer bytes.
     * @return Request pointers on success, nullopt on validation failure.
     */
    [[nodiscard]] static std::optional<RequestPtrs> scatter_request(
        std::span<const uint8_t> payload
    ) {
        auto* request = flatbuffers::GetMutableRoot<MathOperationRequest>(
            const_cast<uint8_t*>(payload.data()));
        if (!request || !request->operand_a() || !request->operand_b()) {
            return std::nullopt;
        }

        auto* vec_a = request->mutable_operand_a();
        auto* vec_b = request->mutable_operand_b();

        // Must be single-element vectors (scalar-as-vector pattern)
        if (vec_a->size() != 1 || vec_b->size() != 1) {
            return std::nullopt;
        }

        return RequestPtrs{
            .a = vec_a->data(),
            .b = vec_b->data(),
            .operation = request->operation()
        };
    }

    /**
     * @brief Decode response payload into typed pointers.
     * @param payload Raw FlatBuffer bytes (mutable for writing results).
     * @return Response pointers on success, nullopt on validation failure.
     */
    [[nodiscard]] static std::optional<ResponsePtrs> scatter_response(
        std::span<uint8_t> payload
    ) {
        auto* response = flatbuffers::GetMutableRoot<MathOperationResponse>(payload.data());
        if (!response || !response->result() || !response->overflow()) {
            return std::nullopt;
        }

        auto* result = response->mutable_result();
        auto* overflow = response->mutable_overflow();
        if (result->size() != 1 || overflow->size() != 1) {
            return std::nullopt;
        }

        return ResponsePtrs{
            .result = result->data(),
            .overflow = overflow->data()
        };
    }

    /**
     * @brief Create response buffer sized for the given request.
     * @param request The request payload to size the response for.
     * @return Unique pointer to response buffer.
     */
    [[nodiscard]] static std::unique_ptr<PayloadBufferBase> create_response_for_request(
        [[maybe_unused]] std::span<const uint8_t> request
    ) {
        // MathOperation response is fixed size
        return std::make_unique<MathOperationResponseBuffer>(create_response());
    }

    // =========================================================================
    // Compute method (the only interesting part for skill developers!)
    // =========================================================================

    /**
     * @brief Compute the math operation.
     *
     * @param req Request pointers (operands a, b and operation type).
     * @param resp Response pointers (result and overflow flag).
     * @return true on success, false on error.
     */
    bool compute(const RequestPtrs& req, ResponsePtrs& resp) {
        double a = *req.a;
        double b = *req.b;
        MathOperation op = req.operation;

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

        // Write results via pointers - no FlatBuffer dependency!
        *resp.result = result;
        *resp.overflow = overflow ? 1 : 0;
        
        return true;
    }

    // =========================================================================
    // Factory methods (used by manager for creating payloads)
    // =========================================================================

    /**
     * @brief Create a request buffer with typed data access.
     *
     * @param a Initial operand a value.
     * @param b Initial operand b value.
     * @param op Operation type.
     * @return MathOperationPayload with ownership and typed pointers.
     */
    [[nodiscard]] static MathOperationPayload create_request(
        double a = 0.0,
        double b = 0.0,
        MathOperation op = MathOperation_Add
    ) {
        flatbuffers::FlatBufferBuilder builder(128);

        double* ptr_a = nullptr;
        double* ptr_b = nullptr;

        // Create vectors in reverse order (FlatBuffers requirement)
        auto vec_b = builder.CreateUninitializedVector(1, &ptr_b);
        auto vec_a = builder.CreateUninitializedVector(1, &ptr_a);
        *ptr_a = a;
        *ptr_b = b;

        auto request = CreateMathOperationRequest(builder, vec_a, vec_b, op);
        builder.Finish(request);

        auto detached = builder.Release();

        // Extract pointers from the FINISHED buffer by parsing it
        auto* req = flatbuffers::GetMutableRoot<MathOperationRequest>(detached.data());
        double* final_ptr_a = const_cast<double*>(req->operand_a()->data());
        double* final_ptr_b = const_cast<double*>(req->operand_b()->data());

        RequestPtrs ptrs{
            .a = final_ptr_a,
            .b = final_ptr_b,
            .operation = op
        };

        return MathOperationPayload(std::move(detached), ptrs, kSkillId);
    }

    /**
     * @brief Create a response buffer.
     *
     * @param result Initial result value.
     * @param overflow Initial overflow flag.
     * @return MathOperationResponseBuffer with ownership.
     */
    [[nodiscard]] static MathOperationResponseBuffer create_response(
        double result = 0.0,
        bool overflow = false
    ) {
        flatbuffers::FlatBufferBuilder builder(64);

        double* result_ptr = nullptr;
        uint8_t* overflow_ptr = nullptr;
        
        // Create vectors (order matters for FlatBuffers)
        auto overflow_offset = builder.CreateUninitializedVector(1, &overflow_ptr);
        auto result_offset = builder.CreateUninitializedVector(1, &result_ptr);
        *result_ptr = result;
        *overflow_ptr = overflow ? 1 : 0;

        auto response = CreateMathOperationResponse(builder, result_offset, overflow_offset);
        builder.Finish(response);

        auto detached = builder.Release();

        // Extract pointers from the FINISHED buffer by parsing it
        auto* resp = flatbuffers::GetMutableRoot<MathOperationResponse>(detached.data());
        double* final_result_ptr = const_cast<double*>(resp->result()->data());
        uint8_t* final_overflow_ptr = resp->mutable_overflow()->data();

        ResponsePtrs ptrs{
            .result = final_result_ptr,
            .overflow = final_overflow_ptr
        };

        return MathOperationResponseBuffer(std::move(detached), ptrs, kSkillId);
    }

    /**
     * @brief Extract result from response buffer.
     * @param payload Raw FlatBuffer bytes.
     * @return Result value (0.0 if parse fails).
     */
    [[nodiscard]] static double get_result(std::span<const uint8_t> payload) noexcept {
        auto response = flatbuffers::GetRoot<MathOperationResponse>(payload.data());
        if (!response || !response->result() || response->result()->size() < 1) {
            return 0.0;
        }
        return response->result()->Get(0);
    }

    /**
     * @brief Extract overflow flag from response buffer.
     * @param payload Raw FlatBuffer bytes.
     * @return Overflow flag value.
     */
    [[nodiscard]] static bool get_overflow(std::span<const uint8_t> payload) noexcept {
        auto response = flatbuffers::GetRoot<MathOperationResponse>(payload.data());
        if (!response || !response->overflow() || response->overflow()->size() < 1) {
            return false;
        }
        return response->overflow()->Get(0) != 0;
    }
};

} // namespace TaskMessenger::Skills
