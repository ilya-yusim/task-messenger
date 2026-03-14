/**
 * @file skills/builtins/MathOperationPayload.hpp
 * @brief Payload factory for MathOperation skill.
 * 
 * MathOperation uses scalar-as-vector pattern: operand_a, operand_b, and result
 * are stored as single-element [double] vectors for uniform pointer access.
 */
#pragma once

#include "skills/registry/IPayloadFactory.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/SkillIds.hpp"
#include "MathOperationSkill_generated.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace TaskMessenger::Skills {

/**
 * @brief Direct buffer pointers for MathOperation request (write side).
 * 
 * All scalar fields are stored as single-element vectors for uniform `double*` access.
 */
struct MathOperationRequestPtrs {
    double* a;               ///< Pointer to operand a (single-element vector)
    double* b;               ///< Pointer to operand b (single-element vector)
    MathOperation operation; ///< Operation type
};

/**
 * @brief Response buffer pointers for MathOperation (templated on constness).
 * 
 * @tparam Const If true, provides read-only pointers (verification);
 *               if false, provides mutable pointers (handler write).
 */
template<bool Const>
struct MathOperationResponsePtrsT {
    using DoublePtr = std::conditional_t<Const, const double*, double*>;

    DoublePtr result;        ///< Pointer to result (single-element vector)
    bool* overflow;          ///< Pointer to overflow flag (nullptr for read-only)
};

/// @brief Mutable response pointers for handlers writing results.
using MathOperationResponsePtrs = MathOperationResponsePtrsT<false>;

/// @brief Read-only response pointers for verification/read access.
using MathOperationResponseViewPtrs = MathOperationResponsePtrsT<true>;

/// @brief Typed payload buffer for MathOperation skill.
using MathOperationPayload = PayloadBuffer<MathOperationRequestPtrs>;

/**
 * @brief Payload factory for scalar math operations.
 *
 * Creates FlatBuffers payloads for MathOperationRequest using scalar-as-vector
 * pattern for uniform pointer access to all fields.
 */
class MathOperationPayloadFactory : public IPayloadFactory {
public:
    [[nodiscard]] uint32_t skill_id() const noexcept override {
        return SkillIds::MathOperation;
    }

    /**
     * @brief Create a pre-allocated response buffer sized for a given request.
     */
    [[nodiscard]] std::unique_ptr<PayloadBufferBase> create_response_buffer_for_request(
        [[maybe_unused]] std::span<const uint8_t> request
    ) const override {
        return std::make_unique<SimplePayload>(create_response_buffer());
    }

    /**
     * @brief Create a payload buffer with direct pointer access.
     * 
     * Returns a MathOperationPayload with pointers to operand_a, operand_b.
     * Modify values via direct pointer writes: `*ptrs.a = value;`
     * 
     * @param a Initial first operand.
     * @param b Initial second operand.
     * @param op Initial operation type.
     * @return MathOperationPayload with ownership and direct buffer pointers.
     */
    [[nodiscard]] static MathOperationPayload create_payload_buffer(
        double a = 0.0, 
        double b = 0.0, 
        MathOperation op = MathOperation_Add
    ) {
        flatbuffers::FlatBufferBuilder builder(128);

        // Create single-element vectors for scalar fields
        double* a_ptr = nullptr;
        double* b_ptr = nullptr;

        // Create vectors in reverse order (FlatBuffers requirement)
        auto b_offset = builder.CreateUninitializedVector(1, &b_ptr);
        auto a_offset = builder.CreateUninitializedVector(1, &a_ptr);

        // Initialize values
        *a_ptr = a;
        *b_ptr = b;

        auto request = CreateMathOperationRequest(builder, a_offset, b_offset, op);
        builder.Finish(request);

        // Pointers become invalid after Release() - must recalculate
        auto buffer = builder.Release();

        // Get mutable pointers from final buffer
        auto mutable_request = flatbuffers::GetMutableRoot<MathOperationRequest>(buffer.data());
        
        MathOperationRequestPtrs ptrs{
            .a = const_cast<double*>(mutable_request->operand_a()->data()),
            .b = const_cast<double*>(mutable_request->operand_b()->data()),
            .operation = op
        };

        return MathOperationPayload(std::move(buffer), ptrs, SkillIds::MathOperation);
    }

    /**
     * @brief Extract request pointers from a buffer span.
     * 
     * @param payload Buffer span (request sent to handler).
     * @return Request pointers, or nullopt if validation fails.
     */
    [[nodiscard]] static std::optional<MathOperationRequestPtrs> scatter_request_span(
        std::span<const uint8_t> payload
    ) noexcept {
        auto* request = flatbuffers::GetMutableRoot<MathOperationRequest>(
            const_cast<uint8_t*>(payload.data()));
        if (!request || !request->operand_a() || !request->operand_b()) {
            return std::nullopt;
        }

        return MathOperationRequestPtrs{
            .a = const_cast<double*>(request->operand_a()->data()),
            .b = const_cast<double*>(request->operand_b()->data()),
            .operation = request->operation()
        };
    }

    /**
     * @brief Create a simple payload (one-off, no typed pointers needed).
     * @param a First operand.
     * @param b Second operand.
     * @param op Operation type (Add, Subtract, Multiply, Divide).
     * @return SimplePayload with buffer ownership.
     */
    [[nodiscard]] static SimplePayload create_payload(double a, double b, MathOperation op) {
        flatbuffers::FlatBufferBuilder builder(128);

        // Create single-element vectors
        double* a_ptr = nullptr;
        double* b_ptr = nullptr;

        auto b_offset = builder.CreateUninitializedVector(1, &b_ptr);
        auto a_offset = builder.CreateUninitializedVector(1, &a_ptr);

        *a_ptr = a;
        *b_ptr = b;

        auto request = CreateMathOperationRequest(builder, a_offset, b_offset, op);
        builder.Finish(request);

        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::MathOperation);
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
        
        // Create single-element vector for result
        double* result_ptr = nullptr;
        auto result_offset = builder.CreateUninitializedVector(1, &result_ptr);
        *result_ptr = result;
        
        auto response = CreateMathOperationResponse(builder, result_offset, overflow);
        builder.Finish(response);
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::MathOperation);
    }

    /**
     * @brief Extract response pointers for read-only access.
     * 
     * @param payload Raw FlatBuffer bytes.
     * @return Read-only response pointers, or nullopt on failure.
     */
    [[nodiscard]] static std::optional<MathOperationResponseViewPtrs> scatter_response_span(
        std::span<const uint8_t> payload
    ) noexcept {
        auto response = flatbuffers::GetRoot<MathOperationResponse>(payload.data());
        if (!response || !response->result()) {
            return std::nullopt;
        }

        return MathOperationResponseViewPtrs{
            .result = response->result()->data(),
            .overflow = nullptr  // Read via response->overflow() for bool
        };
    }

    /**
     * @brief Get overflow flag from parsed response.
     * 
     * @param payload Raw FlatBuffer bytes.
     * @return Overflow flag value.
     */
    [[nodiscard]] static bool get_response_overflow(std::span<const uint8_t> payload) noexcept {
        auto response = flatbuffers::GetRoot<MathOperationResponse>(payload.data());
        return response ? response->overflow() : false;
    }
};

} // namespace TaskMessenger::Skills
