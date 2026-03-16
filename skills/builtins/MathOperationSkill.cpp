/**
 * @file skills/builtins/MathOperationSkill.cpp
 * @brief Registration for MathOperation skill.
 *
 * Performs scalar math operations (add, subtract, multiply, divide).
 */

#include "skills/registry/SkillRegistration.hpp"
#include "MathOperationSkill.hpp"

namespace TaskMessenger::Skills {

// Self-registration: runs before main()
REGISTER_SKILL_CLASS(
    MathOperationSkill,
    "MathOperation",
    "Performs scalar math operations (add, subtract, multiply, divide)",
    1, 64, 64
);

} // namespace TaskMessenger::Skills
