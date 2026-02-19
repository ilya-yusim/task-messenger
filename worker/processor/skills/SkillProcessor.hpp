/**
 * @file worker/processor/skills/SkillProcessor.hpp
 * @brief Skill processor using FlatBuffers serialization.
 *
 * The SkillProcessor receives skill payloads (routing metadata comes from TaskHeader),
 * dispatches them to the appropriate handler based on skill_id, and returns response payloads.
 */
#pragma once

#include "ISkillHandler.hpp"
#include "StringReversalHandler.hpp"
#include "MathOperationHandler.hpp"
#include "VectorMathHandler.hpp"
#include "FusedMultiplyAddHandler.hpp"
#include "skill_task_generated.h"

#include <memory>
#include <unordered_map>
#include <span>
#include <vector>

class Logger;

namespace TaskMessenger::Skills {

/**
 * @brief Skill processor with handler registry.
 *
 * Processes skill payloads by:
 * 1. Looking up the handler for skill_id
 * 2. Dispatching payload to the handler
 * 3. Returning the handler's response payload
 *
 * Routing metadata (skill_id, task_id) is provided by TaskHeader, not embedded in payloads.
 */
class SkillProcessor {
public:
    /**
     * @brief Construct a SkillProcessor with optional logger.
     * @param logger Logger for debug output (may be nullptr).
     */
    explicit SkillProcessor(std::shared_ptr<Logger> logger = nullptr)
        : logger_(std::move(logger))
    {
        // Register built-in handlers
        register_handler(std::make_unique<StringReversalHandler>());
        register_handler(std::make_unique<MathOperationHandler>());
        register_handler(std::make_unique<VectorMathHandler>());
        register_handler(std::make_unique<FusedMultiplyAddHandler>());
        register_handler(std::make_unique<FusedMultiplyAddMutableHandler>());
    }

    /**
     * @brief Register a skill handler.
     *
     * @param handler The handler to register. Ownership is transferred.
     * @note If a handler with the same skill_id already exists, it is replaced.
     */
    void register_handler(std::unique_ptr<ISkillHandler> handler) {
        if (handler) {
            uint32_t id = handler->skill_id();
            handlers_[id] = std::move(handler);
        }
    }

    /**
     * @brief Check if a handler is registered for a skill_id.
     */
    [[nodiscard]] bool has_handler(uint32_t skill_id) const {
        return handlers_.find(skill_id) != handlers_.end();
    }

    /**
     * @brief Process a skill payload and return the response payload.
     *
     * @param skill_id The skill identifier (from TaskHeader).
     * @param task_id The task identifier for logging (from TaskHeader).
     * @param payload The skill-specific FlatBuffers payload (e.g., StringReversalRequest).
     * @param response_out Output vector for the response payload (e.g., StringReversalResponse).
     * @return true if processing succeeded, false on error.
     */
    [[nodiscard]] bool process(
        uint32_t skill_id,
        uint32_t task_id,
        std::span<const uint8_t> payload,
        std::vector<uint8_t>& response_out
    ) {
        // Find handler
        auto it = handlers_.find(skill_id);
        if (it == handlers_.end()) {
            log_debug("Unknown skill_id=" + std::to_string(skill_id) +
                      " for task_id=" + std::to_string(task_id));
            return false;
        }

        // Process with handler
        bool success = it->second->process(payload, response_out);

        if (success) {
            log_debug("Processed skill=" + std::string(it->second->skill_name()) +
                      " task_id=" + std::to_string(task_id));
        } else {
            log_debug("Failed to process skill=" + std::string(it->second->skill_name()) +
                      " task_id=" + std::to_string(task_id));
        }

        return success;
    }

    /**
     * @brief Get the number of registered handlers.
     */
    [[nodiscard]] size_t handler_count() const noexcept {
        return handlers_.size();
    }

private:
    std::shared_ptr<Logger> logger_;
    std::unordered_map<uint32_t, std::unique_ptr<ISkillHandler>> handlers_;

    void log_debug(const std::string& message);
};

} // namespace TaskMessenger::Skills
