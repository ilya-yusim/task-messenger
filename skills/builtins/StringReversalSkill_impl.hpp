/**
 * @file StringReversalSkill_impl.hpp
 * @brief Implementation for StringReversalSkill — compute + test data.
 *
 * Reverses the input string.
 */

#include "StringReversalSkill_gen.hpp"

#include <string>
#include <string_view>

namespace TaskMessenger::Skills {

// =========================================================================
// Test support
// =========================================================================

size_t StringReversalSkill::get_test_case_count() noexcept { return 3; }

void StringReversalSkill::fill_test_request(size_t case_index,
    std::function<RequestPtrs&(const RequestShape&)> allocate_request)
{
    static const std::string long_str = []() {
        std::string s;
        s.reserve(500);
        for (int i = 0; i < 500; ++i) {
            s.push_back('A' + (i % 26));
        }
        return s;
    }();

    std::string_view data;
    switch (case_index) {
        case 0:  data = "Hello, World!"; break;
        case 1:  data = long_str;        break;
        case 2:  data = "X";             break;
        default: data = "Hello, World!"; break;
    }

    auto& p = allocate_request(RequestShape{.len = data.size()});
    for (size_t i = 0; i < data.size(); ++i) {
        p.input[i] = static_cast<int8_t>(data[i]);
    }
}

// =========================================================================
// Compute
// =========================================================================

bool StringReversalSkill::compute(const RequestPtrs& req, ResponsePtrs& resp) {
    auto original_length = static_cast<uint32_t>(req.input_length);

    if (resp.output_length != original_length) {
        return false;
    }

    for (uint32_t i = 0; i < original_length; ++i) {
        resp.output[i] = req.input[original_length - 1 - i];
    }

    *resp.original_length = original_length;

    return true;
}

} // namespace TaskMessenger::Skills
