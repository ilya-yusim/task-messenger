/**
 * \file worker/processor/TaskProcessor.cpp
 * \brief Implementation of the task execution shim.
 *
 * Delegates to SkillRegistry for FlatBuffers-based skill dispatch.
 */
#include "TaskProcessor.hpp"
#include "logger.hpp"

std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> TaskProcessor::process(
    uint32_t task_id, 
    uint32_t skill_id, 
    std::span<const uint8_t> payload
) {
    if (logger_) {
        logger_->debug("Processing task " + std::to_string(task_id) +
                       " with skill " + std::to_string(skill_id));
    }

    auto& registry = TaskMessenger::Skills::SkillRegistry::instance();
    auto response = registry.dispatch(skill_id, task_id, payload);
    
    if (!response) {
        // Fallback for unregistered skills - return nullptr
        if (logger_) {
            logger_->warning("No handler for skill " + std::to_string(skill_id));
        }
    }
    
    return response;
}
