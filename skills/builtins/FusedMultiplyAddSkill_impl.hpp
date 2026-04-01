/**
 * @file FusedMultiplyAddSkill_impl.hpp
 * @brief Implementation for FusedMultiplyAddSkill — compute + test data.
 *
 * Computes: result[i] = a[i] + c * b[i]
 */

#include "FusedMultiplyAddSkill_gen.hpp"

namespace TaskMessenger::Skills {

// =========================================================================
// Test support
// =========================================================================

size_t FusedMultiplyAddSkill::get_test_case_count() noexcept { return 3; }

void FusedMultiplyAddSkill::fill_test_request(size_t case_index,
    std::function<RequestPtrs&(const RequestShape&)> allocate_request)
{
    size_t size;
    double c;
    switch (case_index) {
        case 0:  size = 100;  c = 2.0; break;
        case 1:  size = 1000; c = 0.5; break;
        case 2:  size = 10;   c = 0.0; break;
        default: size = 100;  c = 2.0; break;
    }

    auto& p = allocate_request(RequestShape{.n = size});
    for (size_t i = 0; i < size; ++i) {
        p.a[i] = static_cast<double>(i);
        p.b[i] = 1.0;
    }
    *p.c = c;
}

// =========================================================================
// Compute
// =========================================================================

bool FusedMultiplyAddSkill::compute(const RequestPtrs& req, ResponsePtrs& resp) {
    const auto& a = req.a;
    const auto& b = req.b;
    double c = *req.c;
    auto size = a.size();
    auto& result = resp.result;

    if (result.size() != size) {
        return false;
    }

    for (decltype(size) i = 0; i < size; ++i) {
        result[i] = a[i] + c * b[i];
    }

    return true;
}

} // namespace TaskMessenger::Skills
