/**
 * @file skills/registry/IPayloadFactory.hpp
 * @brief Base interface for skill-specific payload factories.
 *
 * Each skill implements a factory with its own typed create_payload() method.
 * This enables decentralized payload creation and dynamic skill loading.
 * 
 * Factories provide create_payload_buffer() methods that return PayloadBuffer
 * objects combining ownership with typed data pointers for zero-copy access.
 */
#pragma once

#include <cstdint>

namespace TaskMessenger::Skills {

/**
 * @brief Base interface for skill payload factories.
 *
 * Each skill provides a factory with:
 * - skill_id() for identification
 * - create_payload_buffer() returning a PayloadBuffer with typed pointers
 * - Optionally, static create_payload() for simple one-off creation
 *
 * TaskGenerator includes headers for specific factories and calls
 * their methods directly with typed arguments.
 */
class IPayloadFactory {
public:
    virtual ~IPayloadFactory() = default;

    /**
     * @brief Get the skill ID this factory creates payloads for.
     */
    [[nodiscard]] virtual uint32_t skill_id() const noexcept = 0;
};

} // namespace TaskMessenger::Skills
