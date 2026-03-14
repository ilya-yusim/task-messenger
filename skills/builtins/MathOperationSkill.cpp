/**
 * @file skills/builtins/MathOperationSkill.cpp
 * @brief Self-contained scalar math operation skill with auto-registration.
 */

#include "skills/registry/SkillRegistration.hpp"
#include "skills/handlers/ISkillHandler.hpp"
#include "skills/registry/SkillIds.hpp"
#include "MathOperationPayload.hpp"

#include <cmath>
#include <limits>

namespace TaskMessenger::Skills {
namespace {

/**
 * @brief Handler for scalar math operations.
 *
 * Receives a MathOperationRequest with two operands and an operation type,
 * performs the calculation, and returns a MathOperationResponse.
 */
class MathOperationHandler : public ISkillHandler {
public:
    [[nodiscard]] bool process(
        std::span<const uint8_t> request,
        std::span<uint8_t> response
    ) override {
        auto req_ptrs = MathOperationPayloadFactory::scatter_request_span(request);
        if (!req_ptrs) {
            return false;
        }

        double a = *req_ptrs->a;
        double b = *req_ptrs->b;
        MathOperation op = req_ptrs->operation;

        double result = 0.0;
        bool overflow = false;

        switch (op) {
            case MathOperation_Add:
                result = a + b;
                overflow = std::isinf(result);
                break;
            case MathOperation_Subtract:
                result = a - b;
                overflow = std::isinf(result);
                break;
            case MathOperation_Multiply:
                result = a * b;
                overflow = std::isinf(result);
                break;
            case MathOperation_Divide:
                if (b == 0.0) {
                    overflow = true;
                    result = std::numeric_limits<double>::quiet_NaN();
                } else {
                    result = a / b;
                    overflow = std::isinf(result);
                }
                break;
            default:
                return false;  // Unknown operation
        }

        // Write result to pre-allocated response buffer
        auto* resp = flatbuffers::GetMutableRoot<MathOperationResponse>(response.data());
        if (!resp || !resp->mutable_result()) {
            return false;
        }
        
        // Result is stored as single-element vector
        auto* result_vec = resp->mutable_result();
        if (result_vec->size() > 0) {
            const_cast<double*>(result_vec->data())[0] = result;
        }
        resp->mutate_overflow(overflow);

        return true;
    }
};

// Self-registration: runs before main()
REGISTER_SKILL(
    SkillIds::MathOperation,
    "MathOperation",
    "Performs scalar math operations (add, subtract, multiply, divide)",
    std::make_unique<MathOperationHandler>(),
    std::make_unique<MathOperationPayloadFactory>(),
    1, 64, 64
);

} // anonymous namespace
} // namespace TaskMessenger::Skills
