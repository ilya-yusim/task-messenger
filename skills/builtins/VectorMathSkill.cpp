/**
 * @file skills/builtins/VectorMathSkill.cpp
 * @brief Registration for VectorMath skill.
 *
 * Performs element-wise vector math operations.
 */

#include "skills/registry/SkillRegistration.hpp"
#include "VectorMathSkill.hpp"

namespace TaskMessenger::Skills {

// Self-registration: runs before main()
REGISTER_SKILL_CLASS(VectorMathSkill);

} // namespace TaskMessenger::Skills
