// TaskMessage.hpp - Contiguous task message buffer combining header and payload
#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
 * \brief Contiguous message buffer carrying header, payload, and timing metadata.
 * \ingroup message_module
 */
class TaskMessage {
public:
    TaskMessage()
        : storage_(kHeaderSize, 0)
        , created_time_(std::chrono::steady_clock::now()) {}
    TaskMessage(const TaskMessage&) = default;
    TaskMessage(TaskMessage&&) noexcept = default;
    TaskMessage& operator=(const TaskMessage&) = default;
    TaskMessage& operator=(TaskMessage&&) noexcept = default;

    TaskMessage(uint32_t id, uint32_t type, std::string task_data)
        : storage_(kHeaderSize + task_data.size())
        , created_time_(std::chrono::steady_clock::now()) {
        validate_payload_size(task_data.size());
        TaskHeader header{};
        header.task_id = id;
        header.task_type = type;
        header.body_size = static_cast<uint32_t>(task_data.size());
        std::memcpy(storage_.data(), &header, kHeaderSize);
        if (!task_data.empty()) {
            std::memcpy(storage_.data() + kHeaderSize, task_data.data(), task_data.size());
        }
    }

    [[nodiscard]] bool is_valid() const { return read_header().task_id != 0; }

    [[nodiscard]] uint32_t task_id() const { return read_header().task_id; }
    void set_task_id(uint32_t id) {
        auto header = read_header();
        header.task_id = id;
        write_header(header);
    }

    [[nodiscard]] uint32_t task_type() const { return read_header().task_type; }
    void set_task_type(uint32_t type) {
        auto header = read_header();
        header.task_type = type;
        write_header(header);
    }

    [[nodiscard]] uint32_t body_size() const { return read_header().body_size; }

    [[nodiscard]] TaskHeader header_view() const { return read_header(); }

    [[nodiscard]] std::string_view payload() const {
        const auto payload_size = storage_.size() - kHeaderSize;
        return std::string_view(reinterpret_cast<const char*>(storage_.data() + kHeaderSize), payload_size);
    }
    void set_payload(std::string payload) {
        validate_payload_size(payload.size());
        const auto payload_size = payload.size();
        storage_.resize(kHeaderSize + payload_size);
        if (payload_size > 0) {
            std::memcpy(storage_.data() + kHeaderSize, payload.data(), payload_size);
        }
        auto header = read_header();
        header.body_size = static_cast<uint32_t>(payload_size);
        write_header(header);
    }

    void append_to_payload(std::span<const std::uint8_t> tail) {
        if (tail.empty()) {
            return;
        }
        validate_append_size(tail.size());
        const auto original_size = storage_.size() - kHeaderSize;
        storage_.resize(storage_.size() + tail.size());
        std::memcpy(storage_.data() + kHeaderSize + original_size, tail.data(), tail.size());

        auto header = read_header();
        header.body_size = static_cast<uint32_t>(original_size + tail.size());
        write_header(header);
    }

    void append_to_payload(std::string_view tail) {
        if (tail.empty()) {
            return;
        }
        append_to_payload(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(tail.data()), tail.size()});
    }

    void append_to_payload(std::string tail) {
        append_to_payload(std::string_view(tail));
    }

    [[nodiscard]] std::span<const std::uint8_t> payload_bytes() const {
        return {storage_.data() + kHeaderSize, storage_.size() - kHeaderSize};
    }

    [[nodiscard]] std::span<const std::uint8_t> wire_bytes() const {
        return {storage_.data(), storage_.size()};
    }

    [[nodiscard]] std::chrono::milliseconds get_age() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - created_time_);
    }

private:
    [[nodiscard]] TaskHeader read_header() const {
        TaskHeader header{};
        if (storage_.size() >= kHeaderSize) {
            std::memcpy(&header, storage_.data(), kHeaderSize);
        }
        return header;
    }

    void write_header(const TaskHeader& header) {
        if (storage_.size() < kHeaderSize) {
            storage_.resize(kHeaderSize);
        }
        std::memcpy(storage_.data(), &header, kHeaderSize);
    }

    static void validate_payload_size(std::size_t size) {
        if (size > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("TaskMessage payload exceeds protocol limits");
        }
    }

    void validate_append_size(std::size_t append_size) const {
        const auto current_size = storage_.size() - kHeaderSize;
        if (append_size > std::numeric_limits<uint32_t>::max() - current_size) {
            throw std::length_error("TaskMessage append exceeds protocol limits");
        }
    }

private:
    static constexpr std::size_t kHeaderSize = sizeof(TaskHeader);
    std::vector<std::uint8_t> storage_{};
    std::chrono::steady_clock::time_point created_time_{std::chrono::steady_clock::now()};
};
