/**
 * @file manager/session/TaskVerifier.cpp
 * @brief Implementation of response comparison with epsilon tolerance.
 */
#include "TaskVerifier.hpp"
#include "skills/registry/SkillIds.hpp"
#include "skills/builtins/VectorMathSkill.hpp"
#include "skills/builtins/MathOperationSkill.hpp"
#include "skills/builtins/StringReversalSkill.hpp"
#include "skills/builtins/FusedMultiplyAddSkill.hpp"

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
            auto exp = VectorMathSkill::get_result(expected);
            auto act = VectorMathSkill::get_result(actual);
            
            if (!exp) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode expected VectorMath response", task_id));
            }
            if (!act) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode actual VectorMath response", task_id));
            }
            
            size_t mismatch_idx = 0;
            if (!compare_double_vectors(*exp, *act, abs_epsilon, rel_epsilon, mismatch_idx)) {
                return VerificationResult::fail(
                    std::format("Task {}: VectorMath mismatch at index {}: expected {}, got {}",
                               task_id, mismatch_idx, 
                               (*exp)[mismatch_idx], (*act)[mismatch_idx]));
            }
            break;
        }
        
        case SkillIds::MathOperation: {
            double exp_result = MathOperationSkill::get_result(expected);
            double act_result = MathOperationSkill::get_result(actual);
            
            if (!compare_doubles(exp_result, act_result, abs_epsilon, rel_epsilon)) {
                return VerificationResult::fail(
                    std::format("Task {}: MathOperation result mismatch: expected {}, got {}",
                               task_id, exp_result, act_result));
            }
            
            bool exp_overflow = MathOperationSkill::get_overflow(expected);
            bool act_overflow = MathOperationSkill::get_overflow(actual);
            
            if (exp_overflow != act_overflow) {
                return VerificationResult::fail(
                    std::format("Task {}: MathOperation overflow flag mismatch: expected {}, got {}",
                               task_id, exp_overflow, act_overflow));
            }
            break;
        }
        
        case SkillIds::StringReversal: {
            auto exp = StringReversalSkill::get_output(expected);
            auto act = StringReversalSkill::get_output(actual);
            
            if (!exp) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode expected StringReversal response", task_id));
            }
            if (!act) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode actual StringReversal response", task_id));
            }
            
            if (!compare_strings(*exp, *act)) {
                return VerificationResult::fail(
                    std::format("Task {}: StringReversal output mismatch: expected '{}', got '{}'",
                               task_id, *exp, *act));
            }
            
            uint32_t exp_length = StringReversalSkill::get_original_length(expected);
            uint32_t act_length = StringReversalSkill::get_original_length(actual);
            
            if (exp_length != act_length) {
                return VerificationResult::fail(
                    std::format("Task {}: StringReversal length mismatch: expected {}, got {}",
                               task_id, exp_length, act_length));
            }
            break;
        }
        
        case SkillIds::FusedMultiplyAdd: {
            auto exp = FusedMultiplyAddSkill::get_result(expected);
            auto act = FusedMultiplyAddSkill::get_result(actual);
            
            if (!exp) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode expected FusedMultiplyAdd response", task_id));
            }
            if (!act) {
                return VerificationResult::fail(
                    std::format("Task {}: failed to decode actual FusedMultiplyAdd response", task_id));
            }
            
            size_t mismatch_idx = 0;
            if (!compare_double_vectors(*exp, *act, abs_epsilon, rel_epsilon, mismatch_idx)) {
                return VerificationResult::fail(
                    std::format("Task {}: FusedMultiplyAdd mismatch at index {}: expected {}, got {}",
                               task_id, mismatch_idx, 
                               (*exp)[mismatch_idx], (*act)[mismatch_idx]));
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
