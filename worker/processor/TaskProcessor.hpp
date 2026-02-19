/**
 * \file worker/processor/TaskProcessor.hpp
 * \brief Simple task execution shim used by worker runtimes.
 */
#pragma once

#include <string>
#include <memory>
#include <cstdint>

class Logger;

/** \brief Minimal handler for manager-supplied tasks. */
class TaskProcessor {
public:
	explicit TaskProcessor(std::shared_ptr<Logger> logger) : logger_(std::move(logger)) {}

	/**
	 * \brief Execute a task payload and return the textual result.
	 * \param task_id Manager-provided identifier for correlation.
	 * \param skill_id Skill identifier for dispatch.
	 * \param payload Serialized task data.
	 * \return Result string delivered back to the manager.
	 */
	std::string process(uint32_t task_id, uint32_t skill_id, const std::string& payload);
private:
	std::shared_ptr<Logger> logger_; ///< Logger sink used for debug output.
};

