/**
 * @file manager/session/TaskVerifier.cpp
 * @brief Implementation of response comparison with epsilon tolerance.
 */
#include "TaskVerifier.hpp"
#include "skills/registry/SkillIds.hpp"
#include "skills/builtins/VectorMathPayload.hpp"
#include "skills/builtins/MathOperationPayload.hpp"
#include "skills/builtins/StringReversalPayload.hpp"
#include "skills/builtins/FusedMultiplyAddPayload.hpp"

#include <cmath>
#include <format>

namespace TaskMessenger {

using namespace Skills;

bool TaskVerifier::compare_doubles(
    double expected,
    double actual,
    double abs_epsilon,
    double rel_epsilon
) {
    // Handle exact equality (including infinities)
    if (expected == actual) {
        return true;
    }
    
    // Handle NaN
    if (std::isnan(expected) && std::isnan(actual)) {
        return true;
    }
    if (std::isnan(expected) || std::isnan(actual)) {
        return false;
    }
    
    double diff = std::abs(expected - actual);
    
    // Absolute tolerance check
    if (diff <= abs_epsilon) {
        return true;
    }
    
    // Relative tolerance check
    double max_abs = std::max(std::abs(expected), std::abs(actual));
    return diff <= rel_epsilon * max_abs;
}

bool TaskVerifier::compare_double_vectors(
    std::span<const double> expected,
    std::span<const double> actual,
    double abs_epsilon,
    double rel_epsilon,
    size_t& mismatch_index
) {
    if (expected.size() != actual.size()) {
        mismatch_index = 0;
        return false;
    }
    
    for (size_t i = 0; i < expected.size(); ++i) {
        if (!compare_doubles(expected[i], actual[i], abs_epsilon, rel_epsilon)) {
            mismatch_index = i;
            return false;
        }
    }
    return true;
}

bool TaskVerifier::compare_strings(std::string_view expected, std::string_view actual) {
    return expected == actual;
}

VerificationResult TaskVerifier::compare_responses(
    uint32_t skill_id,
    uint32_t task_id,
    std::span<const uint8_t> expected,
    std::span<const uint8_t> actual,
    double abs_epsilon,
    double rel_epsilon
) {
    switch (skill_id) {
        case SkillIds::VectorMath: {
            auto exp = VectorMathPayloadFactory::scatter_response_span<false>(expected);
            auto act = VectorMathPayloadFactory::scatter_response_span<false>(actual);
            
            if (!exp) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode expected VectorMath response", task_id));
            }
            if (!act) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode actual VectorMath response", task_id));
            }
            
            size_t mismatch_idx = 0;
            if (!compare_double_vectors(exp->result, act->result, abs_epsilon, rel_epsilon, mismatch_idx)) {
                return VerificationResult::fail(
                    std::format("Task {}: VectorMath mismatch at index {}: expected {}, got {}",
                               task_id, mismatch_idx, 
                               exp->result[mismatch_idx], act->result[mismatch_idx]));
            }
            break;
        }
        
        case SkillIds::MathOperation: {
            auto exp = MathOperationPayloadFactory::scatter_response_span(expected);
            auto act = MathOperationPayloadFactory::scatter_response_span(actual);
            
            if (!exp) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode expected MathOperation response", task_id));
            }
            if (!act) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode actual MathOperation response", task_id));
            }
            
            if (!compare_doubles(exp->result, act->result, abs_epsilon, rel_epsilon)) {
                return VerificationResult::fail(
                    std::format("Task {}: MathOperation result mismatch: expected {}, got {}",
                               task_id, exp->result, act->result));
            }
            
            if (exp->overflow != act->overflow) {
                return VerificationResult::fail(
                    std::format("Task {}: MathOperation overflow flag mismatch: expected {}, got {}",
                               task_id, exp->overflow, act->overflow));
            }
            break;
        }
        
        case SkillIds::StringReversal: {
            auto exp = StringReversalPayloadFactory::scatter_response_span(expected);
            auto act = StringReversalPayloadFactory::scatter_response_span(actual);
            
            if (!exp) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode expected StringReversal response", task_id));
            }
            if (!act) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode actual StringReversal response", task_id));
            }
            
            if (!compare_strings(exp->output, act->output)) {
                return VerificationResult::fail(
                    std::format("Task {}: StringReversal output mismatch: expected '{}', got '{}'",
                               task_id, exp->output, act->output));
            }
            
            if (exp->original_length != act->original_length) {
                return VerificationResult::fail(
                    std::format("Task {}: StringReversal length mismatch: expected {}, got {}",
                               task_id, exp->original_length, act->original_length));
            }
            break;
        }
        
        case SkillIds::FusedMultiplyAdd: {
            auto exp = FusedMultiplyAddPayloadFactory::scatter_response_span<false>(expected);
            auto act = FusedMultiplyAddPayloadFactory::scatter_response_span<false>(actual);
            
            if (!exp) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode expected FusedMultiplyAdd response", task_id));
            }
            if (!act) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode actual FusedMultiplyAdd response", task_id));
            }
            
            size_t mismatch_idx = 0;
            if (!compare_double_vectors(exp->result, act->result, abs_epsilon, rel_epsilon, mismatch_idx)) {
                return VerificationResult::fail(
                    std::format("Task {}: FusedMultiplyAdd mismatch at index {}: expected {}, got {}",
                               task_id, mismatch_idx, 
                               exp->result[mismatch_idx], act->result[mismatch_idx]));
            }
            break;
        }
        
        case SkillIds::FusedMultiplyAddMutable: {
            auto exp = FusedMultiplyAddMutablePayloadFactory::scatter_response_span<false>(expected);
            auto act = FusedMultiplyAddMutablePayloadFactory::scatter_response_span<false>(actual);
            
            if (!exp) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode expected FusedMultiplyAddMutable response", task_id));
            }
            if (!act) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode actual FusedMultiplyAddMutable response", task_id));
            }
            
            size_t mismatch_idx = 0;
            if (!compare_double_vectors(exp->result, act->result, abs_epsilon, rel_epsilon, mismatch_idx)) {
                return VerificationResult::fail(
                    std::format("Task {}: FusedMultiplyAddMutable mismatch at index {}: expected {}, got {}",
                               task_id, mismatch_idx, 
                               exp->result[mismatch_idx], act->result[mismatch_idx]));
            }
            break;
        }
        
        default:
            return VerificationResult::fail(
                std::format("Task {}: comparison not implemented for skill_id {}", 
                           task_id, skill_id));
    }
    
    return VerificationResult::pass();
}

} // namespace TaskMessenger
