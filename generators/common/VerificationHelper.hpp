/**
 * \file VerificationHelper.hpp
 * \brief Encapsulates request cloning and post-response verification logic.
 *
 * Used by generators that need to verify worker results against locally
 * computed results. Reads CompareConfig to decide whether verification is
 * active and delegates to SkillRegistry::verify_response().
 */
#pragma once

#include "skills/registry/PayloadBuffer.hpp"
#include "skills/registry/VerificationResult.hpp"

#include <cstdint>
#include <memory>
#include <span>

class VerificationHelper {
public:
    /// \brief Returns true when verification is globally enabled.
    [[nodiscard]] static bool is_enabled();

    /**
     * \brief Clone a request buffer for later verification.
     * \return A clone of \p request, or nullptr if verification is disabled.
     */
    [[nodiscard]] static std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase>
    clone_request(const TaskMessenger::Skills::PayloadBufferBase& request);

    /**
     * \brief Verify a worker's response against locally computed result.
     *
     * Delegates to SkillRegistry::verify_response() and logs failures to stderr.
     *
     * \param task_id   Task identifier (for log messages).
     * \param skill_id  Skill identifier.
     * \param request   The original request payload (cloned copy).
     * \param response  The worker's response payload.
     * \return VerificationResult indicating pass/fail.
     */
    [[nodiscard]] static TaskMessenger::Skills::VerificationResult verify(
        uint32_t task_id,
        uint32_t skill_id,
        std::span<const uint8_t> request,
        std::span<const uint8_t> response);
};
