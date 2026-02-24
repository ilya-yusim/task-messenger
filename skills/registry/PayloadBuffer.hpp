/**
 * @file skills/registry/PayloadBuffer.hpp
 * @brief Base classes for owned payload buffers with typed data access.
 *
 * Combines buffer ownership (via FlatBuffers DetachedBuffer) with skill-specific
 * typed pointers into the buffer data. TaskMessage uses the base class interface
 * for transmission while TaskGenerator uses typed access for data population.
 */
#pragma once

#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <span>
#include <vector>

namespace TaskMessenger::Skills {

/**
 * @brief Base class for owned payload buffers.
 * 
 * Provides common interface for buffer access regardless of skill type.
 * TaskMessage uses this interface to access the serialized data for transmission.
 */
class PayloadBufferBase {
public:
    virtual ~PayloadBufferBase() = default;
    
    // Move-only
    PayloadBufferBase() = default;
    PayloadBufferBase(PayloadBufferBase&&) = default;
    PayloadBufferBase& operator=(PayloadBufferBase&&) = default;
    PayloadBufferBase(const PayloadBufferBase&) = delete;
    PayloadBufferBase& operator=(const PayloadBufferBase&) = delete;
    
    /// @brief Get buffer data for serialization/transmission.
    [[nodiscard]] virtual const uint8_t* data() const noexcept = 0;
    
    /// @brief Get mutable buffer data (for in-place modification).
    [[nodiscard]] virtual uint8_t* mutable_data() noexcept = 0;
    
    /// @brief Get buffer size.
    [[nodiscard]] virtual size_t size() const noexcept = 0;
    
    /// @brief Get skill ID this buffer belongs to.
    [[nodiscard]] virtual uint32_t skill_id() const noexcept = 0;
    
    /// @brief Get buffer as const span.
    [[nodiscard]] std::span<const uint8_t> span() const noexcept {
        return std::span<const uint8_t>(data(), size());
    }
    
    /// @brief Get buffer as mutable span.
    [[nodiscard]] std::span<uint8_t> mutable_span() noexcept {
        return std::span<uint8_t>(mutable_data(), size());
    }
    
    /// @brief Check if buffer is empty.
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
};

/**
 * @brief Owned payload buffer with typed data pointers for a specific skill.
 * 
 * @tparam Ptrs Skill-specific pointer struct (e.g., VectorMathBufferPtrs).
 * 
 * Combines:
 * - Ownership of the FlatBuffer memory (via DetachedBuffer)
 * - Typed pointers/spans into that memory for direct data access
 * - Common base interface for TaskMessage
 */
template<typename Ptrs>
class PayloadBuffer : public PayloadBufferBase {
public:
    PayloadBuffer() = default;
    
    PayloadBuffer(flatbuffers::DetachedBuffer&& buffer, Ptrs ptrs, uint32_t skill_id)
        : buffer_(std::move(buffer))
        , ptrs_(ptrs)
        , skill_id_(skill_id) {}
    
    // Move operations
    PayloadBuffer(PayloadBuffer&&) = default;
    PayloadBuffer& operator=(PayloadBuffer&&) = default;
    
    [[nodiscard]] const uint8_t* data() const noexcept override { 
        return buffer_.data(); 
    }
    
    [[nodiscard]] uint8_t* mutable_data() noexcept override { 
        return buffer_.data(); 
    }
    
    [[nodiscard]] size_t size() const noexcept override { 
        return buffer_.size(); 
    }
    
    [[nodiscard]] uint32_t skill_id() const noexcept override {
        return skill_id_;
    }
    
    /// @brief Access typed data pointers for this skill.
    [[nodiscard]] Ptrs& ptrs() noexcept { return ptrs_; }
    [[nodiscard]] const Ptrs& ptrs() const noexcept { return ptrs_; }
    
    /// @brief Convenient arrow access to pointer members (e.g., payload->a[0]).
    [[nodiscard]] Ptrs* operator->() noexcept { return &ptrs_; }
    [[nodiscard]] const Ptrs* operator->() const noexcept { return &ptrs_; }

private:
    flatbuffers::DetachedBuffer buffer_;
    Ptrs ptrs_;
    uint32_t skill_id_ = 0;
};

/**
 * @brief Simple buffer pointers for trivial/one-off payloads.
 * 
 * Used when typed pointer access isn't needed (StringReversal, one-off creates).
 */
struct SimpleBufferPtrs {
    // Empty - no typed access needed for simple payloads
};

/// @brief Payload buffer for simple/trivial skills without typed pointers.
using SimplePayload = PayloadBuffer<SimpleBufferPtrs>;

/**
 * @brief Payload buffer holding raw bytes (for received messages or responses).
 * 
 * Unlike PayloadBuffer which wraps a FlatBuffers DetachedBuffer, this class
 * owns a std::vector<uint8_t> directly. Used by workers for response payloads.
 */
class RawPayload : public PayloadBufferBase {
public:
    RawPayload() = default;
    
    RawPayload(std::vector<uint8_t>&& data, uint32_t skill_id)
        : data_(std::move(data))
        , skill_id_(skill_id) {}
    
    // Move operations
    RawPayload(RawPayload&&) = default;
    RawPayload& operator=(RawPayload&&) = default;
    
    [[nodiscard]] const uint8_t* data() const noexcept override { 
        return data_.data(); 
    }
    
    [[nodiscard]] uint8_t* mutable_data() noexcept override { 
        return data_.data(); 
    }
    
    [[nodiscard]] size_t size() const noexcept override { 
        return data_.size(); 
    }
    
    [[nodiscard]] uint32_t skill_id() const noexcept override {
        return skill_id_;
    }

private:
    std::vector<uint8_t> data_;
    uint32_t skill_id_ = 0;
};

} // namespace TaskMessenger::Skills
