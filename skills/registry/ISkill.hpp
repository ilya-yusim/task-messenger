/**
 * @file skills/registry/ISkill.hpp
 * @brief Complete interface for skill implementations.
 *
 * ISkill is the single interface that all skills implement. It provides
 * task processing, payload factory, and identity/metadata capabilities.
 */
#pragma once

#include "skills/registry/IPayloadFactory.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace TaskMessenger::Skills {

/**
 * @brief Complete interface for skill implementations.
 *
 * Skills implement this interface (via the Skill<Derived> base class)
 * to provide task processing, buffer creation, and identity capabilities.
 *
 * Provides:
 * - process()           handle a request and write a response
 * - skill_id()          (inherited from IPayloadFactory)
 * - skill_name()        namespaced dotted name
 * - skill_description() human-readable description
 * - skill_version()     schema version
 */
class ISkill : public IPayloadFactory {
public:
    ~ISkill() override = default;

    // =========================================================================
    // Task processing
    // =========================================================================

    /**
     * @brief Process a task request payload into a pre-allocated response buffer.
     *
     * @param request The inner payload bytes from the TaskRequest.
     *                This is the skill-specific FlatBuffer data.
     * @param response Pre-allocated response buffer to write results into.
     * @return true on success, false on error.
     */
    [[nodiscard]] virtual bool process(
        std::span<const uint8_t> request,
        std::span<uint8_t> response
    ) = 0;

    // =========================================================================
    // Identity and metadata
    // =========================================================================

    /** @brief Get the namespaced skill name (e.g., "builtin.StringReversal"). */
    [[nodiscard]] virtual std::string_view skill_name() const noexcept = 0;

    /** @brief Get a human-readable description of what this skill does. */
    [[nodiscard]] virtual std::string_view skill_description() const noexcept = 0;

    /** @brief Get the schema version for compatibility checking. */
    [[nodiscard]] virtual uint32_t skill_version() const noexcept = 0;
};

} // namespace TaskMessenger::Skills
