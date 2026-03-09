/**
 * @file skills/builtins/StringReversalPayload.hpp
 * @brief Payload factory for StringReversal skill.
 */
#pragma once

#include "skills/registry/IPayloadFactory.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/SkillIds.hpp"
#include "StringReversalSkill_generated.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace TaskMessenger::Skills {

/**
 * @brief Decoded StringReversal response.
 * 
 * Stores the output string view pointing into the FlatBuffer and original length.
 */
struct StringReversalDecodedResponse {
    std::string_view output;     ///< View into the FlatBuffer's output string
    uint32_t original_length;    ///< Length of the original input string
};

/**
 * @brief Decoded StringReversal request.
 * 
 * Stores the input string view pointing into the FlatBuffer.
 */
struct StringReversalDecodedRequest {
    std::string_view input;  ///< View into the FlatBuffer's input string
};

/**
 * @brief Payload factory for string reversal skill.
 *
 * Creates FlatBuffers payloads for StringReversalRequest.
 * Does not support typed buffer access (variable-length strings).
 */
class StringReversalPayloadFactory : public IPayloadFactory {
public:
    [[nodiscard]] uint32_t skill_id() const noexcept override {
        return SkillIds::StringReversal;
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
        // String reversal output is same length as input
        return std::make_unique<SimplePayload>(create_response_buffer(req_ptrs->input.size()));
    }

    /**
     * @brief Create a simple payload for string reversal.
     * @param input The string to reverse.
     * @return SimplePayload with buffer ownership.
     */
    [[nodiscard]] static SimplePayload create_payload(std::string_view input) {
        flatbuffers::FlatBufferBuilder builder(64 + input.size());
        
        auto input_offset = builder.CreateString(input);
        auto request = CreateStringReversalRequest(builder, input_offset);
        builder.Finish(request);
        
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::StringReversal);
    }

    /**
     * @brief Decode a request payload into a typed view.
     * 
     * @param payload Raw payload bytes from TaskMessage.
     * @return Decoded request with string view, or nullopt if validation fails.
     */
    [[nodiscard]] static std::optional<StringReversalDecodedRequest> scatter_request_span(
        std::span<const uint8_t> payload
    ) noexcept {
        auto request = flatbuffers::GetRoot<StringReversalRequest>(payload.data());
        if (!request || !request->input()) {
            return std::nullopt;
        }
        return StringReversalDecodedRequest{
            .input = std::string_view(request->input()->c_str(), request->input()->size())
        };
    }

    /**
     * @brief Create a response buffer with the reversed string.
     * 
     * Since strings are variable-length, we create the response directly.
     * 
     * @param output The reversed string.
     * @param original_length Length of the original input string.
     * @return SimplePayload with the response.
     */
    [[nodiscard]] static SimplePayload create_response_buffer(
        std::string_view output,
        uint32_t original_length
    ) {
        flatbuffers::FlatBufferBuilder builder(64 + output.size());
        auto output_offset = builder.CreateString(output);
        auto response = CreateStringReversalResponse(builder, output_offset, original_length);
        builder.Finish(response);
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::StringReversal);
    }

    /**
     * @brief Create a pre-allocated response buffer for string reversal.
     * 
     * Creates a response with a placeholder string of the specified length.
     * The handler will write the actual reversed string into the buffer.
     * 
     * @param string_length Length of the output string (same as input length).
     * @return SimplePayload with pre-allocated response buffer.
     */
    [[nodiscard]] static SimplePayload create_response_buffer(size_t string_length) {
        // Create placeholder string of correct length (will be overwritten by handler)
        std::string placeholder(string_length, '\0');
        flatbuffers::FlatBufferBuilder builder(64 + string_length);
        auto output_offset = builder.CreateString(placeholder);
        auto response = CreateStringReversalResponse(builder, output_offset, static_cast<uint32_t>(string_length));
        builder.Finish(response);
        return SimplePayload(builder.Release(), SimpleBufferPtrs{}, SkillIds::StringReversal);
    }

    /**
     * @brief Decode a StringReversal response payload.
     * 
     * Extracts the reversed string and original length from the buffer.
     * 
     * @param payload Raw payload bytes.
     * @return Decoded response with string view, or nullopt if validation fails.
     */
    [[nodiscard]] static std::optional<StringReversalDecodedResponse> scatter_response_span(
        std::span<const uint8_t> payload
    ) noexcept {
        auto response = flatbuffers::GetRoot<StringReversalResponse>(payload.data());
        if (!response || !response->output()) {
            return std::nullopt;
        }

        return StringReversalDecodedResponse{
            .output = std::string_view(response->output()->c_str(), response->output()->size()),
            .original_length = response->original_length()
        };
    }
};

} // namespace TaskMessenger::Skills
