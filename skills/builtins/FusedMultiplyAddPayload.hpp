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
#include <vector>

namespace TaskMessenger::Skills {

/**
 * @brief Buffer pointers for FusedMultiplyAdd request.
 * 
 * Provides direct pointer access into FlatBuffer memory for both read and write.
 * Uses scalar-as-vector pattern: scalar c is stored as single-element vector
 * for uniform pointer-based access.
 */
struct FusedMultiplyAddRequestPtrs {
    std::span<double> a;   ///< First operand vector
    std::span<double> b;   ///< Second operand vector
    double* c;             ///< Scalar multiplier (single-element vector)
};

/// @brief Typed payload buffer for FusedMultiplyAdd request.
using FusedMultiplyAddPayload = PayloadBuffer<FusedMultiplyAddRequestPtrs>;

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
     * @brief Create a pre-allocated response buffer sized for a given request.
     */
    [[nodiscard]] std::unique_ptr<PayloadBufferBase> create_response_buffer_for_request(
        std::span<const uint8_t> request
    ) const override {
        auto req_ptrs = scatter_request_span(request);
        if (!req_ptrs) {
            return nullptr;
        }
        return std::make_unique<FusedMultiplyAddResponseBuffer>(
            create_response_buffer(req_ptrs->a.size()));
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
        
        auto detached = builder.Release();
        
        // Extract pointers from the FINISHED buffer by parsing it
        // This is the only safe way since FlatBuffers builds backwards
        auto* req = flatbuffers::GetMutableRoot<FusedMultiplyAddRequest>(detached.data());
        double* final_ptr_a = const_cast<double*>(req->operand_a()->data());
        double* final_ptr_b = const_cast<double*>(req->operand_b()->data());
        double* final_ptr_c = const_cast<double*>(req->scalar_c()->data());
        
        FusedMultiplyAddRequestPtrs ptrs{
            .a = std::span<double>(final_ptr_a, vector_size),
            .b = std::span<double>(final_ptr_b, vector_size),
            .c = final_ptr_c
        };
        
        return FusedMultiplyAddPayload(std::move(detached), ptrs, SkillIds::FusedMultiplyAdd);
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
        
        auto detached = builder.Release();
        
        // Extract pointer from the FINISHED buffer by parsing it
        auto* resp = flatbuffers::GetMutableRoot<FusedMultiplyAddResponse>(detached.data());
        double* final_result_ptr = const_cast<double*>(resp->result()->data());
        
        FusedMultiplyAddResponsePtrs ptrs{
            .result = std::span<double>(final_result_ptr, vector_size)
        };
        
        return FusedMultiplyAddResponseBuffer(std::move(detached), ptrs, SkillIds::FusedMultiplyAdd);
    }

    /**
     * @brief Decode a FusedMultiplyAdd request payload into typed pointers.
     * 
     * Validates the payload and returns pointers directly into the FlatBuffer memory.
     * 
     * @param payload Raw FlatBuffer bytes.
     * @return Request pointers on success, nullopt on validation failure.
     */
    [[nodiscard]] static std::optional<FusedMultiplyAddRequestPtrs> scatter_request_span(
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

        return FusedMultiplyAddRequestPtrs{
            .a = std::span<double>(vec_a->data(), vec_a->size()),
            .b = std::span<double>(vec_b->data(), vec_b->size()),
            .c = scalar_c_vec->data()
        };
    }

    /**
     * @brief Decode a FusedMultiplyAdd response payload into typed view pointers.
     * 
     * Validates the payload and returns view pointers into the buffer.
     * 
     * @tparam Mutable If true, returns writable spans; if false, read-only.
     * @param payload Raw FlatBuffer bytes.
     * @return View pointers on success, nullopt on validation failure.
     */
    template<bool Mutable = false>
    [[nodiscard]] static auto scatter_response_span(
        std::conditional_t<Mutable, std::span<uint8_t>, std::span<const uint8_t>> payload
    ) -> std::optional<FusedMultiplyAddResponsePtrsT<not Mutable>> {
        auto* response = flatbuffers::GetMutableRoot<FusedMultiplyAddResponse>(
            const_cast<uint8_t*>(payload.data()));
        if (!response || !response->result()) {
            return std::nullopt;
        }

        auto* result = response->mutable_result();
        if constexpr (Mutable) {
            return FusedMultiplyAddResponsePtrs{
                .result = std::span<double>(result->data(), result->size())
            };
        } else {
            return FusedMultiplyAddResponseViewPtrs{
                .result = std::span<const double>(result->data(), result->size())
            };
        }
    }
};

} // namespace TaskMessenger::Skills
