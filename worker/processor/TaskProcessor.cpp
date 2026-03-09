/**
 * \file worker/processor/TaskProcessor.cpp
 * \brief Implementation of the task execution shim.
 *
 * Delegates to SkillRegistry for FlatBuffers-based skill dispatch.
 */
#include "TaskProcessor.hpp"
#include "logger.hpp"

bool TaskProcessor::process(
    uint32_t task_id, 
    uint32_t skill_id, 
    std::span<const uint8_t> request,
    std::span<uint8_t> response
) {
    if (logger_) {
        logger_->debug("Processing task " + std::to_string(task_id) +
                       " with skill " + std::to_string(skill_id));
    }

    auto& registry = TaskMessenger::Skills::SkillRegistry::instance();
    bool success = registry.dispatch(skill_id, task_id, request, response);
    
    if (!success) {
        // Fallback for unregistered skills - return false
        if (logger_) {
            logger_->warning("No handler for skill " + std::to_string(skill_id));
        }
    }
    
    return success;
}
