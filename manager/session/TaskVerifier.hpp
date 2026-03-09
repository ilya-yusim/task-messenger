/**
 * @file manager/session/TaskVerifier.hpp
 * @brief Task verification - compare response buffers with epsilon tolerance.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace TaskMessenger {

/**
 * @brief Result of response comparison.
 */
struct VerificationResult {
    bool passed = false;         ///< True if responses match within tolerance
    std::string error_message;   ///< Description if comparison failed
    
    /// @brief Create a passing result.
    static VerificationResult pass() { return {true, {}}; }
    
    /// @brief Create a failing result with message.
    static VerificationResult fail(std::string msg) { return {false, std::move(msg)}; }
};

/**
 * @brief Compares response buffers with epsilon tolerance.
 *
 * Stateless comparison utility. Session handles buffer generation and dispatch;
 * TaskVerifier only compares the resulting response buffers.
 */
class TaskVerifier {
public:
    /**
     * @brief Compare two response buffers for a given skill.
     *
     * Decodes both responses according to skill type and compares values
     * with epsilon tolerance for floating-point data.
     *
     * @param skill_id The skill identifier (determines response format).
     * @param task_id The task identifier (for error messages).
     * @param expected The locally computed response.
     * @param actual The worker's response.
     * @param abs_epsilon Absolute tolerance for floating-point comparisons.
     * @param rel_epsilon Relative tolerance for floating-point comparisons.
     * @return VerificationResult indicating pass/fail with details.
     */
    [[nodiscard]] static VerificationResult compare_responses(
        uint32_t skill_id,
        uint32_t task_id,
        std::span<const uint8_t> expected,
        std::span<const uint8_t> actual,
        double abs_epsilon = 1e-9,
        double rel_epsilon = 1e-6
    );

    /**
     * @brief Compare two double values with tolerance.
     * @param expected The expected value.
     * @param actual The actual value.
     * @param abs_epsilon Absolute tolerance.
     * @param rel_epsilon Relative tolerance.
     * @return true if values are approximately equal.
     */
    [[nodiscard]] static bool compare_doubles(
        double expected,
        double actual,
        double abs_epsilon,
        double rel_epsilon
    );

    /**
     * @brief Compare two double vectors element-wise.
     * @param expected The expected result vector.
     * @param actual The actual result vector.
     * @param abs_epsilon Absolute tolerance.
     * @param rel_epsilon Relative tolerance.
     * @param mismatch_index Output: index of first mismatching element (if any).
     * @return true if vectors match within tolerance.
     */
    [[nodiscard]] static bool compare_double_vectors(
        std::span<const double> expected,
        std::span<const double> actual,
        double abs_epsilon,
        double rel_epsilon,
        size_t& mismatch_index
    );

    /**
     * @brief Compare two strings for equality.
     * @param expected The expected string.
     * @param actual The actual string.
     * @return true if strings are identical.
     */
    [[nodiscard]] static bool compare_strings(
        std::string_view expected,
        std::string_view actual
    );
};

} // namespace TaskMessenger
