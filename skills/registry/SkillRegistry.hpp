/**
 * @file skills/registry/SkillRegistry.hpp
 * @brief Central registry for skills: metadata, handlers, and dispatch.
 *
 * Unified service combining skill registration, lookup, and payload dispatch.
 */
#pragma once

#include "SkillDescriptor.hpp"
#include "SkillIds.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class Logger;

namespace TaskMessenger::Skills {

/**
 * @brief Central registry for skills: metadata, handlers, and dispatch.
 *
 * Thread-safe registry that stores skill descriptors (including handlers)
 * and provides dispatch functionality. Used by:
 * - Manager: Validate skill IDs, query metadata
 * - Worker: Dispatch payloads to handlers, provide diagnostics
 *
 * Can be used as a singleton via instance() or instantiated directly for testing.
 */
class SkillRegistry {
public:
    /**
     * @brief Construct a SkillRegistry with optional logger.
     * @param logger Logger for debug output (may be nullptr).
     *
     * Skills self-register via static initialization using REGISTER_SKILL macro.
     * See SkillRegistration.hpp and skills/builtins/ for examples.
     */
    explicit SkillRegistry(std::shared_ptr<Logger> logger = nullptr);
    
    /**
     * @brief Get the global singleton instance.
     * @return Reference to the global SkillRegistry.
     */
    static SkillRegistry& instance();
    
    // Non-copyable, non-movable
    SkillRegistry(const SkillRegistry&) = delete;
    SkillRegistry& operator=(const SkillRegistry&) = delete;
    SkillRegistry(SkillRegistry&&) = delete;
    SkillRegistry& operator=(SkillRegistry&&) = delete;
    
    // =========================================================================
    // Registration
    // =========================================================================
    
    /**
     * @brief Register a skill with its handler.
     * @param descriptor Complete skill definition (takes ownership of handler).
     * @note If a skill with the same ID exists, it is replaced.
     */
    void register_skill(SkillDescriptor descriptor);
    
    // =========================================================================
    // Query
    // =========================================================================
    
    /**
     * @brief Check if a skill is registered.
     * @param skill_id The skill identifier to check.
     * @return true if the skill is registered.
     */
    [[nodiscard]] bool has_skill(uint32_t skill_id) const;
    
    /**
     * @brief Get skill name by ID.
     * @param skill_id The skill identifier.
     * @return Skill name or empty string if not found.
     */
    [[nodiscard]] std::string get_skill_name(uint32_t skill_id) const;
    
    /**
     * @brief Get all registered skill IDs.
     * @return Vector of skill IDs.
     */
    [[nodiscard]] std::vector<uint32_t> skill_ids() const;
    
    /**
     * @brief Get the number of registered skills.
     */
    [[nodiscard]] size_t skill_count() const;
    
    // =========================================================================
    // Dispatch
    // =========================================================================
    
    /**
     * @brief Dispatch a payload to the appropriate skill handler.
     *
     * @param skill_id The skill identifier.
     * @param task_id The task identifier (for logging).
     * @param payload The skill-specific request payload (FlatBuffers).
     * @return Response payload, or nullptr if dispatch/processing failed.
     */
    [[nodiscard]] std::unique_ptr<PayloadBufferBase> dispatch(
        uint32_t skill_id,
        uint32_t task_id,
        std::span<const uint8_t> payload
    );
    
    // =========================================================================
    // Payload Factory
    // =========================================================================
    
    /**
     * @brief Get the payload factory for a skill.
     * @param skill_id The skill identifier.
     * @return Pointer to the factory or nullptr if skill not found or has no factory.
     */
    [[nodiscard]] IPayloadFactory* get_payload_factory(uint32_t skill_id) const;
    
    // =========================================================================
    // Testing / Reset
    // =========================================================================
    
    /**
     * @brief Clear all registered skills (primarily for testing).
     */
    void clear();
    
    /**
     * @brief Set the logger.
     * @param logger New logger instance.
     */
    void set_logger(std::shared_ptr<Logger> logger);

private:
    void log_debug(const std::string& message);
    
    std::shared_ptr<Logger> logger_;
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, SkillDescriptor> skills_;
};

} // namespace TaskMessenger::Skills
