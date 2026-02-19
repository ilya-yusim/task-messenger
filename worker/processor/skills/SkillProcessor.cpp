/**
 * @file worker/processor/skills/SkillProcessor.cpp
 * @brief Implementation of the SkillProcessor class.
 */
#include "SkillProcessor.hpp"
#include "logger.hpp"

namespace TaskMessenger::Skills {

void SkillProcessor::log_debug(const std::string& message) {
    if (logger_) {
        logger_->debug("[SkillProcessor] " + message);
    }
}

} // namespace TaskMessenger::Skills
