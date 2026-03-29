/**
 * @file VectorMathSkill_impl.hpp
 * @brief Implementation for VectorMathSkill — compute + test data.
 *
 * Performs element-wise vector math operations (add, subtract, multiply, divide).
 */

#include "VectorMathSkill_gen.hpp"
#include "MathOperation.hpp"

#include <cmath>
#include <limits>

namespace TaskMessenger::Skills {

// =========================================================================
// Test support
// =========================================================================

size_t VectorMathSkill::get_test_case_count() noexcept { return 3; }

void VectorMathSkill::fill_test_request(size_t case_index,
    std::function<RequestPtrs&(const RequestShape&)> allocate_request)
{
    size_t size;
    int8_t op;
    switch (case_index) {
        case 0:  size = 100;  op = MathOperation_Add;      break;
        case 1:  size = 1000; op = MathOperation_Multiply;  break;
        case 2:  size = 10;   op = MathOperation_Divide;    break;
        default: size = 100;  op = MathOperation_Add;       break;
    }

    auto& p = allocate_request(RequestShape{.n = size});
    for (size_t i = 0; i < size; ++i) {
        p.a[i] = static_cast<double>(i + 1);
        p.b[i] = static_cast<double>(i + 1);
    }
    *p.operation = op;
}

// =========================================================================
// Compute
// =========================================================================

bool VectorMathSkill::compute(const RequestPtrs& req, ResponsePtrs& resp) {
    const auto& a = req.a;
    const auto& b = req.b;
    auto size = a.size();
    MathOperation op = static_cast<MathOperation>(*req.operation);
    auto& result = resp.result;

    if (result.size() != size) {
        return false;
    }

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
                result[i] = (b[i] != 0.0) ? a[i] / b[i]
                    : std::numeric_limits<double>::quiet_NaN();
                break;
            default:
                return false;
        }
    }

    return true;
}

} // namespace TaskMessenger::Skills
