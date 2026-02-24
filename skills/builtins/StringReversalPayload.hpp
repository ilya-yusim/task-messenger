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
    [[nodiscard]] static std::optional<StringReversalDecodedRequest> decode_request(
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
};

} // namespace TaskMessenger::Skills
