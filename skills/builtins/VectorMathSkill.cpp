/**
 * @file skills/builtins/VectorMathSkill.cpp
 * @brief Self-contained vector math skill with auto-registration.
 */

#include "skills/registry/SkillRegistration.hpp"
#include "skills/handlers/ISkillHandler.hpp"
#include "skills/registry/SkillIds.hpp"
#include "VectorMathPayload.hpp"

#include <cmath>
#include <limits>

namespace TaskMessenger::Skills {
namespace {

/**
 * @brief Handler for element-wise vector math operations.
 *
 * Receives a VectorMathRequest with two operand vectors and an operation,
 * performs element-wise calculation, and returns a VectorMathResponse.
 *
 * Demonstrates direct buffer write access with CreateUninitializedVector.
 */
class VectorMathHandler : public ISkillHandler {
public:
    [[nodiscard]] bool process(
        std::span<const uint8_t> request,
        std::span<uint8_t> response
    ) override {
        auto req_ptrs = VectorMathPayloadFactory::scatter_request_span(request);
        if (!req_ptrs) {
            return false;
        }

        auto resp_ptrs = VectorMathPayloadFactory::scatter_response_span<true>(response);
        if (!resp_ptrs) {
            return false;
        }

        const auto& a = req_ptrs->a;
        const auto& b = req_ptrs->b;
        auto size = a.size();
        MathOperation op = req_ptrs->operation;
        auto& result = resp_ptrs->result;

        if (result.size() != size) {
            return false;  // Response buffer size mismatch
        }

        // Compute results directly into the buffer
        for (decltype(size) i = 0; i < size; ++i) {
            switch (op) {
                case MathOperation_Add:
                    result[i] = a[i] + b[i];
                    break;
                case MathOperation_Subtract:
                    result[i] = a[i] - b[i];
                    break;
                case MathOperation_Multiply:
                    result[i] = a[i] * b[i];
                    break;
                case MathOperation_Divide:
                    result[i] = (b[i] != 0.0) ? a[i] / b[i] : std::numeric_limits<double>::quiet_NaN();
                    break;
                default:
                    return false;
            }
        }

        return true;
    }
};

// Self-registration: runs before main()
REGISTER_SKILL(
    SkillIds::VectorMath,
    "VectorMath",
    "Performs element-wise vector math operations",
    std::make_unique<VectorMathHandler>(),
    std::make_unique<VectorMathPayloadFactory>(),
    1, 4096, 4096
);

} // anonymous namespace
} // namespace TaskMessenger::Skills
