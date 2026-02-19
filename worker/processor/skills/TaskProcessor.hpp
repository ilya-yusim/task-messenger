/**
 * @file worker/processor/skills/TaskProcessor.hpp
 * @brief Task processor using FlatBuffers serialization.
 *
 * The TaskProcessor receives TaskRequest messages, dispatches them to
 * the appropriate handler based on task_type, and returns TaskResponse messages.
 */
#pragma once

#include "ITaskHandler.hpp"
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

namespace TaskMessenger::Tasks {

/**
 * @brief Task processor with handler registry.
 *
 * Processes TaskRequest messages by:
 * 1. Parsing the envelope to extract task_type and task_id
 * 2. Dispatching to the registered handler for that task_type
 * 3. Wrapping the handler's response in a TaskResponse envelope
 */
class TaskProcessor {
public:
    /**
     * @brief Construct a TaskProcessor with optional logger.
     * @param logger Logger for debug output (may be nullptr).
     */
    explicit TaskProcessor(std::shared_ptr<Logger> logger = nullptr)
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
     * @brief Register a task handler.
     *
     * @param handler The handler to register. Ownership is transferred.
     * @note If a handler with the same task_type already exists, it is replaced.
     */
    void register_handler(std::unique_ptr<ITaskHandler> handler) {
        if (handler) {
            uint32_t id = handler->task_type();
            handlers_[id] = std::move(handler);
        }
    }

    /**
     * @brief Check if a handler is registered for a task_type.
     */
    [[nodiscard]] bool has_handler(uint32_t task_type) const {
        return handlers_.find(task_type) != handlers_.end();
    }

    /**
     * @brief Process a TaskRequest and return a TaskResponse.
     *
     * @param request_bytes The serialized TaskRequest bytes.
     * @return Serialized TaskResponse bytes.
     */
    [[nodiscard]] std::vector<uint8_t> process(std::span<const uint8_t> request_bytes) {
        // Parse the envelope
        auto task_request = GetTaskRequest(request_bytes.data());
        if (!task_request) {
            return build_error_response(0, 0);
        }

        uint32_t task_type = task_request->task_type();
        uint32_t task_id = task_request->task_id();

        // Find handler
        auto it = handlers_.find(task_type);
        if (it == handlers_.end()) {
            log_debug("Unknown task_type=" + std::to_string(task_type) +
                      " for task_id=" + std::to_string(task_id));
            return build_error_response(task_type, task_id);
        }

        // Get payload
        auto payload = task_request->payload();
        if (!payload) {
            log_debug("Empty payload for task_type=" + std::to_string(task_type));
            return build_error_response(task_type, task_id);
        }

        // Process with handler
        std::vector<uint8_t> response_payload;
        bool success = it->second->process(
            std::span<const uint8_t>(payload->data(), payload->size()),
            response_payload
        );

        if (success) {
            log_debug("Processed task=" + std::string(it->second->task_name()) +
                      " task_id=" + std::to_string(task_id));
        } else {
            log_debug("Failed to process task=" + std::string(it->second->task_name()) +
                      " task_id=" + std::to_string(task_id));
        }

        return build_response(task_type, task_id, success, response_payload);
    }

    /**
     * @brief Get the number of registered handlers.
     */
    [[nodiscard]] size_t handler_count() const noexcept {
        return handlers_.size();
    }

private:
    std::shared_ptr<Logger> logger_;
    std::unordered_map<uint32_t, std::unique_ptr<ITaskHandler>> handlers_;

    void log_debug(const std::string& message);

    [[nodiscard]] std::vector<uint8_t> build_response(
        uint32_t task_type,
        uint32_t task_id,
        bool success,
        const std::vector<uint8_t>& payload
    ) {
        flatbuffers::FlatBufferBuilder builder(128 + payload.size());

        flatbuffers::Offset<flatbuffers::Vector<uint8_t>> payload_offset = 0;
        if (!payload.empty()) {
            payload_offset = builder.CreateVector(payload);
        }

        auto response = CreateTaskResponse(
            builder,
            task_type,
            task_id,
            success,
            payload_offset
        );
        builder.Finish(response);

        return std::vector<uint8_t>(
            builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()
        );
    }

    [[nodiscard]] std::vector<uint8_t> build_error_response(
        uint32_t task_type,
        uint32_t task_id
    ) {
        flatbuffers::FlatBufferBuilder builder(64);
        auto response = CreateTaskResponse(builder, task_type, task_id, false, 0);
        builder.Finish(response);

        return std::vector<uint8_t>(
            builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()
        );
    }
};

} // namespace TaskMessenger::Tasks
