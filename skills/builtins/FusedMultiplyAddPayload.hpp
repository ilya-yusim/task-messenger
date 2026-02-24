/**
 * @file skills/builtins/FusedMultiplyAddPayload.hpp
 * @brief Payload factories for FusedMultiplyAdd skills.
 */
#pragma once

#include "skills/registry/IPayloadFactory.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/SkillIds.hpp"
#include "FusedMultiplyAddSkill_generated.h"

#include <cassert>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace TaskMessenger::Skills {

/**
 * @brief Buffer pointers for FusedMultiplyAdd request (templated on constness).
 * @tparam Const If true, provides read-only views; if false, provides mutable access.
 */
template<bool Const>
struct FusedMultiplyAddPtrs {
    using DoubleSpan = std::conditional_t<Const, std::span<const double>, std::span<double>>;
    using DoublePtr = std::conditional_t<Const, const double*, double*>;
    
    DoubleSpan a;   ///< First operand vector
    DoubleSpan b;   ///< Second operand vector
    DoublePtr c;    ///< Scalar multiplier (nullptr for Mutable variant on write side)
};

/// @brief Mutable buffer pointers for writing FusedMultiplyAdd request.
using FusedMultiplyAddBufferPtrs = FusedMultiplyAddPtrs<false>;

/// @brief Read-only view pointers for decoding FusedMultiplyAdd request.
using FusedMultiplyAddViewPtrs = FusedMultiplyAddPtrs<true>;

/**
 * @brief Decoded FusedMultiplyAdd request with scalar storage.
 * 
 * For the Mutable variant, the scalar is stored by value and the pointer
 * points to the stored value. This struct must outlive any use of its pointers.
 */
struct FusedMultiplyAddDecodedRequest {
    std::span<const double> a;  ///< First operand vector (view into buffer)
    std::span<const double> b;  ///< Second operand vector (view into buffer)
    double c_storage;           ///< Scalar storage (copied from buffer)
    const double* c = &c_storage;  ///< Pointer to scalar (always valid)
    
    /// @brief Get view pointers (for uniform access pattern).
    [[nodiscard]] FusedMultiplyAddViewPtrs ptrs() const noexcept {
        return FusedMultiplyAddViewPtrs{.a = a, .b = b, .c = c};
    }
};

/// @brief Typed payload buffer for FusedMultiplyAdd request.
using FusedMultiplyAddPayload = PayloadBuffer<FusedMultiplyAddBufferPtrs>;

/**
 * @brief Buffer pointers for FusedMultiplyAdd response (templated on constness).
 * @tparam Const If true, provides read-only views; if false, provides mutable access.
 */
template<bool Const>
struct FusedMultiplyAddResponsePtrsT {
    using DoubleSpan = std::conditional_t<Const, std::span<const double>, std::span<double>>;
    
    DoubleSpan result;  ///< Result vector
};

/// @brief Mutable buffer pointers for writing FusedMultiplyAdd response.
using FusedMultiplyAddResponsePtrs = FusedMultiplyAddResponsePtrsT<false>;

/// @brief Read-only view pointers for decoding FusedMultiplyAdd response.
using FusedMultiplyAddResponseViewPtrs = FusedMultiplyAddResponsePtrsT<true>;

/// @brief Typed payload buffer for FusedMultiplyAdd response.
using FusedMultiplyAddResponseBuffer = PayloadBuffer<FusedMultiplyAddResponsePtrs>;

/**
 * @brief Payload factory for FusedMultiplyAdd (scalar-as-vector pattern).
 *
 * Creates FlatBuffers payloads for FusedMultiplyAddRequest.
 * Computes: result[i] = a[i] + c * b[i]
 */
class FusedMultiplyAddPayloadFactory : public IPayloadFactory {
public:
    [[nodiscard]] uint32_t skill_id() const noexcept override {
        return SkillIds::FusedMultiplyAdd;
    }

