/**
 * @file skills/registry/CompareUtils.hpp
 * @brief Centralized comparison utilities for skill verification.
 *
 * Provides configurable comparison helpers for floating-point values,
 * vectors, and byte sequences used by skill compare_response() methods.
 */
#pragma once

#include "skills/registry/VerificationResult.hpp"

#include <cmath>
#include <cstdint>
#include <span>
#include <string>

namespace TaskMessenger::Skills {

/**
 * @brief Configuration for verification comparisons.
 *
 * Provides default epsilon values and testing options that can be configured
 * by the manager at startup without skills depending on manager code.
 */
struct CompareConfig {
    bool enabled = false;        ///< Enable/disable verification globally
    double abs_epsilon = 1e-9;   ///< Absolute tolerance for near-zero values
    double rel_epsilon = 1e-6;   ///< Relative tolerance for larger values
    bool inject_failure = false; ///< Corrupt response data to test failure paths

    /**
     * @brief Access the global default configuration.
     *
     * Manager can modify these defaults at startup:
     * @code
     * auto& cfg = CompareConfig::defaults();
     * cfg.enabled = manager_opts::get_verify_enabled();
     * cfg.abs_epsilon = manager_opts::get_verify_epsilon();
     * cfg.rel_epsilon = manager_opts::get_verify_rel_epsilon();
     * cfg.inject_failure = manager_opts::get_verify_inject_failure();
     * @endcode
     *
     * @return Reference to the global default configuration.
     */
    [[nodiscard]] static CompareConfig& defaults();
};

// =============================================================================
// Primitive Comparison Helpers
// =============================================================================

/**
 * @brief Compare two double values with tolerance.
 *
 * Uses absolute epsilon for values near zero, relative epsilon otherwise.
 * Handles NaN (both NaN = match), infinity, and exact equality.
 *
 * @param expected The expected value.
 * @param actual The actual value.
 * @param cfg Comparison configuration (defaults to global config).
 * @return true if values are approximately equal.
 */
[[nodiscard]] bool doubles_equal(
    double expected,
    double actual,
    const CompareConfig& cfg = CompareConfig::defaults()
);

/**
 * @brief Compare two double vectors element-wise with tolerance.
 *
 * @param expected The expected result vector.
 * @param actual The actual result vector.
 * @param cfg Comparison configuration.
 * @param mismatch_index Output: index of first mismatching element (if any).
 * @return true if vectors have same size and all elements match within tolerance.
 */
[[nodiscard]] bool vectors_equal(
    std::span<const double> expected,
    std::span<const double> actual,
    const CompareConfig& cfg = CompareConfig::defaults(),
    size_t* mismatch_index = nullptr
);

// =============================================================================
// VerificationResult-returning Helpers for compare_response()
// =============================================================================

/**
 * @brief Compare two scalar double values, returning VerificationResult.
 *
 * @param expected The expected value.
 * @param actual The actual value.
 * @param field_name Name of the field for error messages.
 * @param cfg Comparison configuration.
 * @return VerificationResult with pass/fail and diagnostic message.
 */
[[nodiscard]] VerificationResult compare_scalar(
    double expected,
    double actual,
    const char* field_name = "result",
    const CompareConfig& cfg = CompareConfig::defaults()
);

/**
 * @brief Compare two double vectors, returning VerificationResult.
 *
 * @param expected The expected result vector.
 * @param actual The actual result vector.
 * @param field_name Name of the field for error messages.
 * @param cfg Comparison configuration.
 * @return VerificationResult with pass/fail and diagnostic message.
 */
[[nodiscard]] VerificationResult compare_vector(
    std::span<const double> expected,
    std::span<const double> actual,
    const char* field_name = "result",
    const CompareConfig& cfg = CompareConfig::defaults()
);

/**
 * @brief Compare two integer values, returning VerificationResult.
 *
 * @tparam T Integer type (uint8_t, uint32_t, int8_t, etc.)
 * @param expected The expected value.
 * @param actual The actual value.
 * @param field_name Name of the field for error messages.
 * @return VerificationResult with pass/fail and diagnostic message.
 */
template<typename T>
[[nodiscard]] VerificationResult compare_int(
    T expected,
    T actual,
    const char* field_name
) {
    if (expected != actual) {
        return VerificationResult::failure(
            std::string(field_name) + " mismatch: expected=" + 
            std::to_string(expected) + " got=" + std::to_string(actual));
    }
    return VerificationResult::success();
}

/**
 * @brief Compare two byte sequences (int8_t spans), returning VerificationResult.
 *
 * Typically used for string/character data stored as [int8] in FlatBuffers.
 *
 * @param expected The expected byte sequence.
 * @param actual The actual byte sequence.
 * @param field_name Name of the field for error messages.
 * @param cfg Comparison configuration.
 * @return VerificationResult with pass/fail and diagnostic message.
 */
[[nodiscard]] VerificationResult compare_bytes(
    std::span<const int8_t> expected,
    std::span<const int8_t> actual,
    const char* field_name = "output",
    const CompareConfig& cfg = CompareConfig::defaults()
);

/**
 * @brief Compare two byte sequences via raw pointers and length.
 *
 * Convenience overload for skills that store output as pointer + length.
 *
 * @param expected Pointer to expected bytes.
 * @param actual Pointer to actual bytes.
 * @param length Number of bytes to compare.
 * @param field_name Name of the field for error messages.
 * @param cfg Comparison configuration.
 * @return VerificationResult with pass/fail and diagnostic message.
 */
[[nodiscard]] VerificationResult compare_bytes(
    const int8_t* expected,
    const int8_t* actual,
    size_t length,
    const char* field_name = "output",
    const CompareConfig& cfg = CompareConfig::defaults()
);

} // namespace TaskMessenger::Skills
