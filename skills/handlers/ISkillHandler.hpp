/**
 * @file skills/handlers/ISkillHandler.hpp
 * @brief Interface for skill handlers using FlatBuffers serialization.
 *
 * Skills are typed task categories with request/response data structures.
 * Each skill handler processes a specific skill_id and returns serialized responses.
 */
#pragma once

#include <cstdint>
#include <span>

namespace TaskMessenger::Skills {

/**
 * @brief Interface for skill handlers.
 *
 * Implement this interface to create handlers for specific skill types.
 * The handler receives the inner payload from a TaskRequest and produces
 * the inner payload for a TaskResponse.
 *
 * Skill metadata (id, name, description) is stored in SkillDescriptor,
 * not in the handler itself.
 */
class ISkillHandler {
public:
    virtual ~ISkillHandler() = default;

    /**
     * @brief Process a task request payload into a pre-allocated response buffer.
     *
     * @param request The inner payload bytes from the TaskRequest.
     *                This is the skill-specific FlatBuffer data (e.g., VectorMathRequest).
     * @param response Pre-allocated response buffer to write results into.
     *                 Use factory's scatter_response_span<true>() for typed access.
     * @return true on success, false on error.
     */
    [[nodiscard]] virtual bool process(
        std::span<const uint8_t> request,
        std::span<uint8_t> response
    ) = 0;
};

} // namespace TaskMessenger::Skills
