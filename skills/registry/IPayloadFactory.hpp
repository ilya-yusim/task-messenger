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

#include "skills/registry/VerificationResult.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace TaskMessenger::Skills {

// Forward declaration
class PayloadBufferBase;

/**
 * @brief Base interface for skill payload factories.
 *
 * Each skill provides a factory with:
 * - skill_id() for identification
 * - create_payload_buffer() returning a PayloadBuffer with typed pointers
 * - create_response_buffer_for_request() to create pre-allocated response buffers
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

    /**
     * @brief Create a pre-allocated response buffer sized for a given request.
     * 
     * The returned buffer is ready to be passed to a skill handler's process() method.
     * 
     * @param request The serialized request payload (used to determine response size).
     * @return A pre-allocated response buffer, or nullptr on error.
     */
    [[nodiscard]] virtual std::unique_ptr<PayloadBufferBase> create_response_buffer_for_request(
        std::span<const uint8_t> request
    ) const = 0;

    // =========================================================================
    // Test/Verification Support
    // =========================================================================

    /**
     * @brief Create a test request buffer with predefined test data.
     *
     * Used for verification and testing. Each skill defines its own test cases.
     *
     * @param case_index Which test case to create (0 = default).
     * @return A populated request buffer, or nullptr if case_index is invalid.
     */
    [[nodiscard]] virtual std::unique_ptr<PayloadBufferBase> create_test_request_buffer(
        size_t case_index = 0
    ) const = 0;

    /**
     * @brief Get the number of available test cases.
     * @return Number of test cases this skill provides.
     */
    [[nodiscard]] virtual size_t test_case_count() const noexcept = 0;

    /**
     * @brief Verify a worker's response against locally computed result.
     *
     * Computes the expected result from the request, then compares it with
     * the worker's response using skill-specific comparison logic.
     *
     * @param request The original request payload.
     * @param worker_response The worker's response payload.
     * @return VerificationResult indicating pass/fail with optional message.
     */
    [[nodiscard]] virtual VerificationResult verify_response(
        std::span<const uint8_t> request,
        std::span<const uint8_t> worker_response
    ) const = 0;
};

} // namespace TaskMessenger::Skills
