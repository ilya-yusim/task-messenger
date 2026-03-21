/**
 * @file skills/registry/ISkill.hpp
 * @brief Combined interface for skill handlers and payload factories.
 *
 * ISkill unifies ISkillHandler and IPayloadFactory into a single interface,
 * allowing skill developers to implement one class instead of two.
 */
#pragma once

#include "skills/handlers/ISkillHandler.hpp"
#include "skills/registry/IPayloadFactory.hpp"

namespace TaskMessenger::Skills {

/**
 * @brief Combined interface for skill handler and payload factory.
 *
 * Skills implement this interface (via the Skill<Derived> base class)
 * to provide both task processing and buffer creation capabilities.
 */
class ISkill : public ISkillHandler, public IPayloadFactory {
public:
    ~ISkill() override = default;
};

} // namespace TaskMessenger::Skills
