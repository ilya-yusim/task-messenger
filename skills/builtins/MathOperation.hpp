/**
 * @file MathOperation.hpp
 * @brief Shared enum for math operation types.
 *
 * Used by MathOperationSkill and VectorMathSkill.
 */
#pragma once

#include <cstdint>

namespace TaskMessenger::Skills {

enum MathOperation : int8_t {
    MathOperation_Add      = 0,
    MathOperation_Subtract = 1,
    MathOperation_Multiply = 2,
    MathOperation_Divide   = 3,
};

} // namespace TaskMessenger::Skills
