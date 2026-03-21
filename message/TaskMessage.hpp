// TaskMessage.hpp - Zero-copy task message with separate header and payload storage
#pragma once

#include "message/TaskCompletionSource.hpp"
#include "skills/registry/PayloadBuffer.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>

using TaskMessenger::Skills::PayloadBufferBase;

/**
 * \defgroup message_module Task Message Module
 * \brief Serialization helpers and coroutine-friendly pools for manager/worker communication.
 */

/**
 * \file message/TaskMessage.hpp
 * \brief In-memory task framing utilities shared between manager and worker.
 * \ingroup message_module
 */

/** \brief Header framing task requests and responses. */
struct TaskHeader {
    uint32_t task_id;    ///< Unique task identifier shared across request/response
    uint32_t body_size;  ///< Size of following payload (bytes)
    uint32_t skill_id;   ///< Skill identifier for dispatch
};

/**
 * \brief Zero-copy message buffer carrying header, payload, and timing metadata.
 * \ingroup message_module
 * 
 * Header and payload are stored separately to enable zero-copy construction
 * when the payload is moved in. The wire_bytes() method returns
 * separate spans for scatter-gather I/O with TCP_NODELAY.
 * 
 * Uses PayloadBufferBase for type-erased payload with typed access via downcast.
 */
class TaskMessage {
public:
    static constexpr std::size_t kHeaderSize = sizeof(TaskHeader);

    TaskMessage()
        : header_{}
        , payload_buffer_{}
        , created_time_(std::chrono::steady_clock::now()) {}

    // Move-only (unique_ptr is not copyable)
    TaskMessage(const TaskMessage&) = delete;
    TaskMessage& operator=(const TaskMessage&) = delete;
    TaskMessage(TaskMessage&&) noexcept = default;
    TaskMessage& operator=(TaskMessage&&) noexcept = default;

    /**
     * \brief Construct a TaskMessage taking ownership of a PayloadBuffer (zero-copy).
     * \param id Task identifier
     * \param buffer Unique pointer to PayloadBufferBase (moved)
     * 
     * Use this constructor with factory.create_payload() or create_payload_buffer()
     * for zero-copy buffer transfer. The skill_id is extracted from the buffer.
     * \throws std::invalid_argument if buffer is null.
     * \throws std::length_error if buffer size exceeds protocol limits.
     */
    TaskMessage(uint32_t id, std::unique_ptr<PayloadBufferBase> buffer)
        : header_{id, validated_payload_size(buffer), buffer->skill_id()}
        , payload_buffer_(std::move(buffer))
        , created_time_(std::chrono::steady_clock::now()) {}

    /**
     * \brief Construct a TaskMessage with both request and pre-allocated response buffers.
     * \param id Task identifier
     * \param request_buffer Request payload buffer (moved)
     * \param response_buffer Pre-allocated response buffer for skill output (moved)
     * 
     * Use this constructor when pre-allocating response buffers for zero-copy
     * skill processing. The response buffer will be passed to the skill handler.
     * \throws std::invalid_argument if request_buffer is null.
     * \throws std::length_error if request_buffer size exceeds protocol limits.
     */
    TaskMessage(uint32_t id, 
                std::unique_ptr<PayloadBufferBase> request_buffer,
                std::unique_ptr<PayloadBufferBase> response_buffer)
        : header_{id, validated_payload_size(request_buffer), request_buffer->skill_id()}
        , payload_buffer_(std::move(request_buffer))
        , response_buffer_(std::move(response_buffer))
        , created_time_(std::chrono::steady_clock::now()) {}

    [[nodiscard]] bool is_valid() const noexcept { return header_.task_id != 0; }

    [[nodiscard]] uint32_t task_id() const noexcept { return header_.task_id; }
    [[nodiscard]] uint32_t skill_id() const noexcept { return header_.skill_id; }
    [[nodiscard]] uint32_t body_size() const noexcept { return header_.body_size; }

