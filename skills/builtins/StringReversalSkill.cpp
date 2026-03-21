/**
 * @file skills/builtins/StringReversalSkill.cpp
 * @brief Registration for StringReversal skill.
 *
 * Reverses the input string.
 */

#include "skills/registry/SkillRegistration.hpp"
#include "StringReversalSkill.hpp"

namespace TaskMessenger::Skills {

// Self-registration: runs before main()
REGISTER_SKILL_CLASS(
    StringReversalSkill,
    "StringReversal",
    "Reverses the input string",
    1, 256, 256
);

} // namespace TaskMessenger::Skills
