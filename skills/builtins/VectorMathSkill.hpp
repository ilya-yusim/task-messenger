/**
 * @file skills/builtins/VectorMathSkill.hpp
 * @brief VectorMath skill - element-wise vector math operations.
 *
 * Performs element-wise math operations (add, subtract, multiply, divide)
 * using the unified Skill<Derived> pattern.
 */
#pragma once

#include "skills/registry/Skill.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/SkillIds.hpp"
#include "VectorMathSkill_generated.h"

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace TaskMessenger::Skills {

// Forward declaration
class VectorMathSkill;

/**
 * @brief Buffer pointers for VectorMath request.
 */
struct VectorMathRequestPtrs {
    std::span<double> a;      ///< First operand vector
    std::span<double> b;      ///< Second operand vector
    MathOperation operation;  ///< Operation type
};

/**
 * @brief Buffer pointers for VectorMath response.
 */
struct VectorMathResponsePtrs {
    std::span<double> result;  ///< Result vector
};

/// @brief Typed payload buffer for VectorMath request.
using VectorMathPayload = PayloadBuffer<VectorMathRequestPtrs>;

/// @brief Typed payload buffer for VectorMath response.
using VectorMathResponseBuffer = PayloadBuffer<VectorMathResponsePtrs>;

/**
 * @brief VectorMath skill implementation.
 *
 * Performs element-wise vector math operations.
 *
 * This class combines the handler and payload factory into a single unit.
 * Skill developers only implement compute() - base class handles scatter logic.
 */
class VectorMathSkill : public Skill<VectorMathSkill> {
public:
    // =========================================================================
    // Required type aliases for Skill<Derived>
    // =========================================================================
    using RequestPtrs = VectorMathRequestPtrs;
    using ResponsePtrs = VectorMathResponsePtrs;
    
    /// Skill identifier
    static constexpr uint32_t kSkillId = SkillIds::VectorMath;

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
        auto* request = flatbuffers::GetMutableRoot<VectorMathRequest>(
            const_cast<uint8_t*>(payload.data()));
        if (!request || !request->operand_a() || !request->operand_b()) {
            return std::nullopt;
        }

        auto* vec_a = request->mutable_operand_a();
        auto* vec_b = request->mutable_operand_b();

        // Vectors must have the same size
        if (vec_a->size() != vec_b->size()) {
            return std::nullopt;
        }

        return RequestPtrs{
            .a = std::span<double>(vec_a->data(), vec_a->size()),
            .b = std::span<double>(vec_b->data(), vec_b->size()),
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
        auto* response = flatbuffers::GetMutableRoot<VectorMathResponse>(payload.data());
        if (!response || !response->result()) {
            return std::nullopt;
        }

        auto* result = response->mutable_result();
        return ResponsePtrs{
            .result = std::span<double>(result->data(), result->size())
        };
    }

    /**
     * @brief Create response buffer sized for the given request.
     * @param request The request payload to size the response for.
     * @return Unique pointer to response buffer, or nullptr on failure.
     */
    [[nodiscard]] static std::unique_ptr<PayloadBufferBase> create_response_for_request(
        std::span<const uint8_t> request
    ) {
        auto req_ptrs = scatter_request(request);
        if (!req_ptrs) {
            return nullptr;
        }
        return std::make_unique<VectorMathResponseBuffer>(
            create_response(req_ptrs->a.size()));
    }

    // =========================================================================
    // Compute method (the only interesting part for skill developers!)
    // =========================================================================

    /**
     * @brief Compute element-wise vector math operation.
     *
     * @param req Request pointers (operand vectors a, b and operation type).
     * @param resp Response pointers (result vector).
     * @return true on success, false on error.
     */
    bool compute(const RequestPtrs& req, ResponsePtrs& resp) {
        const auto& a = req.a;
        const auto& b = req.b;
        auto size = a.size();
        MathOperation op = req.operation;
        auto& result = resp.result;

        if (result.size() != size) {
            return false;  // Response buffer size mismatch
        }

        // Compute results directly into the buffer
        for (decltype(size) i = 0; i < size; ++i) {
            switch (op) {
                case MathOperation_Add:
                    result[i] = a[i] + b[i];
                    break;
                case MathOperation_Subtract:
                    result[i] = a[i] - b[i];
                    break;
                case MathOperation_Multiply:
                    result[i] = a[i] * b[i];
                    break;
                case MathOperation_Divide:
                    result[i] = (b[i] != 0.0) ? a[i] / b[i] : std::numeric_limits<double>::quiet_NaN();
                    break;
                default:
                    return false;
            }
        }

        return true;
    }

    // =========================================================================
    // Factory methods (used by manager for creating payloads)
    // =========================================================================

    /**
     * @brief Create a request buffer with typed data access.
     *
     * @param vector_size Size of both operand vectors.
     * @param op Initial operation type.
     * @return VectorMathPayload with ownership and typed pointers.
     */
    [[nodiscard]] static VectorMathPayload create_request(
        size_t vector_size,
        MathOperation op = MathOperation_Add
    ) {
        flatbuffers::FlatBufferBuilder builder(64 + vector_size * 2 * sizeof(double));

        double* ptr_a = nullptr;
        double* ptr_b = nullptr;

        auto vec_a = builder.CreateUninitializedVector(vector_size, &ptr_a);
        auto vec_b = builder.CreateUninitializedVector(vector_size, &ptr_b);
        auto request = CreateVectorMathRequest(builder, vec_a, vec_b, op);
        builder.Finish(request);

        auto detached = builder.Release();

        // Extract pointers from the FINISHED buffer by parsing it
        auto* req = flatbuffers::GetMutableRoot<VectorMathRequest>(detached.data());
        double* final_ptr_a = const_cast<double*>(req->operand_a()->data());
        double* final_ptr_b = const_cast<double*>(req->operand_b()->data());

        RequestPtrs ptrs{
            .a = std::span<double>(final_ptr_a, vector_size),
            .b = std::span<double>(final_ptr_b, vector_size),
            .operation = op
        };

        return VectorMathPayload(std::move(detached), ptrs, kSkillId);
    }

    /**
     * @brief Create a simple request payload (one-off, no typed pointers).
     * @param a First operand vector.
     * @param b Second operand vector.
     * @param op Operation type.
     * @return SimplePayload with buffer ownership.
     */
    [[nodiscard]] static SimplePayload create_simple_request(
        const std::vector<double>& a,
        const std::vector<double>& b,
        MathOperation op
    ) {
        flatbuffers::FlatBufferBuilder builder(64 + (a.size() + b.size()) * sizeof(double));

        auto vec_a = builder.CreateVector(a);
        auto vec_b = builder.CreateVector(b);
        auto request = CreateVectorMathRequest(builder, vec_a, vec_b, op);
        builder.Finish(request);

        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, kSkillId);
    }

    /**
     * @brief Create a response buffer with typed data access.
     *
     * @param vector_size Size of the result vector.
     * @return VectorMathResponseBuffer with ownership and typed pointer.
     */
    [[nodiscard]] static VectorMathResponseBuffer create_response(size_t vector_size) {
        flatbuffers::FlatBufferBuilder builder(64 + vector_size * sizeof(double));

        double* result_ptr = nullptr;
        auto result_offset = builder.CreateUninitializedVector(vector_size, &result_ptr);
        auto response = CreateVectorMathResponse(builder, result_offset);
        builder.Finish(response);

        auto detached = builder.Release();

        // Extract pointer from the FINISHED buffer by parsing it
        auto* resp = flatbuffers::GetMutableRoot<VectorMathResponse>(detached.data());
        double* final_result_ptr = const_cast<double*>(resp->result()->data());

        ResponsePtrs ptrs{
            .result = std::span<double>(final_result_ptr, vector_size)
        };

        return VectorMathResponseBuffer(std::move(detached), ptrs, kSkillId);
    }

    /**
     * @brief Extract read-only response pointers.
     * @param payload Raw FlatBuffer bytes.
     * @return Read-only result span on success, nullopt on failure.
     */
    [[nodiscard]] static std::optional<std::span<const double>> get_result(
        std::span<const uint8_t> payload
    ) noexcept {
        auto response = flatbuffers::GetRoot<VectorMathResponse>(payload.data());
        if (!response || !response->result()) {
            return std::nullopt;
        }
        return std::span<const double>(response->result()->data(), response->result()->size());
    }
};

} // namespace TaskMessenger::Skills
