/**
 * @file skills/registry/SkillRegistration.hpp
 * @brief Self-registration helper for skills.
 *
 * This header provides the REGISTER_SKILL_CLASS macro that allows skills to
 * self-register during static initialization, eliminating the need for
 * a central register_builtin_skills() function.
 */
#pragma once

#include "SkillRegistry.hpp"

#include <memory>

namespace TaskMessenger::Skills {

/**
 * @brief Helper class for static skill registration.
 *
 * Create a static instance of this class to register a skill during
 * static initialization. The skill will be registered before main() runs.
 */
class SkillRegistration {
public:
    /**
     * @brief Register a skill with the global registry.
     * @param skill The skill implementation to register.
     */
    explicit SkillRegistration(std::unique_ptr<ISkill> skill) {
        SkillRegistry::instance().register_skill(std::move(skill));
    }
};

} // namespace TaskMessenger::Skills

// Token concatenation helpers for __COUNTER__ expansion
#define SKILL_REG_CONCAT_IMPL(a, b) a##b
#define SKILL_REG_CONCAT(a, b) SKILL_REG_CONCAT_IMPL(a, b)

/**
 * @def REGISTER_SKILL_CLASS
 * @brief Macro to register a combined Skill class.
 *
 * All metadata (name, description, version) is read from the class itself.
 *
 * Usage:
 * @code
 * REGISTER_SKILL_CLASS(MySkill);
 * @endcode
 *
 * @param SkillClass The skill class that extends Skill<SkillClass>
 */
#define REGISTER_SKILL_CLASS(SkillClass) \
    static ::TaskMessenger::Skills::SkillRegistration \
        SKILL_REG_CONCAT(_skill_registration_, __COUNTER__)( \
            std::make_unique<SkillClass>() \
        )
