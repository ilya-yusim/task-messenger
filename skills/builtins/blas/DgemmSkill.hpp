/**
 * @file skills/builtins/blas/DgemmSkill.hpp
 * @brief BLAS Dgemm skill - general matrix-matrix multiply.
 *
 * Computes: C = alpha * A * B + beta * C
 * Delegates to cblas_dgemm() from OpenBLAS or Apple Accelerate.
 */
#pragma once

#include "skills/registry/CompareUtils.hpp"
#include "skills/registry/MatrixSpan.hpp"
#include "skills/registry/Skill.hpp"
#include "skills/registry/PayloadBuffer.hpp"
#include "DgemmSkill_generated.h"

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#else
#include <cblas.h>
#endif

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace TaskMessenger::Skills {

// Forward declaration
class DgemmSkill;

/**
 * @brief Buffer pointers for Dgemm request.
 *
 * Uses MatrixSpan for input matrices (mutable for factory/test fill,
 * CBLAS accepts const double* so no safety issue) and scalar-as-vector
 * pattern for alpha/beta scalars.
 */
struct DgemmRequestPtrs {
    MatrixSpan a;   ///< Input matrix A (m x k)
    MatrixSpan b;   ///< Input matrix B (k x n)
    double* alpha;       ///< Scalar multiplier for A*B
    double* beta;        ///< Scalar multiplier for C
};

/**
 * @brief Buffer pointers for Dgemm response.
 *
 * Uses MatrixSpan for writable output matrix.
 */
struct DgemmResponsePtrs {
    MatrixSpan c;  ///< Output matrix C (m x n)
};

/// @brief Typed payload buffer for Dgemm request.
using DgemmPayload = PayloadBuffer<DgemmRequestPtrs>;

/// @brief Typed payload buffer for Dgemm response.
using DgemmResponseBuffer = PayloadBuffer<DgemmResponsePtrs>;

/**
 * @brief BLAS Dgemm skill implementation.
 *
 * Computes: C = alpha * A * B + beta * C
 * where A is m×k, B is k×n, C is m×n, all row-major.
 */
class DgemmSkill : public Skill<DgemmSkill> {
public:
    // =========================================================================
    // Required type aliases for Skill<Derived>
    // =========================================================================
    using RequestPtrs = DgemmRequestPtrs;
    using ResponsePtrs = DgemmResponsePtrs;

    static constexpr std::string_view kSkillName = "builtin.blas.Dgemm";
    static constexpr std::string_view kSkillDescription = "General matrix-matrix multiply: C = alpha*A*B + beta*C";
    static constexpr uint32_t kSkillVersion = 1;

    // =========================================================================
    // Scatter methods
    // =========================================================================

    [[nodiscard]] static std::optional<RequestPtrs> scatter_request(
        std::span<const uint8_t> payload
    ) {
        auto* request = flatbuffers::GetMutableRoot<DgemmRequest>(
            const_cast<uint8_t*>(payload.data()));
        if (!request) return std::nullopt;

        // Validate all fields present
        if (!request->a() || !request->a_rows() || !request->a_cols()
            || !request->b() || !request->b_rows() || !request->b_cols()
            || !request->alpha() || !request->beta()) {
            return std::nullopt;
        }

        auto* vec_a = request->mutable_a();
        auto* vec_a_rows = request->mutable_a_rows();
        auto* vec_a_cols = request->mutable_a_cols();
        auto* vec_b = request->mutable_b();
        auto* vec_b_rows = request->mutable_b_rows();
        auto* vec_b_cols = request->mutable_b_cols();
        auto* vec_alpha = request->mutable_alpha();
        auto* vec_beta = request->mutable_beta();

        // Scalar-as-vector validation: dimension fields must be single-element
        if (vec_a_rows->size() != 1 || vec_a_cols->size() != 1
            || vec_b_rows->size() != 1 || vec_b_cols->size() != 1
            || vec_alpha->size() != 1 || vec_beta->size() != 1) {
            return std::nullopt;
        }

        int32_t a_rows = vec_a_rows->Get(0);
        int32_t a_cols = vec_a_cols->Get(0);
        int32_t b_rows = vec_b_rows->Get(0);
        int32_t b_cols = vec_b_cols->Get(0);

        // Dimension compatibility: A cols must equal B rows
        if (a_cols != b_rows) return std::nullopt;

        MatrixSpan mat_a{
            .data = std::span<double>(vec_a->data(), vec_a->size()),
            .rows = a_rows,
            .cols = a_cols
        };
        MatrixSpan mat_b{
            .data = std::span<double>(vec_b->data(), vec_b->size()),
            .rows = b_rows,
            .cols = b_cols
        };

        if (!mat_a.valid() || !mat_b.valid()) return std::nullopt;

        return RequestPtrs{
            .a = mat_a,
            .b = mat_b,
            .alpha = vec_alpha->data(),
            .beta = vec_beta->data()
        };
    }

    [[nodiscard]] static std::optional<ResponsePtrs> scatter_response(
        std::span<uint8_t> payload
    ) {
        auto* response = flatbuffers::GetMutableRoot<DgemmResponse>(payload.data());
        if (!response || !response->c() || !response->c_rows() || !response->c_cols()) {
            return std::nullopt;
        }

        auto* vec_c = response->mutable_c();
        auto* vec_c_rows = response->mutable_c_rows();
        auto* vec_c_cols = response->mutable_c_cols();

        if (vec_c_rows->size() != 1 || vec_c_cols->size() != 1) {
            return std::nullopt;
        }

        MatrixSpan mat_c{
            .data = std::span<double>(vec_c->data(), vec_c->size()),
            .rows = vec_c_rows->Get(0),
            .cols = vec_c_cols->Get(0)
        };

        if (!mat_c.valid()) return std::nullopt;

        return ResponsePtrs{ .c = mat_c };
    }

    // =========================================================================
    // Test cases
    // =========================================================================

    /**
     * @brief Create a test request with predefined matrix data.
     *
     * @param case_index Test case selection:
     *   - 0: 2×2 identity × identity = identity (alpha=1, beta=0)
     *   - 1: 2×3 × 3×2 known product
     *   - 2: 3×3 with alpha=2.0, beta=0.0
     * @return DgemmPayload populated with test data.
     */
    [[nodiscard]] static DgemmPayload create_test_request(size_t case_index = 0) {
        switch (case_index) {
            case 1: {
                // 2×3 × 3×2 → 2×2
                auto payload = create_request(2, 3, 2);
                auto& p = payload.ptrs();
                // A = [[1,2,3],[4,5,6]]
                p.a.data[0]=1; p.a.data[1]=2; p.a.data[2]=3;
                p.a.data[3]=4; p.a.data[4]=5; p.a.data[5]=6;
                // B = [[7,8],[9,10],[11,12]]
                p.b.data[0]=7;  p.b.data[1]=8;
                p.b.data[2]=9;  p.b.data[3]=10;
                p.b.data[4]=11; p.b.data[5]=12;
                *p.alpha = 1.0;
                *p.beta = 0.0;
                return payload;
            }
            case 2: {
                // 3×3 with alpha=2.0
                auto payload = create_request(3, 3, 3, 2.0, 0.0);
                auto& p = payload.ptrs();
                for (size_t i = 0; i < 9; ++i) {
                    p.a.data[i] = static_cast<double>(i + 1);
                    p.b.data[i] = static_cast<double>(i + 1);
                }
                return payload;
            }
            default: {
                // 2×2 identity × identity
                auto payload = create_request(2, 2, 2);
                auto& p = payload.ptrs();
                // Identity matrix A
                p.a.data[0]=1; p.a.data[1]=0;
                p.a.data[2]=0; p.a.data[3]=1;
                // Identity matrix B
                p.b.data[0]=1; p.b.data[1]=0;
                p.b.data[2]=0; p.b.data[3]=1;
                *p.alpha = 1.0;
                *p.beta = 0.0;
                return payload;
            }
        }
    }

    [[nodiscard]] static constexpr size_t get_test_case_count() noexcept {
        return 3;
    }

    // =========================================================================
    // Response factory
    // =========================================================================

    [[nodiscard]] static std::unique_ptr<PayloadBufferBase> create_response_for_request(
        std::span<const uint8_t> request
    ) {
        auto req_ptrs = scatter_request(request);
        if (!req_ptrs) return nullptr;
        return std::make_unique<DgemmResponseBuffer>(
            create_response(req_ptrs->a.rows, req_ptrs->b.cols));
    }

    [[nodiscard]] static std::unique_ptr<PayloadBufferBase> create_response_for_request(
        const PayloadBufferBase& request
    ) {
        return create_response_for_request(request.span());
    }

    // =========================================================================
    // Compute
    // =========================================================================

    /**
     * @brief Compute C = alpha * A * B + beta * C via cblas_dgemm.
     */
    bool compute(const RequestPtrs& req, ResponsePtrs& resp) {
        int32_t m = req.a.rows;
        int32_t n = req.b.cols;
        int32_t k = req.a.cols;

        if (resp.c.rows != m || resp.c.cols != n) {
            return false;
        }

        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                    m, n, k,
                    *req.alpha,
                    req.a.ptr(), req.a.ld(),
                    req.b.ptr(), req.b.ld(),
                    *req.beta,
                    resp.c.ptr(), resp.c.ld());

        return true;
    }

    // =========================================================================
    // Factory methods
    // =========================================================================

    /**
     * @brief Create a request buffer with typed matrix access.
     *
     * @param m Rows of A (and C).
     * @param k Columns of A / rows of B.
     * @param n Columns of B (and C).
     * @param alpha Initial alpha value.
     * @param beta Initial beta value.
     * @return DgemmPayload with ownership and typed pointers.
     */
    [[nodiscard]] static DgemmPayload create_request(
        int32_t m, int32_t k, int32_t n,
        double alpha = 1.0, double beta = 0.0
    ) {
        size_t a_size = static_cast<size_t>(m) * k;
        size_t b_size = static_cast<size_t>(k) * n;
        // Estimate: data vectors + 6 scalar-as-vector int32s + 2 scalar-as-vector doubles + overhead
        size_t est = 128 + (a_size + b_size + 2) * sizeof(double) + 6 * sizeof(int32_t);
        flatbuffers::FlatBufferBuilder builder(est);

        double* ptr_a = nullptr;
        double* ptr_b = nullptr;
        int32_t* ptr_a_rows = nullptr;
        int32_t* ptr_a_cols = nullptr;
        int32_t* ptr_b_rows = nullptr;
        int32_t* ptr_b_cols = nullptr;
        double* ptr_alpha = nullptr;
        double* ptr_beta = nullptr;

        auto off_a      = builder.CreateUninitializedVector(a_size, &ptr_a);
        auto off_a_rows = builder.CreateUninitializedVector<int32_t>(1, &ptr_a_rows);
        auto off_a_cols = builder.CreateUninitializedVector<int32_t>(1, &ptr_a_cols);
        auto off_b      = builder.CreateUninitializedVector(b_size, &ptr_b);
        auto off_b_rows = builder.CreateUninitializedVector<int32_t>(1, &ptr_b_rows);
        auto off_b_cols = builder.CreateUninitializedVector<int32_t>(1, &ptr_b_cols);
        auto off_alpha  = builder.CreateUninitializedVector<double>(1, &ptr_alpha);
        auto off_beta   = builder.CreateUninitializedVector<double>(1, &ptr_beta);

        // Set dimension scalars
        *ptr_a_rows = m;
        *ptr_a_cols = k;
        *ptr_b_rows = k;
        *ptr_b_cols = n;
        *ptr_alpha = alpha;
        *ptr_beta = beta;

        auto request = CreateDgemmRequest(builder,
            off_a, off_a_rows, off_a_cols,
            off_b, off_b_rows, off_b_cols,
            off_alpha, off_beta);
        builder.Finish(request);

        auto detached = builder.Release();

        // Re-parse to get stable pointers from the finalized buffer
        auto* req = flatbuffers::GetMutableRoot<DgemmRequest>(detached.data());

        RequestPtrs ptrs{
            .a = MatrixSpan{
                .data = std::span<double>(const_cast<double*>(req->a()->data()), a_size),
                .rows = m,
                .cols = k
            },
            .b = MatrixSpan{
                .data = std::span<double>(const_cast<double*>(req->b()->data()), b_size),
                .rows = k,
                .cols = n
            },
            .alpha = const_cast<double*>(req->alpha()->data()),
            .beta = const_cast<double*>(req->beta()->data())
        };

        return DgemmPayload(std::move(detached), ptrs, kSkillId());
    }

    /**
     * @brief Create a response buffer with typed matrix access.
     *
     * @param m Rows of C.
     * @param n Columns of C.
     * @return DgemmResponseBuffer with ownership and typed pointer.
     */
    [[nodiscard]] static DgemmResponseBuffer create_response(int32_t m, int32_t n) {
        size_t c_size = static_cast<size_t>(m) * n;
        size_t est = 64 + c_size * sizeof(double) + 2 * sizeof(int32_t);
        flatbuffers::FlatBufferBuilder builder(est);

        double* ptr_c = nullptr;
        int32_t* ptr_c_rows = nullptr;
        int32_t* ptr_c_cols = nullptr;

        auto off_c      = builder.CreateUninitializedVector(c_size, &ptr_c);
        auto off_c_rows = builder.CreateUninitializedVector<int32_t>(1, &ptr_c_rows);
        auto off_c_cols = builder.CreateUninitializedVector<int32_t>(1, &ptr_c_cols);

        *ptr_c_rows = m;
        *ptr_c_cols = n;

        // Zero-initialize C (beta=0 makes this unnecessary, but safe default)
        for (size_t i = 0; i < c_size; ++i) ptr_c[i] = 0.0;

        auto response = CreateDgemmResponse(builder, off_c, off_c_rows, off_c_cols);
        builder.Finish(response);

        auto detached = builder.Release();

        auto* resp = flatbuffers::GetMutableRoot<DgemmResponse>(detached.data());

        ResponsePtrs ptrs{
            .c = MatrixSpan{
                .data = std::span<double>(
                    const_cast<double*>(resp->c()->data()), c_size),
                .rows = m,
                .cols = n
            }
        };

        return DgemmResponseBuffer(std::move(detached), ptrs, kSkillId());
    }

    /**
     * @brief Extract read-only result matrix from response payload.
     */
    [[nodiscard]] static std::optional<ConstMatrixSpan> get_result(
        std::span<const uint8_t> payload
    ) noexcept {
        auto* response = flatbuffers::GetRoot<DgemmResponse>(payload.data());
        if (!response || !response->c() || !response->c_rows() || !response->c_cols()) {
            return std::nullopt;
        }
        if (response->c_rows()->size() != 1 || response->c_cols()->size() != 1) {
            return std::nullopt;
        }
        return ConstMatrixSpan{
            .data = std::span<const double>(response->c()->data(), response->c()->size()),
            .rows = response->c_rows()->Get(0),
            .cols = response->c_cols()->Get(0)
        };
    }

    // =========================================================================
    // Verification Support
    // =========================================================================

    [[nodiscard]] static VerificationResult compare_response(
        const ResponsePtrs& computed,
        const ResponsePtrs& worker
    ) {
        return compare_vector(computed.c.data, worker.c.data, "c");
    }
};

} // namespace TaskMessenger::Skills
