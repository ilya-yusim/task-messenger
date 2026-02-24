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
    [[nodiscard]] uint32_t skill_id() const noexcept override { 
        return SkillIds::VectorMath; 
    }
    
    [[nodiscard]] const char* skill_name() const noexcept override { 
        return "VectorMath"; 
    }

    [[nodiscard]] std::unique_ptr<PayloadBufferBase> process(
        std::span<const uint8_t> payload
    ) override {
        auto decoded = VectorMathPayloadFactory::decode_request(payload);
        if (!decoded) {
            return nullptr;
        }

        const auto& a = decoded->a;
        const auto& b = decoded->b;
        auto size = a.size();
        MathOperation op = decoded->operation;

        // Create response buffer with direct write access
        auto response = VectorMathPayloadFactory::create_response_buffer(size);
        auto& result = response.ptrs().result;

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
                    return nullptr;
            }
        }

        return std::make_unique<VectorMathResponseBuffer>(std::move(response));
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
