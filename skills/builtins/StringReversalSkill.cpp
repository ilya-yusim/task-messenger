/**
 * @file skills/builtins/StringReversalSkill.cpp
 * @brief Self-contained string reversal skill with auto-registration.
 */

#include "skills/registry/SkillRegistration.hpp"
#include "skills/handlers/ISkillHandler.hpp"
#include "skills/registry/SkillIds.hpp"
#include "StringReversalPayload.hpp"

#include <algorithm>
#include <string>

namespace TaskMessenger::Skills {
namespace {

/**
 * @brief Handler for string reversal skill.
 *
 * Receives a StringReversalRequest, reverses the input string,
 * and returns a StringReversalResponse.
 */
class StringReversalHandler : public ISkillHandler {
public:
    [[nodiscard]] uint32_t skill_id() const noexcept override { 
        return SkillIds::StringReversal; 
    }
    
    [[nodiscard]] const char* skill_name() const noexcept override { 
        return "StringReversal"; 
    }

    [[nodiscard]] std::unique_ptr<PayloadBufferBase> process(
        std::span<const uint8_t> payload
    ) override {
        auto decoded = StringReversalPayloadFactory::decode_request(payload);
        if (!decoded) {
            return nullptr;
        }

        // Reverse the string
        std::string output(decoded->input.rbegin(), decoded->input.rend());
        auto original_length = static_cast<uint32_t>(decoded->input.size());

        // Create response
        auto response = StringReversalPayloadFactory::create_response_buffer(output, original_length);
        return std::make_unique<SimplePayload>(std::move(response));
    }
};

// Self-registration: runs before main()
REGISTER_SKILL(
    SkillIds::StringReversal,
    "StringReversal",
    "Reverses the input string",
    std::make_unique<StringReversalHandler>(),
    std::make_unique<StringReversalPayloadFactory>(),
    1, 256, 256
);

} // anonymous namespace
} // namespace TaskMessenger::Skills
