/**
 * @file skills/builtins/VectorMathPayload.hpp
 * @brief Payload factory for VectorMath skill.
 */
#pragma once

#include "skills/registry/IPayloadFactory.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/SkillIds.hpp"
#include "VectorMathSkill_generated.h"

#include <cassert>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace TaskMessenger::Skills {

/**
 * @brief Buffer pointers for VectorMath request (templated on constness).
 * @tparam Const If true, provides read-only views; if false, provides mutable access.
 */
template<bool Const>
struct VectorMathPtrs {
    using DoubleSpan = std::conditional_t<Const, std::span<const double>, std::span<double>>;
    
    DoubleSpan a;           ///< First operand vector
    DoubleSpan b;           ///< Second operand vector
    MathOperation operation;  ///< Operation type (same for read/write)
};

/// @brief Mutable buffer pointers for writing VectorMath request.
using VectorMathBufferPtrs = VectorMathPtrs<false>;

/// @brief Read-only view pointers for decoding VectorMath request.
using VectorMathViewPtrs = VectorMathPtrs<true>;

/// @brief Typed payload buffer for VectorMath request.
using VectorMathPayload = PayloadBuffer<VectorMathBufferPtrs>;

/**
 * @brief Buffer pointers for VectorMath response (templated on constness).
 * @tparam Const If true, provides read-only views; if false, provides mutable access.
 */
template<bool Const>
struct VectorMathResponsePtrsT {
    using DoubleSpan = std::conditional_t<Const, std::span<const double>, std::span<double>>;
    
    DoubleSpan result;  ///< Result vector
};

/// @brief Mutable buffer pointers for writing VectorMath response.
using VectorMathResponsePtrs = VectorMathResponsePtrsT<false>;

/// @brief Read-only view pointers for decoding VectorMath response.
using VectorMathResponseViewPtrs = VectorMathResponsePtrsT<true>;

/// @brief Typed payload buffer for VectorMath response.
using VectorMathResponseBuffer = PayloadBuffer<VectorMathResponsePtrs>;

/**
 * @brief Payload factory for element-wise vector math operations.
 *
 * Creates FlatBuffers payloads for VectorMathRequest.
 * Supports both typed buffer creation and simple one-off creation.
 */
class VectorMathPayloadFactory : public IPayloadFactory {
public:
    [[nodiscard]] uint32_t skill_id() const noexcept override {
        return SkillIds::VectorMath;
    }

    /**
     * @brief Create a payload buffer with typed data access.
     * 
     * Returns a VectorMathPayload combining ownership with spans pointing
     * directly into the FlatBuffer memory for zero-copy data population.
     * 
     * @param vector_size Size of both operand vectors.
     * @param op Initial operation type.
     * @return VectorMathPayload with ownership and typed pointers.
     */
    [[nodiscard]] static VectorMathPayload create_payload_buffer(
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
        
        VectorMathBufferPtrs ptrs{
            .a = std::span<double>(final_ptr_a, vector_size),
            .b = std::span<double>(final_ptr_b, vector_size),
            .operation = op
        };
        
        return VectorMathPayload(std::move(detached), ptrs, SkillIds::VectorMath);
    }

    /**
     * @brief Create a simple payload (one-off, no typed pointers needed).
     * @param a First operand vector.
     * @param b Second operand vector.
     * @param op Operation type (Add, Subtract, Multiply, Divide).
     * @return SimplePayload with buffer ownership.
     */
    [[nodiscard]] static SimplePayload create_payload(
        const std::vector<double>& a,
        const std::vector<double>& b,
        MathOperation op
    ) {
        flatbuffers::FlatBufferBuilder builder(64 + (a.size() + b.size()) * sizeof(double));
        
        auto vec_a = builder.CreateVector(a);
        auto vec_b = builder.CreateVector(b);
        auto request = CreateVectorMathRequest(builder, vec_a, vec_b, op);
        builder.Finish(request);
        
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::VectorMath);
    }

    /**
     * @brief Get mutable access to the request for changing operation type.
     * 
     * Use on a VectorMathPayload's buffer with --gen-mutable generated methods.
     * 
     * @param payload The payload to get mutable access to.
     * @return Mutable pointer to the VectorMathRequest.
     */
    [[nodiscard]] static VectorMathRequest* get_mutable_request(VectorMathPayload& payload) noexcept {
        return flatbuffers::GetMutableRoot<VectorMathRequest>(payload.mutable_data());
    }

    /**
     * @brief Create a response buffer with typed data access.
     * 
     * Returns a VectorMathResponseBuffer combining ownership with span pointing
     * directly into the FlatBuffer memory for zero-copy result writing.
     * 
     * @param vector_size Size of the result vector.
     * @return VectorMathResponseBuffer with ownership and typed pointer.
     */
    [[nodiscard]] static VectorMathResponseBuffer create_response_buffer(size_t vector_size) {
        flatbuffers::FlatBufferBuilder builder(64 + vector_size * sizeof(double));
        
        double* result_ptr = nullptr;
        auto result_offset = builder.CreateUninitializedVector(vector_size, &result_ptr);
        auto response = CreateVectorMathResponse(builder, result_offset);
        builder.Finish(response);
        
        auto detached = builder.Release();
        
        // Extract pointer from the FINISHED buffer by parsing it
        auto* resp = flatbuffers::GetMutableRoot<VectorMathResponse>(detached.data());
        double* final_result_ptr = const_cast<double*>(resp->result()->data());
        
        VectorMathResponsePtrs ptrs{
            .result = std::span<double>(final_result_ptr, vector_size)
        };
        
        return VectorMathResponseBuffer(std::move(detached), ptrs, SkillIds::VectorMath);
    }

    /**
     * @brief Decode a VectorMath request payload into typed view pointers.
     * 
     * Validates the payload and returns read-only view pointers into the buffer.
     * 
     * @param payload Raw FlatBuffer bytes.
     * @return View pointers on success, nullopt on validation failure.
     */
    [[nodiscard]] static std::optional<VectorMathViewPtrs> decode_request(
        std::span<const uint8_t> payload
    ) {
        auto request = flatbuffers::GetRoot<VectorMathRequest>(payload.data());
        if (!request || !request->operand_a() || !request->operand_b()) {
            return std::nullopt;
        }

        auto vec_a = request->operand_a();
        auto vec_b = request->operand_b();

        // Vectors must have the same size
        if (vec_a->size() != vec_b->size()) {
            return std::nullopt;
        }

        return VectorMathViewPtrs{
            .a = std::span<const double>(vec_a->data(), vec_a->size()),
            .b = std::span<const double>(vec_b->data(), vec_b->size()),
            .operation = request->operation()
        };
    }
};

} // namespace TaskMessenger::Skills
