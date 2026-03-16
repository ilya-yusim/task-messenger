/**
 * @file skills/builtins/StringReversalSkill.hpp
 * @brief StringReversal skill - reverses input string.
 *
 * Uses the unified Skill<Derived> pattern for string operations.
 */
#pragma once

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
 * Stores string view pointing directly into the FlatBuffer.
 */
struct StringReversalRequestPtrs {
    std::string_view input;  ///< View into the FlatBuffer's input string
};

/**
 * @brief Decoded pointers for StringReversal response.
 *
 * For strings we can't use direct pointers easily (null-terminated),
 * so we provide the buffer pointer and let compute() write directly.
 * original_length uses scalar-as-vector pattern.
 */
struct StringReversalResponsePtrs {
    char* output;             ///< Pointer to output buffer
    size_t output_length;     ///< Length of output (same as input)
    uint32_t* original_length; ///< Pointer to original_length (single-element vector)
};

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
        auto* request = flatbuffers::GetRoot<StringReversalRequest>(payload.data());
        if (!request || !request->input()) {
            return std::nullopt;
        }

        return RequestPtrs{
            .input = std::string_view(request->input()->c_str(), request->input()->size())
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
        if (!response || !response->mutable_output() || !response->original_length()) {
            return std::nullopt;
        }

        auto* output_str = response->mutable_output();
        auto* orig_len = response->mutable_original_length();
        if (orig_len->size() != 1) {
            return std::nullopt;
        }

        return ResponsePtrs{
            .output = const_cast<char*>(output_str->c_str()),
            .output_length = output_str->size(),
            .original_length = orig_len->data()
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
        // String reversal output is same length as input
        return std::make_unique<SimplePayload>(create_response(req_ptrs->input.size()));
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
        auto original_length = static_cast<uint32_t>(req.input.size());

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
    // Factory methods (used by manager for creating payloads)
    // =========================================================================

    /**
     * @brief Create a request payload.
     *
     * @param input The string to reverse.
     * @return SimplePayload with buffer ownership.
     */
    [[nodiscard]] static SimplePayload create_request(std::string_view input) {
        flatbuffers::FlatBufferBuilder builder(64 + input.size());

        auto input_offset = builder.CreateString(input);
        auto request = CreateStringReversalRequest(builder, input_offset);
        builder.Finish(request);

        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, kSkillId);
    }

    /**
     * @brief Create a response buffer with pre-allocated string.
     *
     * @param string_length Length of the output string.
     * @return SimplePayload with pre-allocated response buffer.
     */
    [[nodiscard]] static SimplePayload create_response(size_t string_length) {
        // Create placeholder string of correct length (will be overwritten)
        std::string placeholder(string_length, '\0');
        flatbuffers::FlatBufferBuilder builder(64 + string_length);
        
        // Create original_length as single-element vector
        uint32_t* orig_len_ptr = nullptr;
        auto orig_len_offset = builder.CreateUninitializedVector(1, &orig_len_ptr);
        *orig_len_ptr = 0;  // Will be set by compute()
        
        auto output_offset = builder.CreateString(placeholder);
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
        return std::string_view(response->output()->c_str(), response->output()->size());
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
};

} // namespace TaskMessenger::Skills
