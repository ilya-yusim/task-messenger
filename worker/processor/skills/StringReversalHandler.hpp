/**
 * @file worker/processor/skills/StringReversalHandler.hpp
 * @brief Handler for the string reversal skill (skill_id = 1).
 */
#pragma once

#include "ISkillHandler.hpp"
#include "skill_task_generated.h"

#include <algorithm>
#include <string>

namespace TaskMessenger::Skills {

/**
 * @brief Handler for string reversal skill.
 *
 * Receives a StringReversalRequest, reverses the input string,
 * and returns a StringReversalResponse.
 */
class StringReversalHandler : public ISkillHandler {
public:
    static constexpr uint32_t kSkillId = 1;

    [[nodiscard]] uint32_t skill_id() const noexcept override { return kSkillId; }
    [[nodiscard]] const char* skill_name() const noexcept override { return "StringReversal"; }

    [[nodiscard]] bool process(
        std::span<const uint8_t> payload,
        std::vector<uint8_t>& response_out
    ) override {
        // Parse the inner StringReversalRequest
        auto request = flatbuffers::GetRoot<StringReversalRequest>(payload.data());
        if (!request || !request->input()) {
            return false;
        }

        // Reverse the string
        std::string input = request->input()->str();
        std::string output(input.rbegin(), input.rend());

        // Build response
        flatbuffers::FlatBufferBuilder builder(256);
        auto output_offset = builder.CreateString(output);
        auto response = CreateStringReversalResponse(
            builder,
            output_offset,
            static_cast<uint32_t>(input.length())
        );
        builder.Finish(response);

        // Copy to output vector
        response_out.assign(
            builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()
        );

        return true;
    }
};

} // namespace TaskMessenger::Skills
