/**
 * \file worker/processor/TaskProcessor.hpp
 * \brief Simple task execution shim used by worker runtimes.
 *
 * Delegates to SkillRegistry for FlatBuffers-based skill dispatch.
 */
#pragma once

#include "skills/registry/SkillRegistry.hpp"
#include "skills/registry/PayloadBuffer.hpp"

#include <span>
#include <memory>
#include <cstdint>

class Logger;

/** \brief Minimal handler for manager-supplied tasks. */
class TaskProcessor {
public:
	explicit TaskProcessor(std::shared_ptr<Logger> logger) 
		: logger_(logger)
	{
		// Set logger on the global registry
		TaskMessenger::Skills::SkillRegistry::instance().set_logger(logger);
	}

	/**
	 * \brief Execute a task payload and return the result.
	 * \param task_id Manager-provided identifier for correlation.
	 * \param skill_id Skill identifier for dispatch.
	 * \param payload Serialized task data (FlatBuffers payload).
	 * \return Response payload, or nullptr on error.
	 */
	std::unique_ptr<TaskMessenger::Skills::PayloadBufferBase> process(
		uint32_t task_id, 
		uint32_t skill_id, 
		std::span<const uint8_t> payload
	);
	
private:
	std::shared_ptr<Logger> logger_; ///< Logger sink used for debug output.
};
