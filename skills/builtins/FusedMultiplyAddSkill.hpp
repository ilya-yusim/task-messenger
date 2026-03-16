/**
 * @file skills/builtins/FusedMultiplyAddSkill.hpp
 * @brief FusedMultiplyAdd skill - computes result[i] = a[i] + c * b[i].
 *
 * This skill demonstrates the Skill<Derived> pattern where handler and
 * payload factory are combined into a single class.
 */
#pragma once

#include "skills/registry/Skill.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/SkillIds.hpp"
#include "FusedMultiplyAddSkill_generated.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace TaskMessenger::Skills {

// Forward declaration
class FusedMultiplyAddSkill;

/**
 * @brief Buffer pointers for FusedMultiplyAdd request.
 *
 * Provides direct pointer access into FlatBuffer memory.
 * Uses scalar-as-vector pattern: scalar c is stored as single-element vector
 * for uniform pointer-based access.
 */
struct FusedMultiplyAddRequestPtrs {
    std::span<double> a;   ///< First operand vector
    std::span<double> b;   ///< Second operand vector
    double* c;             ///< Scalar multiplier (single-element vector)
};

/**
 * @brief Buffer pointers for FusedMultiplyAdd response.
 *
 * Provides mutable access to result vector in FlatBuffer memory.
 */
struct FusedMultiplyAddResponsePtrs {
    std::span<double> result;  ///< Result vector
};

/// @brief Typed payload buffer for FusedMultiplyAdd request.
using FusedMultiplyAddPayload = PayloadBuffer<FusedMultiplyAddRequestPtrs>;

/// @brief Typed payload buffer for FusedMultiplyAdd response.
using FusedMultiplyAddResponseBuffer = PayloadBuffer<FusedMultiplyAddResponsePtrs>;

/**
 * @brief FusedMultiplyAdd skill implementation.
 *
 * Computes: result[i] = a[i] + c * b[i]
 *
 * This class combines the handler and payload factory into a single unit.
 * Skill developers only need to implement compute() - the base class
 * handles scatter/dispatch logic.
 */
class FusedMultiplyAddSkill : public Skill<FusedMultiplyAddSkill> {
public:
    // =========================================================================
    // Required type aliases for Skill<Derived>
    // =========================================================================
    using RequestPtrs = FusedMultiplyAddRequestPtrs;
    using ResponsePtrs = FusedMultiplyAddResponsePtrs;
    
    /// Skill identifier
    static constexpr uint32_t kSkillId = SkillIds::FusedMultiplyAdd;

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
        auto* request = flatbuffers::GetMutableRoot<FusedMultiplyAddRequest>(
            const_cast<uint8_t*>(payload.data()));
        if (!request || !request->operand_a() || !request->operand_b() || !request->scalar_c()) {
            return std::nullopt;
        }

        auto* vec_a = request->mutable_operand_a();
        auto* vec_b = request->mutable_operand_b();
        auto* scalar_c_vec = request->mutable_scalar_c();

        // Vectors must have the same size
        if (vec_a->size() != vec_b->size()) {
            return std::nullopt;
        }

        // scalar_c should be a single-element vector
        if (scalar_c_vec->size() != 1) {
            return std::nullopt;
        }

        return RequestPtrs{
            .a = std::span<double>(vec_a->data(), vec_a->size()),
            .b = std::span<double>(vec_b->data(), vec_b->size()),
            .c = scalar_c_vec->data()
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
        auto* response = flatbuffers::GetMutableRoot<FusedMultiplyAddResponse>(payload.data());
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
        return std::make_unique<FusedMultiplyAddResponseBuffer>(
            create_response(req_ptrs->a.size()));
    }

    // =========================================================================
    // Compute method (the only interesting part for skill developers!)
    // =========================================================================

    /**
     * @brief Compute FMA: result[i] = a[i] + c * b[i]
     *
     * @param req Request pointers (input vectors a, b and scalar c).
     * @param resp Response pointers (output result vector).
     * @return true on success, false on error.
     */
    bool compute(const RequestPtrs& req, ResponsePtrs& resp) {
        const auto& a = req.a;
        const auto& b = req.b;
        double c = *req.c;
        auto size = a.size();
        auto& result = resp.result;

        if (result.size() != size) {
            return false;  // Response buffer size mismatch
        }

        // Compute FMA: result[i] = a[i] + c * b[i]
        for (decltype(size) i = 0; i < size; ++i) {
            result[i] = a[i] + c * b[i];
        }

        return true;
    }

    // =========================================================================
    // Factory methods (used by manager for creating payloads)
    // =========================================================================

