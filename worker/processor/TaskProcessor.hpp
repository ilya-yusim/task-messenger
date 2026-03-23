/**
 * \file worker/processor/TaskProcessor.hpp
 * \brief Simple task execution shim used by worker runtimes.
 *
 * Delegates to SkillRegistry for FlatBuffers-based skill dispatch.
 */
#pragma once

#include "skills/registry/SkillRegistry.hpp"

#include <span>
#include <memory>
#include <cstdint>

class Logger;

/** \brief Minimal handler for dispatcher-supplied tasks. */
class TaskProcessor {
public:
	explicit TaskProcessor(std::shared_ptr<Logger> logger) 
		: logger_(logger)
	{
		// Set logger on the global registry
		TaskMessenger::Skills::SkillRegistry::instance().set_logger(logger);
	}

	/**
	 * \brief Execute a task payload into a pre-allocated response buffer.
   * \param task_id Dispatcher-provided identifier for correlation.
	 * \param skill_id Skill identifier for dispatch.
	 * \param request Serialized task data (FlatBuffers payload).
	 * \param response Pre-allocated response buffer for skill output.
	 * \return true on success, false on error.
	 */
	bool process(
		uint32_t task_id, 
		uint32_t skill_id, 
		std::span<const uint8_t> request,
		std::span<uint8_t> response
	);
	
private:
	std::shared_ptr<Logger> logger_; ///< Logger sink used for debug output.
};