    /**
     * @brief Create a payload buffer with typed data access.
     * 
     * Returns a FusedMultiplyAddPayload combining ownership with spans/pointers
     * directly into the FlatBuffer memory for zero-copy data population.
     * 
     * @param vector_size Size of both operand vectors.
     * @param c Initial scalar multiplier value.
     * @return FusedMultiplyAddPayload with ownership and typed pointers.
     */
    [[nodiscard]] static FusedMultiplyAddPayload create_payload_buffer(
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
        
        FusedMultiplyAddBufferPtrs ptrs{
            .a = std::span<double>(ptr_a, vector_size),
            .b = std::span<double>(ptr_b, vector_size),
            .c = ptr_c
        };
        
        return FusedMultiplyAddPayload(builder.Release(), ptrs, SkillIds::FusedMultiplyAdd);
    }

    /**
     * @brief Create a simple payload (one-off, no typed pointers needed).
     * @param a First operand vector.
     * @param b Second operand vector.
     * @param c Scalar multiplier.
     * @return SimplePayload with buffer ownership.
     */
    [[nodiscard]] static SimplePayload create_payload(
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
        
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::FusedMultiplyAdd);
    }

    /**
     * @brief Create a response buffer with typed data access.
     * 
     * Returns a FusedMultiplyAddResponseBuffer combining ownership with span pointing
     * directly into the FlatBuffer memory for zero-copy result writing.
     * 
     * @param vector_size Size of the result vector.
     * @return FusedMultiplyAddResponseBuffer with ownership and typed pointer.
     */
    [[nodiscard]] static FusedMultiplyAddResponseBuffer create_response_buffer(size_t vector_size) {
        flatbuffers::FlatBufferBuilder builder(64 + vector_size * sizeof(double));
        
        double* result_ptr = nullptr;
        auto result_offset = builder.CreateUninitializedVector(vector_size, &result_ptr);
        auto response = CreateFusedMultiplyAddResponse(builder, result_offset);
        builder.Finish(response);
        
        FusedMultiplyAddResponsePtrs ptrs{
            .result = std::span<double>(result_ptr, vector_size)
        };
        
        return FusedMultiplyAddResponseBuffer(builder.Release(), ptrs, SkillIds::FusedMultiplyAdd);
    }

    /**
     * @brief Decode a FusedMultiplyAdd request payload into typed view pointers.
     * 
     * Validates the payload and returns read-only view pointers into the buffer.
     * 
     * @param payload Raw FlatBuffer bytes.
     * @return Decoded request on success, nullopt on validation failure.
     */
    [[nodiscard]] static std::optional<FusedMultiplyAddDecodedRequest> decode_request(
        std::span<const uint8_t> payload
    ) {
        auto request = flatbuffers::GetRoot<FusedMultiplyAddRequest>(payload.data());
        if (!request || !request->operand_a() || !request->operand_b() || !request->scalar_c()) {
            return std::nullopt;
        }

        auto vec_a = request->operand_a();
        auto vec_b = request->operand_b();
        auto scalar_c_vec = request->scalar_c();

        // Vectors must have the same size
        if (vec_a->size() != vec_b->size()) {
            return std::nullopt;
        }

        // scalar_c should be a single-element vector
        if (scalar_c_vec->size() != 1) {
            return std::nullopt;
        }

        FusedMultiplyAddDecodedRequest result{
            .a = std::span<const double>(vec_a->data(), vec_a->size()),
            .b = std::span<const double>(vec_b->data(), vec_b->size()),
            .c_storage = scalar_c_vec->Get(0)
        };
        result.c = &result.c_storage;
        return result;
    }
};

/**
 * @brief Payload factory for FusedMultiplyAddMutable (true scalar pattern).
 *
 * Creates FlatBuffers payloads for FusedMultiplyAddMutableRequest.
 * Computes: result[i] = a[i] + c * b[i]
 * Uses --gen-mutable for scalar field mutation via mutate_scalar_c().
 */
class FusedMultiplyAddMutablePayloadFactory : public IPayloadFactory {
public:
    [[nodiscard]] uint32_t skill_id() const noexcept override {
        return SkillIds::FusedMultiplyAddMutable;
    }