    /**
     * @brief Create a request buffer with typed data access.
     *
     * Returns a FusedMultiplyAddPayload combining ownership with spans/pointers
     * directly into the FlatBuffer memory for zero-copy data population.
     *
     * @param vector_size Size of both operand vectors.
     * @param c Initial scalar multiplier value.
     * @return FusedMultiplyAddPayload with ownership and typed pointers.
     */
    [[nodiscard]] static FusedMultiplyAddPayload create_request(
        size_t vector_size,
        double c = 0.0
    ) {
        flatbuffers::FlatBufferBuilder builder(64 + (vector_size * 2 + 1) * sizeof(double));

        double* ptr_a = nullptr;
        double* ptr_b = nullptr;
        double* ptr_c = nullptr;

        auto vec_a = builder.CreateUninitializedVector(vector_size, &ptr_a);
        auto vec_b = builder.CreateUninitializedVector(vector_size, &ptr_b);
        // Scalar stored as single-element vector for direct pointer access
        auto scalar_c = builder.CreateUninitializedVector(1, &ptr_c);
        *ptr_c = c;

        auto request = CreateFusedMultiplyAddRequest(builder, vec_a, vec_b, scalar_c);
        builder.Finish(request);

        auto detached = builder.Release();

        // Extract pointers from the FINISHED buffer by parsing it
        auto* req = flatbuffers::GetMutableRoot<FusedMultiplyAddRequest>(detached.data());
        double* final_ptr_a = const_cast<double*>(req->operand_a()->data());
        double* final_ptr_b = const_cast<double*>(req->operand_b()->data());
        double* final_ptr_c = const_cast<double*>(req->scalar_c()->data());

        RequestPtrs ptrs{
            .a = std::span<double>(final_ptr_a, vector_size),
            .b = std::span<double>(final_ptr_b, vector_size),
            .c = final_ptr_c
        };

        return FusedMultiplyAddPayload(std::move(detached), ptrs, kSkillId);
    }

    /**
     * @brief Create a simple request payload (one-off, no typed pointers).
     * @param a First operand vector.
     * @param b Second operand vector.
     * @param c Scalar multiplier.
     * @return SimplePayload with buffer ownership.
     */
    [[nodiscard]] static SimplePayload create_simple_request(
        const std::vector<double>& a,
        const std::vector<double>& b,
        double c
    ) {
        flatbuffers::FlatBufferBuilder builder(64 + (a.size() + b.size() + 1) * sizeof(double));

        auto vec_a = builder.CreateVector(a);
        auto vec_b = builder.CreateVector(b);
        std::vector<double> c_vec = {c};
        auto scalar_c = builder.CreateVector(c_vec);

        auto request = CreateFusedMultiplyAddRequest(builder, vec_a, vec_b, scalar_c);
        builder.Finish(request);

        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, kSkillId);
    }

    /**
     * @brief Create a response buffer with typed data access.
     *
     * Returns a FusedMultiplyAddResponseBuffer combining ownership with span
     * pointing directly into the FlatBuffer memory for zero-copy result writing.
     *
     * @param vector_size Size of the result vector.
     * @return FusedMultiplyAddResponseBuffer with ownership and typed pointer.
     */
    [[nodiscard]] static FusedMultiplyAddResponseBuffer create_response(size_t vector_size) {
        flatbuffers::FlatBufferBuilder builder(64 + vector_size * sizeof(double));

        double* result_ptr = nullptr;
        auto result_offset = builder.CreateUninitializedVector(vector_size, &result_ptr);
        auto response = CreateFusedMultiplyAddResponse(builder, result_offset);
        builder.Finish(response);

        auto detached = builder.Release();

        // Extract pointer from the FINISHED buffer by parsing it
        auto* resp = flatbuffers::GetMutableRoot<FusedMultiplyAddResponse>(detached.data());
        double* final_result_ptr = const_cast<double*>(resp->result()->data());

        ResponsePtrs ptrs{
            .result = std::span<double>(final_result_ptr, vector_size)
        };

        return FusedMultiplyAddResponseBuffer(std::move(detached), ptrs, kSkillId);
    }

    /**
     * @brief Extract read-only result span from response payload.
     * @param payload Raw FlatBuffer bytes.
     * @return Read-only result span on success, nullopt on failure.
     */
    [[nodiscard]] static std::optional<std::span<const double>> get_result(
        std::span<const uint8_t> payload
    ) noexcept {
        auto response = flatbuffers::GetRoot<FusedMultiplyAddResponse>(payload.data());
        if (!response || !response->result()) {
            return std::nullopt;
        }
        return std::span<const double>(response->result()->data(), response->result()->size());
    }
};

} // namespace TaskMessenger::Skills
