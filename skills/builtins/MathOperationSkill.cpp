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
    [[nodiscard]] uint32_t skill_id() const noexcept override { 
        return SkillIds::MathOperation; 
    }
    
    [[nodiscard]] const char* skill_name() const noexcept override { 
        return "MathOperation"; 
    }

    [[nodiscard]] std::unique_ptr<PayloadBufferBase> process(
        std::span<const uint8_t> payload
    ) override {
        auto decoded = MathOperationPayloadFactory::decode_request(payload);
        if (!decoded) {
            return nullptr;
        }

        double a = *decoded->a;
        double b = *decoded->b;
        MathOperation op = decoded->operation;

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
                return nullptr;  // Unknown operation
        }

        // Create response buffer with result
        auto response = MathOperationPayloadFactory::create_response_buffer(result, overflow);
        return std::make_unique<SimplePayload>(std::move(response));
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
