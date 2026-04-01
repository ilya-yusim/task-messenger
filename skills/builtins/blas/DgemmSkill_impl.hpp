/**
 * @file DgemmSkill_impl.hpp
 * @brief Implementation for DgemmSkill — compute + test data.
 *
 * Includes the generated _gen.hpp and defines:
 *   - get_test_case_count()
 *   - fill_test_request()  — populates test data via allocate_request callback
 *   - compute()            — the actual BLAS call
 */

#include "DgemmSkill_gen.hpp"

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#else
#include <cblas.h>
#endif

namespace TaskMessenger::Skills {

// =========================================================================
// Test support
// =========================================================================

size_t DgemmSkill::get_test_case_count() noexcept { return 3; }

void DgemmSkill::fill_test_request(size_t case_index,
    std::function<RequestPtrs&(const RequestShape&)> allocate_request)
{
    switch (case_index) {
        case 0: {
            // 2×2 identity × identity
            RequestShape shape;
            shape.m = 2;
            shape.k = 2;
            shape.n = 2;
            auto& p = allocate_request(shape);

            p.a.data[0]=1; p.a.data[1]=0;
            p.a.data[2]=0; p.a.data[3]=1;
            p.b.data[0]=1; p.b.data[1]=0;
            p.b.data[2]=0; p.b.data[3]=1;
            *p.alpha = 1.0;
            *p.beta = 0.0;
            break;
        }
        case 1: {
            // 2×3 × 3×2 → 2×2
            RequestShape shape;
            shape.m = 2;  // A rows
            shape.k = 3;  // inner dimension
            shape.n = 2;  // B cols
            auto& p = allocate_request(shape);

            // A = [[1,2,3],[4,5,6]]
            p.a.data[0]=1; p.a.data[1]=2; p.a.data[2]=3;
            p.a.data[3]=4; p.a.data[4]=5; p.a.data[5]=6;
            // B = [[7,8],[9,10],[11,12]]
            p.b.data[0]=7;  p.b.data[1]=8;
            p.b.data[2]=9;  p.b.data[3]=10;
            p.b.data[4]=11; p.b.data[5]=12;
            *p.alpha = 1.0;
            *p.beta = 0.0;
            break;
        }
        case 2: {
            // 3×3 with alpha=2.0
            RequestShape shape;
            shape.m = 3;
            shape.k = 3;
            shape.n = 3;
            auto& p = allocate_request(shape);

            for (size_t i = 0; i < 9; ++i) {
                p.a.data[i] = static_cast<double>(i + 1);
                p.b.data[i] = static_cast<double>(i + 1);
            }
            *p.alpha = 2.0;
            *p.beta = 0.0;
            break;
        }
    }
}

// =========================================================================
// Compute
// =========================================================================

bool DgemmSkill::compute(const RequestPtrs& req, ResponsePtrs& resp) {
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

} // namespace TaskMessenger::Skills
