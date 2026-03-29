/**
 * @file MathOperationSkill_impl.hpp
 * @brief Implementation for MathOperationSkill — compute + test data.
 *
 * Performs scalar math operations with overflow detection.
 */

#include "MathOperationSkill_gen.hpp"
#include "MathOperation.hpp"

#include <cmath>
#include <limits>

namespace TaskMessenger::Skills {

// =========================================================================
// Test support
// =========================================================================

size_t MathOperationSkill::get_test_case_count() noexcept { return 3; }

void MathOperationSkill::fill_test_request(size_t case_index,
    std::function<RequestPtrs&(const RequestShape&)> allocate_request)
{
    auto& p = allocate_request(RequestShape{});

    switch (case_index) {
        case 0:  // Basic add
            *p.a = 42.0;
            *p.b = 13.0;
            *p.operation = MathOperation_Add;
            break;
        case 1:  // Overflow test
            *p.a = 1e308;
            *p.b = 1e308;
            *p.operation = MathOperation_Add;
            break;
        case 2:  // Division by zero
            *p.a = 10.0;
            *p.b = 0.0;
            *p.operation = MathOperation_Divide;
            break;
        default:
            *p.a = 0.0;
            *p.b = 0.0;
            *p.operation = MathOperation_Add;
            break;
    }
}

// =========================================================================
// Compute
// =========================================================================

bool MathOperationSkill::compute(const RequestPtrs& req, ResponsePtrs& resp) {
    double a = *req.a;
    double b = *req.b;
    MathOperation op = static_cast<MathOperation>(*req.operation);

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
            return false;
    }

    *resp.result = result;
    *resp.overflow = overflow ? 1 : 0;

    return true;
}

} // namespace TaskMessenger::Skills
