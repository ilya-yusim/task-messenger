/**
 * @file skills/handlers/ISkillHandler.hpp
 * @brief Interface for skill handlers using FlatBuffers serialization.
 *
 * Skills are typed task categories with request/response data structures.
 * Each skill handler processes a specific skill_id and returns serialized responses.
 */
#pragma once

#include "skills/registry/PayloadBuffer.hpp"

#include <cstdint>
#include <memory>
#include <span>

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
     * @brief Get the unique skill ID this handler processes.
     * @return The skill_id value this handler is registered for.
     */
    [[nodiscard]] virtual uint32_t skill_id() const noexcept = 0;

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
     * @return Unique pointer to response payload, or nullptr on error.
     *         The response contains skill-specific data (e.g., StringReversalResponse).
     */
    [[nodiscard]] virtual std::unique_ptr<PayloadBufferBase> process(
        std::span<const uint8_t> payload
    ) = 0;
};

} // namespace TaskMessenger::Skills
