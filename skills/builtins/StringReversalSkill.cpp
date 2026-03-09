/**
 * @file skills/builtins/StringReversalSkill.cpp
 * @brief Self-contained string reversal skill with auto-registration.
 */

#include "skills/registry/SkillRegistration.hpp"
#include "skills/handlers/ISkillHandler.hpp"
#include "skills/registry/SkillIds.hpp"
#include "StringReversalPayload.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace TaskMessenger::Skills {
namespace {

/**
 * @brief Handler for string reversal skill.
 *
 * Receives a StringReversalRequest, reverses the input string,
 * and writes the result into a pre-allocated StringReversalResponse.
 */
class StringReversalHandler : public ISkillHandler {
public:
    [[nodiscard]] bool process(
        std::span<const uint8_t> request,
        std::span<uint8_t> response
    ) override {
        auto req_ptrs = StringReversalPayloadFactory::scatter_request_span(request);
        if (!req_ptrs) {
            return false;
        }

        auto original_length = static_cast<uint32_t>(req_ptrs->input.size());

        // Get mutable response
        auto* resp = flatbuffers::GetMutableRoot<StringReversalResponse>(response.data());
        if (!resp) {
            return false;
        }

        // Verify response buffer has correctly sized string
        auto* output_str = resp->mutable_output();
        if (!output_str || output_str->size() != original_length) {
            return false;
        }

        // Write reversed string directly into the pre-allocated buffer
        // Safe because: we own the buffer, sizes match, and FlatBuffers strings are just byte arrays
        char* output_data = const_cast<char*>(output_str->c_str());
        for (uint32_t i = 0; i < original_length; ++i) {
            output_data[i] = req_ptrs->input[original_length - 1 - i];
        }

        // Mutate the original_length field
        resp->mutate_original_length(original_length);

        return true;
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