    [[nodiscard]] TaskHeader header_view() const noexcept { return header_; }

    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        return payload_buffer_ ? payload_buffer_->span() : std::span<const uint8_t>{};
    }

    [[nodiscard]] std::span<const std::uint8_t> payload_bytes() const noexcept {
        return payload();
    }

    /**
     * \brief Get header and payload as separate spans for scatter-gather I/O.
     * \return Pair of spans: (header_bytes, payload_bytes)
     * \note TCP_NODELAY is enabled by default for low-latency scatter-send.
     */
    [[nodiscard]] std::pair<std::span<const std::uint8_t>, std::span<const std::uint8_t>> 
    wire_bytes() const noexcept {
        return {
            {reinterpret_cast<const std::uint8_t*>(&header_), kHeaderSize},
            payload()
        };
    }

    /**
     * \brief Get just the header bytes.
     * \return Span of header bytes.
     */
    [[nodiscard]] std::span<const std::uint8_t> header_bytes() const noexcept {
        return {reinterpret_cast<const std::uint8_t*>(&header_), kHeaderSize};
    }

    [[nodiscard]] std::chrono::milliseconds get_age() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - created_time_);
    }

    /**
     * \brief Check if this message holds a PayloadBuffer.
     */
    [[nodiscard]] bool has_payload_buffer() const noexcept {
        return payload_buffer_ != nullptr;
    }

    /**
     * \brief Check if this message holds a pre-allocated response buffer.
     */
    [[nodiscard]] bool has_response_buffer() const noexcept {
        return response_buffer_ != nullptr;
    }

    /**
     * \brief Get raw pointer to the response buffer.
     * \return Pointer to response buffer, or nullptr if not present.
     */
    [[nodiscard]] PayloadBufferBase* response_buffer() noexcept {
        return response_buffer_.get();
    }

    [[nodiscard]] const PayloadBufferBase* response_buffer() const noexcept {
        return response_buffer_.get();
    }

    /**
     * \brief Release ownership of the response buffer.
     * \return unique_ptr to response buffer, or nullptr if not holding one.
     */
    [[nodiscard]] std::unique_ptr<PayloadBufferBase> release_response_buffer() {
        return std::move(response_buffer_);
    }

    /**
     * \brief Downcast the response buffer to a typed PayloadBuffer.
     * 
     * \tparam PayloadType The specific PayloadBuffer type (e.g., VectorMathResponseBuffer)
     * \return Pointer to the typed response, or nullptr if type mismatch or no buffer.
     */
    template<typename PayloadType>
    [[nodiscard]] PayloadType* response_as() noexcept {
        return dynamic_cast<PayloadType*>(response_buffer_.get());
    }

    template<typename PayloadType>
    [[nodiscard]] const PayloadType* response_as() const noexcept {
        return dynamic_cast<const PayloadType*>(response_buffer_.get());
    }

    /**
     * \brief Release ownership of the PayloadBuffer for reuse.
     * 
     * After calling this, the message's payload is empty. The returned buffer
     * can be modified and passed to a new TaskMessage.
     * 
     * \return unique_ptr to PayloadBufferBase, or nullptr if not holding one.
     */
    [[nodiscard]] std::unique_ptr<PayloadBufferBase> release_payload() {
        if (payload_buffer_) {
            header_.body_size = 0;
            return std::move(payload_buffer_);
        }
        return nullptr;
    }

    /**
     * \brief Downcast the payload buffer to a typed PayloadBuffer.
     * 
     * \tparam PayloadType The specific PayloadBuffer type (e.g., VectorMathPayload)
     * \return Pointer to the typed payload, or nullptr if type mismatch or no buffer.
     */
    template<typename PayloadType>
    [[nodiscard]] PayloadType* payload_as() noexcept {
        return dynamic_cast<PayloadType*>(payload_buffer_.get());
    }

    template<typename PayloadType>
    [[nodiscard]] const PayloadType* payload_as() const noexcept {
        return dynamic_cast<const PayloadType*>(payload_buffer_.get());
    }

    // --- Completion source support for async response processing ---

    /**
     * \brief Attach a completion source for async notification.
     * \param source Shared pointer to TaskCompletionSource
     * 
     * When the response arrives, Session will call complete() to fill
     * response data and post resumption to the ResponseContext.
     */
    void set_completion_source(std::shared_ptr<TaskCompletionSource> source) {
        completion_source_ = std::move(source);
    }
    
    /**
     * \brief Check if this task has an awaiting coroutine.
     */
    [[nodiscard]] bool has_completion_source() const noexcept {
        return completion_source_ != nullptr;
    }
    
    /**
     * \brief Get the completion source (for Session to call complete()).
     */
    [[nodiscard]] std::shared_ptr<TaskCompletionSource> completion_source() const noexcept {
        return completion_source_;
    }
    
    /**
     * \brief Release ownership of the completion source.
     */
    [[nodiscard]] std::shared_ptr<TaskCompletionSource> release_completion_source() {
        return std::move(completion_source_);
    }
    
    /**
     * \brief Complete the task and resume the awaiting coroutine.
     * \param response_header The response header from worker
     * \param response_body The response body data
     * \param success Whether the task was successful
     * 
     * Convenience method that delegates to TaskCompletionSource::complete().
     * Called by Session after receiving response from worker.
     */
    void complete(const TaskHeader& response_header, 
                  std::span<const std::byte> response_body,
                  bool success) {
        if (completion_source_) {
            completion_source_->complete(
                response_header.task_id,
                response_header.skill_id,
                response_body,
                success
            );
        }
    }

private:
    /// \brief Validate a payload buffer is non-null and fits in the protocol's uint32_t body_size.
    /// \throws std::invalid_argument if buf is null.
    /// \throws std::length_error if buf->size() exceeds uint32_t max.
    static uint32_t validated_payload_size(const std::unique_ptr<PayloadBufferBase>& buf) {
        if (!buf) {
            throw std::invalid_argument("TaskMessage: null payload buffer");
        }
        if (buf->size() > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("TaskMessage payload exceeds protocol limits");
        }
        return static_cast<uint32_t>(buf->size());
    }

    TaskHeader header_;
    std::unique_ptr<PayloadBufferBase> payload_buffer_;
    std::unique_ptr<PayloadBufferBase> response_buffer_;  ///< Pre-allocated response buffer (optional)
    std::shared_ptr<TaskCompletionSource> completion_source_;  ///< Completion source for async notification (optional)
    std::chrono::steady_clock::time_point created_time_;
};

