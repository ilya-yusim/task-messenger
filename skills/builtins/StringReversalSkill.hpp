/**
 * @file skills/builtins/StringReversalSkill.hpp
 * @brief StringReversal skill - reverses input string.
 *
 * Uses the unified Skill<Derived> pattern for string operations.
 */
#pragma once

#include "skills/registry/CompareUtils.hpp"
#include "skills/registry/Skill.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/SkillIds.hpp"
#include "StringReversalSkill_generated.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace TaskMessenger::Skills {

// Forward declaration
class StringReversalSkill;

/**
 * @brief Decoded pointers for StringReversal request.
 *
 * Uses uniform pointer-based access pattern.
 */
struct StringReversalRequestPtrs {
    int8_t* input;       ///< Pointer to input bytes (stored as [int8])
    size_t input_length; ///< Length of input
};

/**
 * @brief Decoded pointers for StringReversal response.
 *
 * All fields use uniform pointer-based access.
 * original_length uses scalar-as-vector pattern.
 */
struct StringReversalResponsePtrs {
    int8_t* output;           ///< Pointer to output buffer (stored as [int8])
    size_t output_length;     ///< Length of output (same as input)
    uint32_t* original_length; ///< Pointer to original_length (single-element vector)
};

/// @brief Typed payload buffer for StringReversal request.
using StringReversalPayload = PayloadBuffer<StringReversalRequestPtrs>;

/**
 * @brief StringReversal skill implementation.
 *
 * Reverses the input string.
 *
 * This class combines the handler and payload factory into a single unit.
 * Skill developers only implement compute() - base class handles scatter logic.
 */
class StringReversalSkill : public Skill<StringReversalSkill> {
public:
    // =========================================================================
    // Required type aliases for Skill<Derived>
    // =========================================================================
    using RequestPtrs = StringReversalRequestPtrs;
    using ResponsePtrs = StringReversalResponsePtrs;
    
    /// Skill identifier
    static constexpr uint32_t kSkillId = SkillIds::StringReversal;

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
        auto* request = flatbuffers::GetMutableRoot<StringReversalRequest>(
            const_cast<uint8_t*>(payload.data()));
        if (!request || !request->input()) {
            return std::nullopt;
        }

        auto* input_vec = request->mutable_input();
        return RequestPtrs{
            .input = input_vec->data(),
            .input_length = input_vec->size()
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
        auto* response = flatbuffers::GetMutableRoot<StringReversalResponse>(payload.data());
        if (!response || !response->output() || !response->original_length()) {
            return std::nullopt;
        }

        auto* output_vec = response->mutable_output();
        auto* orig_len = response->mutable_original_length();
        if (orig_len->size() != 1) {
            return std::nullopt;
        }

        return ResponsePtrs{
            .output = output_vec->data(),
            .output_length = output_vec->size(),
            .original_length = orig_len->data()
        };
    }

