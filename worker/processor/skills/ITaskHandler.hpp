/**
 * @file worker/processor/skills/ISkillHandler.hpp
 * @brief Interface for skill handlers using FlatBuffers serialization.
 *
 * Skills are typed task categories with request/response data structures.
 * Each skill handler processes a specific task_type and returns serialized responses.
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace TaskMessenger::Skills {

/**
 * @brief Interface for skill handlers.
 *
 * Implement this interface to create handlers for specific skill types.
 * The handler receives the inner payload from a TaskRequest and produces
 * the inner payload for a TaskResponse.
 */
class ISkillHandler {
public:
    virtual ~ISkillHandler() = default;

    /**
     * @brief Get the unique task type this handler processes.
     * @return The task_type value this handler is registered for.
     */
    [[nodiscard]] virtual uint32_t task_type() const noexcept = 0;

    /**
     * @brief Get a human-readable name for this skill.
     * @return Skill name for logging and diagnostics.
     */
    [[nodiscard]] virtual const char* skill_name() const noexcept = 0;

    /**
     * @brief Process a task request payload.
     *
     * @param payload The inner payload bytes from the TaskRequest.
     *                This is the skill-specific FlatBuffer data (e.g., StringReversalRequest).
     * @param response_out Output vector to receive the serialized response payload.
     *                     This should contain skill-specific response data (e.g., StringReversalResponse).
     * @return true if processing succeeded, false on error.
     */
    [[nodiscard]] virtual bool process(
        std::span<const uint8_t> payload,
        std::vector<uint8_t>& response_out
    ) = 0;
};

} // namespace TaskMessenger::Skills
