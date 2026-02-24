/**
 * @file skills/registry/SkillRegistration.hpp
 * @brief Self-registration helper for skills.
 *
 * This header provides the REGISTER_SKILL macro that allows skills to
 * self-register during static initialization, eliminating the need for
 * a central register_builtin_skills() function.
 */
#pragma once

#include "SkillRegistry.hpp"
#include "SkillDescriptor.hpp"

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
     * @param descriptor The skill descriptor to register.
     */
    explicit SkillRegistration(SkillDescriptor descriptor) {
        SkillRegistry::instance().register_skill(std::move(descriptor));
    }
};

} // namespace TaskMessenger::Skills

/**
 * @def REGISTER_SKILL
 * @brief Macro to register a skill from its implementation file.
 *
 * Usage:
 * @code
 * REGISTER_SKILL(
 *     SkillIds::MySkill,
 *     "MySkill",
 *     "Description of my skill",
 *     std::make_unique<MySkillHandler>(),
 *     std::make_unique<MySkillPayloadFactory>(),
 *     version, max_input, max_output
 * );
 * @endcode
 *
 * @param id The skill ID (from SkillIds enum)
 * @param name The skill name string
 * @param description The skill description string
 * @param handler A unique_ptr to the skill handler
 * @param factory A unique_ptr to the payload factory (use nullptr if none)
 * @param version The skill version
 * @param max_input Maximum input size
 * @param max_output Maximum output size
 */
// Token concatenation helpers for __COUNTER__ expansion
#define SKILL_REG_CONCAT_IMPL(a, b) a##b
#define SKILL_REG_CONCAT(a, b) SKILL_REG_CONCAT_IMPL(a, b)

#define REGISTER_SKILL(id, name, description, handler, factory, version, max_input, max_output) \
    static ::TaskMessenger::Skills::SkillRegistration \
        SKILL_REG_CONCAT(_skill_registration_, __COUNTER__)( \
            ::TaskMessenger::Skills::SkillDescriptor::create( \
                id, name, description, handler, factory, version, max_input, max_output \
            ) \
        )
