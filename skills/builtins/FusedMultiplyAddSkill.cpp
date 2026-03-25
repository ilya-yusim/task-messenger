/**
 * @file skills/builtins/FusedMultiplyAddSkill.cpp
 * @brief Registration for FusedMultiplyAdd skill.
 *
 * Computes: result[i] = a[i] + c * b[i]
 */

#include "skills/registry/SkillRegistration.hpp"
#include "FusedMultiplyAddSkill.hpp"

namespace TaskMessenger::Skills {

// Self-registration: runs before main()
REGISTER_SKILL_CLASS(FusedMultiplyAddSkill);

} // namespace TaskMessenger::Skills
