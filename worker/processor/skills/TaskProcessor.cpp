/**
 * @file worker/processor/skills/TaskProcessor.cpp
 * @brief Implementation of the TaskProcessor class.
 */
#include "TaskProcessor.hpp"
#include "logger.hpp"

namespace TaskMessenger::Tasks {

void TaskProcessor::log_debug(const std::string& message) {
    if (logger_) {
        logger_->debug("[TaskProcessor] " + message);
    }
}

} // namespace TaskMessenger::Tasks
