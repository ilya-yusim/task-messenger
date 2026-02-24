/**
 * @file skills/registry/SkillDescriptor.hpp
 * @brief Complete skill definition: metadata + handler + payload factory.
 */
#pragma once

#include "skills/handlers/ISkillHandler.hpp"
#include "IPayloadFactory.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace TaskMessenger::Skills {

/**
 * @brief Complete skill definition: metadata, handler, and payload factory.
 *
 * Combines skill metadata (id, name, description, version) with the
 * handler implementation and optional payload factory. Used by SkillRegistry 
 * to store everything about a skill in one place.
 *
 * - handler: Used by worker to process incoming requests
 * - payload_factory: Used by manager to create request payloads
 */
struct SkillDescriptor {
    uint32_t id{0};                           ///< Unique skill identifier
    std::string name;                         ///< Human-readable name
    std::string description;                  ///< Brief description of what the skill does
    uint32_t version{1};                      ///< Schema version for compatibility checking
    std::unique_ptr<ISkillHandler> handler;   ///< Worker-side skill implementation
    std::unique_ptr<IPayloadFactory> payload_factory; ///< Manager-side payload creation
    
    /// Typical request payload size (bytes) for buffer preallocation
    size_t typical_request_size{256};
    
    /// Typical response payload size (bytes) for buffer preallocation
    size_t typical_response_size{256};
    
    /**
     * @brief Default constructor.
     */
    SkillDescriptor() = default;
    
    /**
     * @brief Move constructor.
     */
    SkillDescriptor(SkillDescriptor&&) = default;
    
    /**
     * @brief Move assignment.
     */
    SkillDescriptor& operator=(SkillDescriptor&&) = default;
    
    // Non-copyable due to unique_ptr
    SkillDescriptor(const SkillDescriptor&) = delete;
    SkillDescriptor& operator=(const SkillDescriptor&) = delete;
    
    /**
     * @brief Convenience factory for creating complete skill descriptors.
     *
     * @param id Unique skill identifier.
     * @param name Human-readable name.
     * @param description What this skill does.
     * @param handler The skill implementation (ownership transferred).
     * @param payload_factory The payload factory (ownership transferred, may be nullptr).
     * @param version Schema version (default 1).
     * @param req_size Typical request size hint (default 256).
     * @param resp_size Typical response size hint (default 256).
     */
    static SkillDescriptor create(
        uint32_t id,
        std::string name,
        std::string description,
        std::unique_ptr<ISkillHandler> handler,
        std::unique_ptr<IPayloadFactory> payload_factory,
        uint32_t version = 1,
        size_t req_size = 256,
        size_t resp_size = 256
    ) {
        SkillDescriptor desc;
        desc.id = id;
        desc.name = std::move(name);
        desc.description = std::move(description);
        desc.handler = std::move(handler);
        desc.payload_factory = std::move(payload_factory);
        desc.version = version;
        desc.typical_request_size = req_size;
        desc.typical_response_size = resp_size;
        return desc;
    }
};

} // namespace TaskMessenger::Skills
