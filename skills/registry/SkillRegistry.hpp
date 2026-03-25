/**
 * @file skills/registry/SkillRegistry.hpp
 * @brief Central registry for skills: metadata, handlers, and dispatch.
 *
 * Unified service combining skill registration, lookup, and payload dispatch.
 */
#pragma once

#include "ISkill.hpp"
#include "SkillKey.hpp"
#include "VerificationResult.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Logger;

namespace TaskMessenger::Skills {

/**
 * @brief Central registry for skills: metadata, handlers, and dispatch.
 *
 * Thread-safe registry that stores ISkill implementations
 * and provides dispatch functionality. Used by:
 * - Dispatcher: Validate skill IDs, query metadata
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
     * Skills self-register via static initialization using REGISTER_SKILL_CLASS macro.
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
     * @brief Register a skill implementation.
     * @param skill The skill to register (takes ownership).
     * @note Aborts if a hash collision is detected (different name, same ID).
     */
    void register_skill(std::unique_ptr<ISkill> skill);
    
    // =========================================================================
    // Query
    // =========================================================================
    
    /**
     * @brief Check if a skill is registered (by numeric ID).
     * @param skill_id The skill identifier to check.
     * @return true if the skill is registered.
     */
    [[nodiscard]] bool has_skill(uint32_t skill_id) const;

    /**
     * @brief Check if a skill is registered (by name).
     * @param name The namespaced skill name (e.g., "builtin.StringReversal").
     * @return true if the skill is registered.
     */
    [[nodiscard]] bool has_skill(std::string_view name) const;
    
    /**
     * @brief Get skill ID by name.
     * @param name The namespaced skill name.
     * @return Skill ID, or 0 if not found.
     */
    [[nodiscard]] uint32_t get_skill_id(std::string_view name) const;

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
     * @param request The skill-specific request payload (FlatBuffers).
     * @param response Pre-allocated response buffer for skill output.
     * @return true on success, false if dispatch/processing failed.
     */
    [[nodiscard]] bool dispatch(
        uint32_t skill_id,
        uint32_t task_id,
        std::span<const uint8_t> request,
        std::span<uint8_t> response
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

    /**
     * @brief Create a pre-allocated response buffer for a request.
     * @param skill_id The skill identifier.
     * @param request The serialized request payload.
     * @return A pre-allocated response buffer, or nullptr on error.
     */
    [[nodiscard]] std::unique_ptr<PayloadBufferBase> create_response_buffer(
        uint32_t skill_id,
        std::span<const uint8_t> request
    ) const;

    /**
     * @brief Create a test request buffer for a skill.
     * @param skill_id The skill identifier.
     * @param case_index Which test case to create (0 = default).
     * @return Populated test request buffer, or nullptr on error.
     */
    [[nodiscard]] std::unique_ptr<PayloadBufferBase> create_test_request_buffer(
        uint32_t skill_id,
        size_t case_index = 0
    ) const;

    /**
     * @brief Get the number of test cases for a skill.
     * @param skill_id The skill identifier.
     * @return Number of test cases, or 0 if skill not found.
     */
    [[nodiscard]] size_t get_test_case_count(uint32_t skill_id) const;

    /**
     * @brief Verify a worker's response against locally computed result.
     * @param skill_id The skill identifier.
     * @param request The original request payload.
     * @param worker_response The worker's response payload.
     * @return VerificationResult indicating pass/fail with optional message.
     */
    [[nodiscard]] VerificationResult verify_response(
        uint32_t skill_id,
        std::span<const uint8_t> request,
        std::span<const uint8_t> worker_response
    ) const;
    
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
    std::unordered_map<uint32_t, std::unique_ptr<ISkill>> skills_;
    std::unordered_map<std::string, uint32_t> name_to_id_;
};

} // namespace TaskMessenger::Skills