    /**
     * @brief Create a test request with predefined test data.
     *
     * @param case_index Test case selection:
     *   - 0: "Hello, World!" (13 chars)
     *   - 1: Long string (500 chars)
     *   - 2: Single char "X"
     * @return StringReversalPayload populated with test data.
     */
    [[nodiscard]] static StringReversalPayload create_test_request(size_t case_index = 0) {
        std::string_view test_strings[] = {
            "Hello, World!",                              // Case 0
            std::string_view{},                           // Case 1 - placeholder for long string
            "X"                                           // Case 2
        };
        
        // Generate long string for case 1
        static const std::string long_str = []() {
            std::string s;
            s.reserve(500);
            for (int i = 0; i < 500; ++i) {
                s.push_back('A' + (i % 26));
            }
            return s;
        }();
        
        std::string_view data;
        if (case_index == 1) {
            data = long_str;
        } else if (case_index < std::size(test_strings)) {
            data = test_strings[case_index];
        } else {
            data = test_strings[0];
        }
        
        auto payload = create_request(data.size());
        auto& ptrs = payload.ptrs();
        
        // Copy test string data
        for (size_t i = 0; i < data.size(); ++i) {
            ptrs.input[i] = static_cast<int8_t>(data[i]);
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
     * @return Unique pointer to response buffer, or nullptr on failure.
     */
    [[nodiscard]] static std::unique_ptr<PayloadBufferBase> create_response_for_request(
        std::span<const uint8_t> request
    ) {
        auto req_ptrs = scatter_request(request);
        if (!req_ptrs) {
            return nullptr;
        }
        // String reversal output is same length as input
        return std::make_unique<SimplePayload>(create_response(req_ptrs->input_length));
    }

    /**
     * @brief Create response buffer sized for the given request (PayloadBufferBase overload).
     * @param request The request payload to size the response for.
     * @return Unique pointer to response buffer, or nullptr on failure.
     */
    [[nodiscard]] static std::unique_ptr<PayloadBufferBase> create_response_for_request(
        const PayloadBufferBase& request
    ) {
        return create_response_for_request(request.span());
    }

    // =========================================================================
    // Compute method (the only interesting part for skill developers!)
    // =========================================================================

    /**
     * @brief Reverse the input string.
     *
     * @param req Request pointers (input string).
     * @param resp Response pointers (output buffer and original_length).
     * @return true on success, false on error.
     */
    bool compute(const RequestPtrs& req, ResponsePtrs& resp) {
        auto original_length = static_cast<uint32_t>(req.input_length);

        if (resp.output_length != original_length) {
            return false;  // Size mismatch
        }

        // Reverse the string directly into the output buffer
        for (uint32_t i = 0; i < original_length; ++i) {
            resp.output[i] = req.input[original_length - 1 - i];
        }

        // Write original_length via pointer - no FlatBuffer dependency!
        *resp.original_length = original_length;

        return true;
    }

    // =========================================================================
    // Factory methods (used by dispatcher for creating payloads)
    // =========================================================================

    /**
     * @brief Create a request buffer with typed data access.
     *
     * Allocates uninitialized buffer. Caller fills values via ptrs().
     *
     * @param string_length Length of the input string.
     * @return StringReversalPayload with ownership and typed pointers.
     */
    [[nodiscard]] static StringReversalPayload create_request(size_t string_length) {
        flatbuffers::FlatBufferBuilder builder(64 + string_length);

        int8_t* input_ptr = nullptr;
        auto input_offset = builder.CreateUninitializedVector(string_length, &input_ptr);
        auto request = CreateStringReversalRequest(builder, input_offset);
        builder.Finish(request);

        auto detached = builder.Release();

        // Extract pointer from the FINISHED buffer by parsing it
        auto* req = flatbuffers::GetMutableRoot<StringReversalRequest>(detached.data());
        int8_t* final_input_ptr = const_cast<int8_t*>(req->input()->data());

        RequestPtrs ptrs{
            .input = final_input_ptr,
            .input_length = string_length
        };

        return StringReversalPayload(std::move(detached), ptrs, kSkillId);
    }

    /**
     * @brief Create a response buffer with pre-allocated output vector.
     *
     * @param string_length Length of the output string.
     * @return SimplePayload with pre-allocated response buffer.
     */
    [[nodiscard]] static SimplePayload create_response(size_t string_length) {
        flatbuffers::FlatBufferBuilder builder(64 + string_length);
        
        // Create original_length as single-element vector
        uint32_t* orig_len_ptr = nullptr;
        auto orig_len_offset = builder.CreateUninitializedVector(1, &orig_len_ptr);
        *orig_len_ptr = 0;  // Will be set by compute()
        
        // Create output as int8 vector (will be overwritten by compute)
        int8_t* output_ptr = nullptr;
        auto output_offset = builder.CreateUninitializedVector(string_length, &output_ptr);
        
        auto response = CreateStringReversalResponse(builder, output_offset, orig_len_offset);
        builder.Finish(response);
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, kSkillId);
    }

    /**
     * @brief Decode response from payload.
     * @param payload Raw FlatBuffer bytes.
     * @return Output string view, or nullopt on failure.
     */
    [[nodiscard]] static std::optional<std::string_view> get_output(
        std::span<const uint8_t> payload
    ) noexcept {
        auto response = flatbuffers::GetRoot<StringReversalResponse>(payload.data());
        if (!response || !response->output()) {
            return std::nullopt;
        }
        // int8 vector can be reinterpreted as char*
        return std::string_view(
            reinterpret_cast<const char*>(response->output()->data()),
            response->output()->size()
        );
    }

    /**
     * @brief Get original length from response.
     * @param payload Raw FlatBuffer bytes.
     * @return Original length, or 0 on failure.
     */
    [[nodiscard]] static uint32_t get_original_length(
        std::span<const uint8_t> payload
    ) noexcept {
        auto response = flatbuffers::GetRoot<StringReversalResponse>(payload.data());
        if (!response || !response->original_length() || response->original_length()->size() < 1) {
            return 0;
        }
        return response->original_length()->Get(0);
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
        // Check original_length
        if (auto r = compare_int(*computed.original_length, *worker.original_length, "original_length"); !r.passed) {
            return r;
        }
        // Check output_length
        if (auto r = compare_int(computed.output_length, worker.output_length, "output_length"); !r.passed) {
            return r;
        }
        // Compare output bytes
        return compare_bytes(computed.output, worker.output, computed.output_length, "output");
    }
};

} // namespace TaskMessenger::Skills
