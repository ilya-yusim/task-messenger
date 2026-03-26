/**
 * @file skills/builtins/blas/DgemmSkill.cpp
 * @brief Registration for BLAS Dgemm skill.
 *
 * Computes: C = alpha * A * B + beta * C
 */

#include "skills/registry/SkillRegistration.hpp"
#include "DgemmSkill.hpp"

namespace TaskMessenger::Skills {

// Self-registration: runs before main()
REGISTER_SKILL_CLASS(DgemmSkill);

} // namespace TaskMessenger::Skills
