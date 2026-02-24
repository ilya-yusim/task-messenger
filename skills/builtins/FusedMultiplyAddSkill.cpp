/**
 * @file skills/builtins/FusedMultiplyAddSkill.cpp
 * @brief Self-contained fused multiply-add skills with auto-registration.
 *
 * Computes: result[i] = a[i] + c * b[i]
 */

#include "skills/registry/SkillRegistration.hpp"
#include "skills/handlers/ISkillHandler.hpp"
#include "skills/registry/SkillIds.hpp"
#include "FusedMultiplyAddPayload.hpp"

namespace TaskMessenger::Skills {
namespace {

/**
 * @brief Handler for fused multiply-add operations (scalar-as-vector pattern).
 *
 * Receives a FusedMultiplyAddRequest with vectors a, b and scalar c (as single-element vector),
 * computes result[i] = a[i] + c * b[i], and returns a FusedMultiplyAddResponse.
 */
class FusedMultiplyAddHandler : public ISkillHandler {
public:
    [[nodiscard]] uint32_t skill_id() const noexcept override { 
        return SkillIds::FusedMultiplyAdd; 
    }
    
    [[nodiscard]] const char* skill_name() const noexcept override { 
        return "FusedMultiplyAdd"; 
    }

    [[nodiscard]] std::unique_ptr<PayloadBufferBase> process(
        std::span<const uint8_t> payload
    ) override {
        auto decoded = FusedMultiplyAddPayloadFactory::decode_request(payload);
        if (!decoded) {
            return nullptr;
        }

        const auto& a = decoded->a;
        const auto& b = decoded->b;
        double c = *decoded->c;
        auto size = a.size();

        // Create response buffer with direct write access
        auto response = FusedMultiplyAddPayloadFactory::create_response_buffer(size);
        auto& result = response.ptrs().result;

        // Compute FMA: result[i] = a[i] + c * b[i]
        for (decltype(size) i = 0; i < size; ++i) {
            result[i] = a[i] + c * b[i];
        }

        return std::make_unique<FusedMultiplyAddResponseBuffer>(std::move(response));
    }
};

/**
 * @brief Handler for fused multiply-add with true scalar field.
 *
 * Uses FusedMultiplyAddMutableRequest where scalar_c is a true scalar field.
 */
class FusedMultiplyAddMutableHandler : public ISkillHandler {
public:
    [[nodiscard]] uint32_t skill_id() const noexcept override { 
        return SkillIds::FusedMultiplyAddMutable; 
    }
    
    [[nodiscard]] const char* skill_name() const noexcept override { 
        return "FusedMultiplyAddMutable"; 
    }

    [[nodiscard]] std::unique_ptr<PayloadBufferBase> process(
        std::span<const uint8_t> payload
    ) override {
        auto decoded = FusedMultiplyAddMutablePayloadFactory::decode_request(payload);
        if (!decoded) {
            return nullptr;
        }

        const auto& a = decoded->a;
        const auto& b = decoded->b;
        double c = *decoded->c;
        auto size = a.size();

        // Create response buffer with direct write access
        auto response = FusedMultiplyAddMutablePayloadFactory::create_response_buffer(size);
        auto& result = response.ptrs().result;

        // Compute FMA: result[i] = a[i] + c * b[i]
        for (decltype(size) i = 0; i < size; ++i) {
            result[i] = a[i] + c * b[i];
        }

        return std::make_unique<FusedMultiplyAddResponseBuffer>(std::move(response));
    }
};

// Self-registration: runs before main()
REGISTER_SKILL(
    SkillIds::FusedMultiplyAdd,
    "FusedMultiplyAdd",
    "Computes result[i] = a[i] + c * b[i] with scalar-as-vector pattern",
    std::make_unique<FusedMultiplyAddHandler>(),
    std::make_unique<FusedMultiplyAddPayloadFactory>(),
    1, 4096, 4096
);

REGISTER_SKILL(
    SkillIds::FusedMultiplyAddMutable,
    "FusedMultiplyAddMutable",
    "Computes result[i] = a[i] + c * b[i] with true scalar field",
    std::make_unique<FusedMultiplyAddMutableHandler>(),
    std::make_unique<FusedMultiplyAddMutablePayloadFactory>(),
    1, 4096, 4096
);

} // anonymous namespace
} // namespace TaskMessenger::Skills
