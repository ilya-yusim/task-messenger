/**
 * @file skills/registry/MatrixSpan.hpp
 * @brief Lightweight matrix view over contiguous row-major data.
 *
 * Provides typed access to flat double arrays with dimension metadata,
 * bridging FlatBuffer vectors (stored as flat [double]) with BLAS routines
 * that expect pointer + leading dimension parameters.
 *
 * Used by scatter methods to wrap flat FlatBuffer vectors into dimensioned
 * matrix views. Not a FlatBuffer type — schemas remain flat vectors with
 * scalar-as-vector dimension fields.
 */
#pragma once

#include <cstdint>
#include <span>

namespace TaskMessenger::Skills {

/**
 * @brief Read-only view of a row-major matrix in contiguous memory.
 *
 * Used in request scatter methods where input matrices are read-only.
 * Provides CBLAS-compatible accessors: ptr() for data pointer, ld() for
 * leading dimension.
 */
struct ConstMatrixSpan {
    std::span<const double> data;  ///< Flat row-major elements
    int32_t rows = 0;             ///< Number of rows
    int32_t cols = 0;             ///< Number of columns

    /// @brief Element access (row-major): data[r * cols + c]
    [[nodiscard]] const double& operator()(int32_t r, int32_t c) const {
        return data[static_cast<size_t>(r) * cols + c];
    }

    /// @brief Check that data length matches declared dimensions.
    [[nodiscard]] bool valid() const {
        return rows > 0 && cols > 0
            && data.size() == static_cast<size_t>(rows) * cols;
    }

    /// @brief Raw pointer for CBLAS calls.
    [[nodiscard]] const double* ptr() const { return data.data(); }

    /// @brief Leading dimension (row-major = number of columns).
    [[nodiscard]] int32_t ld() const { return cols; }
};

/**
 * @brief Mutable view of a row-major matrix in contiguous memory.
 *
 * Used in response scatter methods where output matrices are writable.
 * Provides CBLAS-compatible accessors for output parameters.
 */
struct MatrixSpan {
    std::span<double> data;  ///< Flat row-major elements (mutable)
    int32_t rows = 0;       ///< Number of rows
    int32_t cols = 0;       ///< Number of columns

    /// @brief Mutable element access (row-major): data[r * cols + c]
    [[nodiscard]] double& operator()(int32_t r, int32_t c) {
        return data[static_cast<size_t>(r) * cols + c];
    }

    /// @brief Const element access.
    [[nodiscard]] const double& operator()(int32_t r, int32_t c) const {
        return data[static_cast<size_t>(r) * cols + c];
    }

    /// @brief Check that data length matches declared dimensions.
    [[nodiscard]] bool valid() const {
        return rows > 0 && cols > 0
            && data.size() == static_cast<size_t>(rows) * cols;
    }

    /// @brief Mutable raw pointer for CBLAS output parameters.
    [[nodiscard]] double* ptr() { return data.data(); }

    /// @brief Const raw pointer.
    [[nodiscard]] const double* ptr() const { return data.data(); }

    /// @brief Leading dimension (row-major = number of columns).
    [[nodiscard]] int32_t ld() const { return cols; }
};

} // namespace TaskMessenger::Skills
