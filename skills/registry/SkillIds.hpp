/**
 * @file skills/registry/SkillIds.hpp
 * @brief Compile-time skill identifier constants.
 *
 * Central location for skill IDs ensures consistency between manager and worker.
 */
#pragma once

#include <cstdint>

namespace TaskMessenger::Skills {

/**
 * @brief Compile-time skill identifier constants.
 *
 * Add new skill IDs here when implementing new handlers.
 * Keep IDs unique and document the associated handler.
 */
namespace SkillIds {
    /// String reversal skill (StringReversalHandler)
    constexpr uint32_t StringReversal = 1;
    
    /// Scalar math operation skill (MathOperationHandler)
    constexpr uint32_t MathOperation = 2;
    
    /// Vector element-wise math skill (VectorMathHandler)
    constexpr uint32_t VectorMath = 3;
    
    /// Fused multiply-add with scalar-as-vector (FusedMultiplyAddHandler)
    constexpr uint32_t FusedMultiplyAdd = 4;
    
    /// Fused multiply-add with true scalar (FusedMultiplyAddMutableHandler)
    constexpr uint32_t FusedMultiplyAddMutable = 5;
    
    /// Maximum valid skill ID (for iteration/validation)
    constexpr uint32_t MaxSkillId = 5;
    
    /// Total number of defined skills
    constexpr uint32_t Count = 5;
}

} // namespace TaskMessenger::Skills
