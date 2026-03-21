/**
 * @file skills/registry/CompareUtils.cpp
 * @brief Implementation of centralized comparison utilities.
 */
#include "CompareUtils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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
    // Inject failure: offset actual value to simulate worker returning wrong result
    double check_actual = actual;
    bool injected = false;
    if (cfg.inject_failure) {
        check_actual = actual + 1000.0;  // Large offset guarantees mismatch
        injected = true;
    }
    
    if (!doubles_equal(expected, check_actual, cfg)) {
        std::string msg = std::string(field_name) + " mismatch: expected=" + 
            std::to_string(expected) + " got=" + std::to_string(check_actual) +
            " diff=" + std::to_string(std::abs(expected - check_actual));
        if (injected) {
            msg += " [INJECTED]";
        }
        return VerificationResult::failure(msg);
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

    // Inject failure: offset first element to simulate worker returning wrong result
    bool injected = false;
    std::vector<double> check_actual;
    std::span<const double> actual_span = actual;
    if (cfg.inject_failure && !actual.empty()) {
        check_actual.assign(actual.begin(), actual.end());
        check_actual[0] += 1000.0;  // Large offset guarantees mismatch
        actual_span = check_actual;
        injected = true;
    }

    size_t mismatch_idx = 0;
    if (!vectors_equal(expected, actual_span, cfg, &mismatch_idx)) {
        std::string msg = std::string(field_name) + " mismatch at index " + std::to_string(mismatch_idx) +
            ": expected=" + std::to_string(expected[mismatch_idx]) +
            " got=" + std::to_string(actual_span[mismatch_idx]) +
            " diff=" + std::to_string(std::abs(expected[mismatch_idx] - actual_span[mismatch_idx]));
        if (injected) {
            msg += " [INJECTED]";
        }
        return VerificationResult::failure(msg);
    }
    return VerificationResult::success();
}

VerificationResult compare_bytes(
    std::span<const int8_t> expected,
    std::span<const int8_t> actual,
    const char* field_name,
    const CompareConfig& cfg
) {
    if (expected.size() != actual.size()) {
        return VerificationResult::failure(
            std::string(field_name) + " length mismatch: expected=" + 
            std::to_string(expected.size()) + " got=" + std::to_string(actual.size()));
    }

    // Inject failure: flip first byte to simulate worker returning wrong result
    bool injected = false;
    std::vector<int8_t> check_actual;
    std::span<const int8_t> actual_span = actual;
    if (cfg.inject_failure && !actual.empty()) {
        check_actual.assign(actual.begin(), actual.end());
        check_actual[0] ^= 0x20;  // Flip case bit for printable chars
        actual_span = check_actual;
        injected = true;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual_span[i]) {
            std::string msg = std::string(field_name) + " mismatch at index " + std::to_string(i) +
                ": expected='" + std::string(1, static_cast<char>(expected[i])) +
                "' got='" + std::string(1, static_cast<char>(actual_span[i])) + "'";
            if (injected) {
                msg += " [INJECTED]";
            }
            return VerificationResult::failure(msg);
        }
    }
    return VerificationResult::success();
}

VerificationResult compare_bytes(
    const int8_t* expected,
    const int8_t* actual,
    size_t length,
    const char* field_name,
    const CompareConfig& cfg
) {
    return compare_bytes(
        std::span<const int8_t>(expected, length),
        std::span<const int8_t>(actual, length),
        field_name,
        cfg);
}

} // namespace TaskMessenger::Skills
