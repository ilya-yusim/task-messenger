// TaskMessage.hpp - Zero-copy task message with separate header and payload storage
#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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
    uint32_t task_type;  ///< Application-defined task type discriminator
};

/**
 * \brief Zero-copy message buffer carrying header, payload, and timing metadata.
 * \ingroup message_module
 * 
 * Header and payload are stored separately to enable zero-copy construction
 * when the payload string is moved in. The wire_bytes() method returns
 * separate spans for scatter-gather I/O with TCP_NODELAY.
 */
class TaskMessage {
public:
    static constexpr std::size_t kHeaderSize = sizeof(TaskHeader);

    TaskMessage()
        : header_{}
        , payload_{}
        , created_time_(std::chrono::steady_clock::now()) {}

    TaskMessage(const TaskMessage&) = default;
    TaskMessage(TaskMessage&&) noexcept = default;
    TaskMessage& operator=(const TaskMessage&) = default;
    TaskMessage& operator=(TaskMessage&&) noexcept = default;

    /**
     * \brief Construct a TaskMessage taking ownership of the payload string.
     * \param id Task identifier
     * \param type Application-defined task type
     * \param task_data Payload string (moved, zero-copy)
     */
    TaskMessage(uint32_t id, uint32_t type, std::string task_data)
        : header_{id, static_cast<uint32_t>(task_data.size()), type}
        , payload_(std::move(task_data))
        , created_time_(std::chrono::steady_clock::now()) {
        if (payload_.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("TaskMessage payload exceeds protocol limits");
        }
    }

    [[nodiscard]] bool is_valid() const noexcept { return header_.task_id != 0; }

    [[nodiscard]] uint32_t task_id() const noexcept { return header_.task_id; }
    [[nodiscard]] uint32_t task_type() const noexcept { return header_.task_type; }
    [[nodiscard]] uint32_t body_size() const noexcept { return header_.body_size; }

    [[nodiscard]] TaskHeader header_view() const noexcept { return header_; }

    [[nodiscard]] std::string_view payload() const noexcept {
        return std::string_view(payload_);
    }

    [[nodiscard]] std::span<const std::uint8_t> payload_bytes() const noexcept {
        return {reinterpret_cast<const std::uint8_t*>(payload_.data()), payload_.size()};
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
            {reinterpret_cast<const std::uint8_t*>(payload_.data()), payload_.size()}
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

private:
    TaskHeader header_;
    std::string payload_;
    std::chrono::steady_clock::time_point created_time_;
};

