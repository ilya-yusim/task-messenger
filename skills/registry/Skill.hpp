/**
 * @file skills/registry/Skill.hpp
 * @brief Template base class for skill implementations.
 *
 * Skill<Derived> provides the scatter/dispatch boilerplate so skill developers
 * only need to implement the compute() method and static factory methods.
 */
#pragma once

#include "skills/registry/ISkill.hpp"
#include "skills/registry/SkillKey.hpp"
#include "skills/registry/VerificationResult.hpp"

#include <concepts>
#include <optional>
#include <span>
#include <string_view>

namespace TaskMessenger::Skills {

/**
 * @brief Template base class for skills using static polymorphism.
 *
 * @tparam Derived The concrete skill class (CRTP pattern).
 *
 * Derived class must provide:
 * - `struct RequestPtrs` - typed pointers into request buffer
 * - `struct ResponsePtrs` - typed pointers into response buffer
 * - `static constexpr std::string_view kSkillName` - namespaced skill name
 * - `static constexpr std::string_view kSkillDescription` - human-readable description
 * - `static constexpr uint32_t kSkillVersion` - schema version
 * - `static std::optional<RequestPtrs> scatter_request(std::span<const uint8_t>)`
 * - `static std::optional<ResponsePtrs> scatter_response(std::span<uint8_t>)`
 * - `static std::unique_ptr<PayloadBufferBase> create_response_for_request(std::span<const uint8_t>)`
 * - `bool compute(const RequestPtrs& req, ResponsePtrs& resp)`
 *
 * The base class provides kSkillId() which derives the numeric ID from kSkillName
 * via SkillKey::from_name(). Derived classes should never declare a kSkillId member.
 *
 * Example:
 * @code
 * class MySkill : public Skill<MySkill> {
 * public:
 *     struct RequestPtrs { double* input; size_t size; };
 *     struct ResponsePtrs { double* output; };
 *     static constexpr std::string_view kSkillName = "mynamespace.MySkill";
 *     static constexpr std::string_view kSkillDescription = "Does something useful";
 *     static constexpr uint32_t kSkillVersion = 1;
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
    // Task processing (ISkill)
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
     * @brief Derive the skill ID from Derived::kSkillName.
     *
     * Provides kSkillId as a static constexpr function rather than requiring
     * derived classes to declare it. The name is the single source of truth.
     */
    static constexpr uint32_t kSkillId() {
        static_assert(
            requires { { Derived::kSkillName } -> std::convertible_to<std::string_view>; },
            "Derived skill must provide: "
            "static constexpr std::string_view kSkillName = \"namespace.SkillName\";"
        );
        return SkillKey::from_name(Derived::kSkillName);
    }

    /**
     * @brief Get the skill ID for this skill.
     * @return The skill ID derived from Derived::kSkillName via SkillKey::from_name().
     */
    [[nodiscard]] uint32_t skill_id() const noexcept final override {
        return kSkillId();
    }

    // =========================================================================
    // ISkill metadata implementation
    // =========================================================================

    /** @brief Returns Derived::kSkillName. */
    [[nodiscard]] std::string_view skill_name() const noexcept final override {
        return Derived::kSkillName;
    }

    /** @brief Returns Derived::kSkillDescription. */
    [[nodiscard]] std::string_view skill_description() const noexcept final override {
        static_assert(
            requires { { Derived::kSkillDescription } -> std::convertible_to<std::string_view>; },
            "Derived skill must provide: "
            "static constexpr std::string_view kSkillDescription = \"...\";" 
        );
        return Derived::kSkillDescription;
    }

    /** @brief Returns Derived::kSkillVersion. */
    [[nodiscard]] uint32_t skill_version() const noexcept final override {
        static_assert(
            requires { { Derived::kSkillVersion } -> std::convertible_to<uint32_t>; },
            "Derived skill must provide: "
            "static constexpr uint32_t kSkillVersion = 1;"
        );
        return Derived::kSkillVersion;
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

    // =========================================================================
    // Test/Verification Support
    // =========================================================================

    /**
     * @brief Create a test request buffer with predefined test data.
     *
     * Delegates to Derived::create_test_request() and wraps in unique_ptr.
     *
     * @param case_index Which test case to create.
     * @return Populated request buffer, or nullptr if case_index invalid.
     */
    [[nodiscard]] std::unique_ptr<PayloadBufferBase> create_test_request_buffer(
        size_t case_index = 0
    ) const override {
        if (case_index >= Derived::get_test_case_count()) {
            return nullptr;
        }
        using PayloadType = decltype(Derived::create_test_request(case_index));
        return std::make_unique<PayloadType>(Derived::create_test_request(case_index));
    }

    /**
     * @brief Get the number of available test cases.
     * @return Number of test cases from Derived::get_test_case_count().
     */
    [[nodiscard]] size_t test_case_count() const noexcept override {
        return Derived::get_test_case_count();
    }

    /**
     * @brief Verify a worker's response against locally computed result.
     *
     * This method orchestrates the full verification pipeline:
     * 1. scatter_request() - parse request into typed pointers
     * 2. create_response_for_request() - allocate local response buffer
     * 3. scatter_response() - get typed pointers into local buffer
     * 4. compute() - compute expected result locally
     * 5. scatter_response() - parse worker's response (const_cast encapsulated here)
     * 6. compare_response() - delegate to skill-specific comparison
     *
     * Skills only need to implement compare_response(computed, worker).
     *
     * @param request The original request payload.
     * @param worker_response The worker's response payload.
     * @return VerificationResult indicating pass/fail with optional message.
     */
    [[nodiscard]] VerificationResult verify_response(
        std::span<const uint8_t> request,
        std::span<const uint8_t> worker_response
    ) const override {
        // 1. Parse request into typed pointers
        auto req = Derived::scatter_request(request);
        if (!req) {
            return VerificationResult::failure("Failed to scatter request");
        }

        // 2. Create local response buffer
        auto local_buf = Derived::create_response_for_request(request);
        if (!local_buf) {
            return VerificationResult::failure("Failed to create local response buffer");
        }

        // 3. Get typed pointers into local buffer (mutable for writing)
        auto local_resp = Derived::scatter_response(local_buf->mutable_span());
        if (!local_resp) {
            return VerificationResult::failure("Failed to scatter local response");
        }

        // 4. Compute expected result locally (const_cast: we need mutable this for compute)
        bool compute_ok = static_cast<Derived*>(const_cast<Skill*>(this))->compute(*req, *local_resp);
        if (!compute_ok) {
            return VerificationResult::failure("Local computation failed");
        }

        // 5. Parse worker's response (const_cast encapsulated - we only read)
        auto worker_resp = Derived::scatter_response(
            std::span<uint8_t>(const_cast<uint8_t*>(worker_response.data()),
                               worker_response.size())
        );
        if (!worker_resp) {
            return VerificationResult::failure("Failed to scatter worker response");
        }

        // 6. Delegate to skill-specific comparison
        return Derived::compare_response(*local_resp, *worker_resp);
    }
};

} // namespace TaskMessenger::Skills
