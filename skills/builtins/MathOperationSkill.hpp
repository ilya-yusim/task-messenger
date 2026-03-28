/**
 * @file skills/builtins/MathOperationSkill.hpp
 * @brief MathOperation skill - scalar math operations.
 *
 * Performs scalar math operations (add, subtract, multiply, divide)
 * using the unified Skill<Derived> pattern.
 */
#pragma once

#include "skills/registry/CompareUtils.hpp"
#include "skills/registry/Skill.hpp"
#include "skills/registry/PayloadBuffer.hpp"
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
 * Uses scalar-as-vector pattern: all fields stored as single-element vectors
 * for uniform pointer-based access.
 */
struct MathOperationRequestPtrs {
    double* a;         ///< Pointer to operand a (single-element vector)
    double* b;         ///< Pointer to operand b (single-element vector)
    int8_t* operation; ///< Pointer to operation type (single-element [int8] vector)
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
    
    /// Namespaced skill name (single source of truth for identity)
    static constexpr std::string_view kSkillName = "builtin.MathOperation";
    static constexpr std::string_view kSkillDescription = "Performs scalar math operations (add, subtract, multiply, divide)";
    static constexpr uint32_t kSkillVersion = 1;

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
        auto* vec_op = request->mutable_operation();

        // Must be single-element vectors (scalar-as-vector pattern)
        if (vec_a->size() != 1 || vec_b->size() != 1 || !vec_op || vec_op->size() != 1) {
            return std::nullopt;
        }

        return RequestPtrs{
            .a = vec_a->data(),
            .b = vec_b->data(),
            .operation = vec_op->data()
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
     * @brief Create a test request with predefined test data.
     *
     * @param case_index Test case selection:
     *   - 0: Basic add (a=42.0, b=13.0, op=Add)
     *   - 1: Overflow test (a=1e308, b=1e308, op=Add)
     *   - 2: Division by zero (a=10.0, b=0.0, op=Divide)
     * @return MathOperationPayload populated with test data.
     */
    [[nodiscard]] static MathOperationPayload create_test_request(size_t case_index = 0) {
        auto payload = create_request();
        auto& ptrs = payload.ptrs();

        switch (case_index) {
            case 0:  // Basic add
                *ptrs.a = 42.0;
                *ptrs.b = 13.0;
                *ptrs.operation = MathOperation_Add;
                break;
            case 1:  // Overflow test
                *ptrs.a = 1e308;
                *ptrs.b = 1e308;
                *ptrs.operation = MathOperation_Add;
                break;
            case 2:  // Division by zero
                *ptrs.a = 10.0;
                *ptrs.b = 0.0;
                *ptrs.operation = MathOperation_Divide;
                break;
            default:
                *ptrs.a = 0.0;
                *ptrs.b = 0.0;
                *ptrs.operation = MathOperation_Add;
                break;
        }
        return payload;
    }

    /**
     * @brief Get the number of available test cases.
     * @return Number of predefined test cases.
     */
    [[nodiscard]] static constexpr size_t get_test_case_count() noexcept {
        return 3;
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

    /**
     * @brief Create response buffer sized for the given request (PayloadBufferBase overload).
     * @param request The request payload to size the response for.
     * @return Unique pointer to response buffer.
     */
    [[nodiscard]] static std::unique_ptr<PayloadBufferBase> create_response_for_request(
        [[maybe_unused]] const PayloadBufferBase& request
    ) {
        return create_response_for_request(request.span());
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
        MathOperation op = static_cast<MathOperation>(*req.operation);

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
    // Factory methods (used by dispatcher for creating payloads)
    // =========================================================================

    /**
     * @brief Create a request buffer with typed data access.
     *
     * Allocates uninitialized buffers. Caller fills values via ptrs().
     *
     * @return MathOperationPayload with ownership and typed pointers.
     */
    [[nodiscard]] static MathOperationPayload create_request() {
        flatbuffers::FlatBufferBuilder builder(128);

        double* ptr_a = nullptr;
        double* ptr_b = nullptr;
        int8_t* ptr_op = nullptr;

        // Create vectors in reverse order (FlatBuffers requirement)
        auto vec_op = builder.CreateUninitializedVector(1, &ptr_op);
        auto vec_b = builder.CreateUninitializedVector(1, &ptr_b);
        auto vec_a = builder.CreateUninitializedVector(1, &ptr_a);

        auto request = CreateMathOperationRequest(builder, vec_a, vec_b, vec_op);
        builder.Finish(request);

        auto detached = builder.Release();

        // Extract pointers from the FINISHED buffer by parsing it
        auto* req = flatbuffers::GetMutableRoot<MathOperationRequest>(detached.data());
        double* final_ptr_a = const_cast<double*>(req->operand_a()->data());
        double* final_ptr_b = const_cast<double*>(req->operand_b()->data());
        int8_t* final_ptr_op = const_cast<int8_t*>(req->operation()->data());

        RequestPtrs ptrs{
            .a = final_ptr_a,
            .b = final_ptr_b,
            .operation = final_ptr_op
        };

        return MathOperationPayload(std::move(detached), ptrs, kSkillId());
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

        return MathOperationResponseBuffer(std::move(detached), ptrs, kSkillId());
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

    // =========================================================================
    // Verification Support
    // =========================================================================

    /**
     * @brief Compare locally computed result with worker's result.
     *
     * @param computed Response pointers from local computation.
     * @param worker Response pointers from worker's response.
     * @return VerificationResult indicating pass/fail.
     */
    [[nodiscard]] static VerificationResult compare_response(
        const ResponsePtrs& computed,
        const ResponsePtrs& worker
    ) {
        // Check overflow flag first
        if (auto r = compare_int(*computed.overflow, *worker.overflow, "overflow"); !r.passed) {
            return r;
        }
        // Compare result with configured epsilon tolerance
        return compare_scalar(*computed.result, *worker.result, "result");
    }
};

} // namespace TaskMessenger::Skills
