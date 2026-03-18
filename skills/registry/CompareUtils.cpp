/**
 * @file skills/registry/CompareUtils.cpp
 * @brief Implementation of centralized comparison utilities.
 */
#include "CompareUtils.hpp"

#include <cmath>
#include <limits>

namespace TaskMessenger::Skills {

// =============================================================================
// Global Configuration
// =============================================================================

CompareConfig& CompareConfig::defaults() {
    static CompareConfig instance;
    return instance;
}

// =============================================================================
// Primitive Comparison Helpers
// =============================================================================

bool doubles_equal(double expected, double actual, const CompareConfig& cfg) {
    // Handle exact equality (including infinities)
    if (expected == actual) {
        return true;
    }

    // Handle NaN: both NaN = match, one NaN = mismatch
    if (std::isnan(expected) && std::isnan(actual)) {
        return true;
    }
    if (std::isnan(expected) || std::isnan(actual)) {
        return false;
    }

    double diff = std::abs(expected - actual);

    // Absolute tolerance check for near-zero values
    if (diff <= cfg.abs_epsilon) {
        return true;
    }

    // Relative tolerance check for larger values
    double max_abs = std::max(std::abs(expected), std::abs(actual));
    return diff <= cfg.rel_epsilon * max_abs;
}

bool vectors_equal(
    std::span<const double> expected,
    std::span<const double> actual,
    const CompareConfig& cfg,
    size_t* mismatch_index
) {
    if (expected.size() != actual.size()) {
        if (mismatch_index) {
            *mismatch_index = 0;
        }
        return false;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (!doubles_equal(expected[i], actual[i], cfg)) {
            if (mismatch_index) {
                *mismatch_index = i;
            }
            return false;
        }
    }
    return true;
}

// =============================================================================
// VerificationResult-returning Helpers
// =============================================================================

VerificationResult compare_scalar(
    double expected,
    double actual,
    const char* field_name,
    const CompareConfig& cfg
) {
    if (!doubles_equal(expected, actual, cfg)) {
        return VerificationResult::failure(
            std::string(field_name) + " mismatch: expected=" + 
            std::to_string(expected) + " got=" + std::to_string(actual) +
            " diff=" + std::to_string(std::abs(expected - actual)));
    }
    return VerificationResult::success();
}

VerificationResult compare_vector(
    std::span<const double> expected,
    std::span<const double> actual,
    const char* field_name,
    const CompareConfig& cfg
) {
    if (expected.size() != actual.size()) {
        return VerificationResult::failure(
            std::string(field_name) + " size mismatch: expected=" + 
            std::to_string(expected.size()) + " got=" + std::to_string(actual.size()));
    }

    size_t mismatch_idx = 0;
    if (!vectors_equal(expected, actual, cfg, &mismatch_idx)) {
        return VerificationResult::failure(
            std::string(field_name) + " mismatch at index " + std::to_string(mismatch_idx) +
            ": expected=" + std::to_string(expected[mismatch_idx]) +
            " got=" + std::to_string(actual[mismatch_idx]) +
            " diff=" + std::to_string(std::abs(expected[mismatch_idx] - actual[mismatch_idx])));
    }
    return VerificationResult::success();
}

VerificationResult compare_bytes(
    std::span<const int8_t> expected,
    std::span<const int8_t> actual,
    const char* field_name
) {
    if (expected.size() != actual.size()) {
        return VerificationResult::failure(
            std::string(field_name) + " length mismatch: expected=" + 
            std::to_string(expected.size()) + " got=" + std::to_string(actual.size()));
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            return VerificationResult::failure(
                std::string(field_name) + " mismatch at index " + std::to_string(i) +
                ": expected='" + std::string(1, static_cast<char>(expected[i])) +
                "' got='" + std::string(1, static_cast<char>(actual[i])) + "'");
        }
    }
    return VerificationResult::success();
}

VerificationResult compare_bytes(
    const int8_t* expected,
    const int8_t* actual,
    size_t length,
    const char* field_name
) {
    return compare_bytes(
        std::span<const int8_t>(expected, length),
        std::span<const int8_t>(actual, length),
        field_name);
}

} // namespace TaskMessenger::Skills
