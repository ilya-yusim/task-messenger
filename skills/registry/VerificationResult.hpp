/**
 * @file skills/registry/VerificationResult.hpp
 * @brief Result type for task verification operations.
 *
 * VerificationResult is returned by verify_response() to indicate
 * whether a worker's response matched the locally computed result.
 */
#pragma once

#include <string>
#include <utility>

namespace TaskMessenger::Skills {

/**
 * @brief Result of verifying a worker's response against local computation.
 *
 * Used by skills to report whether verification passed or failed,
 * along with an optional diagnostic message.
 */
struct VerificationResult {
    bool passed = false;       ///< True if worker response matched expected result
    std::string message;       ///< Diagnostic message (typically empty on success)

    /**
     * @brief Create a successful verification result.
     * @return VerificationResult with passed=true.
     */
    [[nodiscard]] static VerificationResult success() {
        return {true, {}};
    }

    /**
     * @brief Create a failed verification result with a message.
     * @param msg Diagnostic message describing the failure.
     * @return VerificationResult with passed=false.
     */
    [[nodiscard]] static VerificationResult failure(std::string msg) {
        return {false, std::move(msg)};
    }
};

} // namespace TaskMessenger::Skills