    /**
     * @brief Create a payload buffer with typed data access.
     * 
     * Returns a FusedMultiplyAddPayload combining ownership with spans
     * directly into the FlatBuffer memory. Use get_mutable_request() and
     * mutate_scalar_c() to change the scalar value.
     * 
     * @param vector_size Size of both operand vectors.
     * @param c Initial scalar multiplier value.
     * @return FusedMultiplyAddPayload with ownership and typed pointers (c is nullptr).
     */
    [[nodiscard]] static FusedMultiplyAddPayload create_payload_buffer(
        size_t vector_size, 
        double c = 0.0
    ) {
        flatbuffers::FlatBufferBuilder builder(64 + vector_size * 2 * sizeof(double));
        
        double* ptr_a = nullptr;
        double* ptr_b = nullptr;
        
        auto vec_a = builder.CreateUninitializedVector(vector_size, &ptr_a);
        auto vec_b = builder.CreateUninitializedVector(vector_size, &ptr_b);
        
        auto request = CreateFusedMultiplyAddMutableRequest(builder, vec_a, vec_b, c);
        builder.Finish(request);
        
        FusedMultiplyAddBufferPtrs ptrs{
            .a = std::span<double>(ptr_a, vector_size),
            .b = std::span<double>(ptr_b, vector_size),
            .c = nullptr  // Use get_mutable_request()->mutate_scalar_c()
        };
        
        return FusedMultiplyAddPayload(builder.Release(), ptrs, SkillIds::FusedMultiplyAddMutable);
    }

    /**
     * @brief Get mutable access to the request for changing scalar_c.
     * 
     * @param payload The payload to get mutable access to.
     * @return Mutable pointer to the FusedMultiplyAddMutableRequest.
     */
    [[nodiscard]] static FusedMultiplyAddMutableRequest* get_mutable_request(
        FusedMultiplyAddPayload& payload
    ) noexcept {
        return flatbuffers::GetMutableRoot<FusedMultiplyAddMutableRequest>(payload.mutable_data());
    }

    /**
     * @brief Create a simple payload (one-off, no typed pointers needed).
     * @param a First operand vector.
     * @param b Second operand vector.
     * @param c Scalar multiplier.
     * @return SimplePayload with buffer ownership.
     */
    [[nodiscard]] static SimplePayload create_payload(
        const std::vector<double>& a,
        const std::vector<double>& b,
        double c
    ) {
        flatbuffers::FlatBufferBuilder builder(64 + (a.size() + b.size()) * sizeof(double));
        
        auto vec_a = builder.CreateVector(a);
        auto vec_b = builder.CreateVector(b);
        
        auto request = CreateFusedMultiplyAddMutableRequest(builder, vec_a, vec_b, c);
        builder.Finish(request);
        
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::FusedMultiplyAddMutable);
    }

    /**
     * @brief Create a response buffer with typed data access.
     * 
     * Returns a FusedMultiplyAddResponseBuffer combining ownership with span pointing
     * directly into the FlatBuffer memory for zero-copy result writing.
     * Note: Both FusedMultiplyAdd and FusedMultiplyAddMutable use the same response schema.
     * 
     * @param vector_size Size of the result vector.
     * @return FusedMultiplyAddResponseBuffer with ownership and typed pointer.
     */
    [[nodiscard]] static FusedMultiplyAddResponseBuffer create_response_buffer(size_t vector_size) {
        return FusedMultiplyAddPayloadFactory::create_response_buffer(vector_size);
    }

    /**
     * @brief Decode a FusedMultiplyAddMutable request payload into typed view pointers.
     * 
     * Validates the payload and returns read-only view pointers. The scalar value
     * is copied into the returned struct (no pointer into buffer for true scalars).
     * 
     * @param payload Raw FlatBuffer bytes.
     * @return Decoded request on success, nullopt on validation failure.
     */
    [[nodiscard]] static std::optional<FusedMultiplyAddDecodedRequest> decode_request(
        std::span<const uint8_t> payload
    ) {
        auto request = flatbuffers::GetRoot<FusedMultiplyAddMutableRequest>(payload.data());
        if (!request || !request->operand_a() || !request->operand_b()) {
            return std::nullopt;
        }

        auto vec_a = request->operand_a();
        auto vec_b = request->operand_b();

        // Vectors must have the same size
        if (vec_a->size() != vec_b->size()) {
            return std::nullopt;
        }

        FusedMultiplyAddDecodedRequest result{
            .a = std::span<const double>(vec_a->data(), vec_a->size()),
            .b = std::span<const double>(vec_b->data(), vec_b->size()),
            .c_storage = request->scalar_c()
        };
        result.c = &result.c_storage;
        return result;
    }
};

} // namespace TaskMessenger::Skills
