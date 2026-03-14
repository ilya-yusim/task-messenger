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
    [[nodiscard]] bool process(
        std::span<const uint8_t> request,
        std::span<uint8_t> response
    ) override {
        auto req_ptrs = FusedMultiplyAddPayloadFactory::scatter_request_span(request);
        if (!req_ptrs) {
            return false;
        }

        auto resp_ptrs = FusedMultiplyAddPayloadFactory::scatter_response_span<true>(response);
        if (!resp_ptrs) {
            return false;
        }

        const auto& a = req_ptrs->a;
        const auto& b = req_ptrs->b;
        double c = *req_ptrs->c;
        auto size = a.size();
        auto& result = resp_ptrs->result;

        if (result.size() != size) {
            return false;  // Response buffer size mismatch
        }

        // Compute FMA: result[i] = a[i] + c * b[i]
        for (decltype(size) i = 0; i < size; ++i) {
            result[i] = a[i] + c * b[i];
        }

        return true;
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

} // anonymous namespace
} // namespace TaskMessenger::Skills
