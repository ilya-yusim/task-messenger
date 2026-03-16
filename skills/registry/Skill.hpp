/**
 * @file skills/registry/Skill.hpp
 * @brief Template base class for skill implementations.
 *
 * Skill<Derived> provides the scatter/dispatch boilerplate so skill developers
 * only need to implement the compute() method and static factory methods.
 */
#pragma once

#include "skills/registry/ISkill.hpp"

#include <optional>
#include <span>

namespace TaskMessenger::Skills {

/**
 * @brief Template base class for skills using static polymorphism.
 *
 * @tparam Derived The concrete skill class (CRTP pattern).
 *
 * Derived class must provide:
 * - `struct RequestPtrs` - typed pointers into request buffer
 * - `struct ResponsePtrs` - typed pointers into response buffer
 * - `static constexpr uint32_t kSkillId` - unique skill identifier
 * - `static std::optional<RequestPtrs> scatter_request(std::span<const uint8_t>)`
 * - `static std::optional<ResponsePtrs> scatter_response(std::span<uint8_t>)`
 * - `static std::unique_ptr<PayloadBufferBase> create_response_for_request(std::span<const uint8_t>)`
 * - `bool compute(const RequestPtrs& req, ResponsePtrs& resp)`
 *
 * Example:
 * @code
 * class MySkill : public Skill<MySkill> {
 * public:
 *     struct RequestPtrs { double* input; size_t size; };
 *     struct ResponsePtrs { double* output; };
 *     static constexpr uint32_t kSkillId = SkillIds::MySkill;
 *
 *     static std::optional<RequestPtrs> scatter_request(std::span<const uint8_t> payload);
 *     static std::optional<ResponsePtrs> scatter_response(std::span<uint8_t> payload);
 *     static std::unique_ptr<PayloadBufferBase> create_response_for_request(std::span<const uint8_t>);
 *
 *     bool compute(const RequestPtrs& req, ResponsePtrs& resp) {
 *         // Your computation here - pure logic, no serialization knowledge
 *         return true;
 *     }
 * };
 * @endcode
 */
template<typename Derived>
class Skill : public ISkill {
public:

    // =========================================================================
    // ISkillHandler implementation
    // =========================================================================

    /**
     * @brief Process a task request into a response buffer.
     *
     * Handles scatter logic automatically, then delegates to Derived::compute().
     * Marked final to ensure consistent scatter behavior across all skills.
     *
     * @param request Raw FlatBuffer request bytes.
     * @param response Pre-allocated response buffer to write results into.
     * @return true on success, false on scatter or compute failure.
     */
    [[nodiscard]] bool process(
        std::span<const uint8_t> request,
        std::span<uint8_t> response
    ) final override {
        auto req = Derived::scatter_request(request);
        if (!req) {
            return false;
        }

        auto resp = Derived::scatter_response(response);
        if (!resp) {
            return false;
        }

        return static_cast<Derived*>(this)->compute(*req, *resp);
    }

    // =========================================================================
    // IPayloadFactory implementation
    // =========================================================================

    /**
     * @brief Get the skill ID for this skill.
     * @return The skill ID from Derived::kSkillId.
     */
    [[nodiscard]] uint32_t skill_id() const noexcept final override {
        return Derived::kSkillId;
    }

    /**
     * @brief Create a response buffer sized appropriately for the given request.
     *
     * Delegates to Derived::create_response_for_request().
     *
     * @param request The request payload to size the response for.
     * @return Unique pointer to the response buffer, or nullptr on failure.
     */
    [[nodiscard]] std::unique_ptr<PayloadBufferBase> create_response_buffer_for_request(
        std::span<const uint8_t> request
    ) const override {
        return Derived::create_response_for_request(request);
    }
};

} // namespace TaskMessenger::Skills
