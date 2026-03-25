# Plan: Math Library Integration as Task-Messenger Skills

## TL;DR

Integrate publicly available math libraries into the task-messenger skill framework, exposing computational operations (dense/sparse linear algebra, FFT, statistics, ODE solvers) as skills that workers can execute. The existing `compute()` + FlatBuffer scatter pattern already aligns perfectly with how math libraries consume data — contiguous `double*` arrays with dimension parameters. The recommended approach is a tiered library strategy: **OpenBLAS** for BLAS/LAPACK, **Eigen** for C++ linear algebra, **FFTW** for FFT, **GSL** for statistics, **SuiteSparse** for sparse solvers, and **SUNDIALS** for ODE/PDE.

---

## Context: How Math Libraries Fit the Architecture

The task-messenger skill `compute()` pattern receives data via `std::span<double>` (zero-copy FlatBuffer access) and writes results directly into pre-allocated response buffers. This maps 1:1 to how BLAS-style libraries expect inputs: contiguous memory pointers with dimension parameters.

**Concrete example — the existing `FusedMultiplyAddSkill::compute()` is literally a manual reimplementation of `cblas_daxpy()`:**
- Current: `for (i) result[i] = a[i] + c * b[i]`
- BLAS: `cblas_dcopy(n, a, 1, result, 1); cblas_daxpy(n, c, b, 1, result, 1)`

---

## BLAS and MKL: Fit Assessment

### BLAS (Basic Linear Algebra Subprograms)

**Verdict: Excellent fit.**

- BLAS is an API *specification*, not a single library — multiple implementations exist
- The CBLAS C interface takes raw `double*` pointers and integer dimensions — exactly what `std::span<double>::data()` provides
- Three levels map naturally to increasingly complex skills:
  - **Level 1** (vector-vector): `ddot`, `daxpy`, `dnrm2`, `dscal` — direct upgrades of existing VectorMath/FMA skills
  - **Level 2** (matrix-vector): `dgemv`, `dtrsv` — new linear transform skills
  - **Level 3** (matrix-matrix): `dgemm`, `dtrsm` — new matrix multiply/solve skills
- Available on all platforms, optimized per architecture

### Intel MKL (oneAPI Math Kernel Library)

**Verdict: Partial fit — fast on x86, but NOT portable to ARM64.**

- Provides BLAS, LAPACK, FFT, RNG, sparse solvers in one package
- Highly optimized for Intel CPUs, decent on AMD x86_64
- **Not available for ARM64** (macOS Apple Silicon, Linux ARM servers) — this is a dealbreaker given the platform requirements
- Large binary footprint (~200MB+)
- Free to use/redistribute since 2017, but proprietary
- **Recommendation**: Don't use MKL as the primary backend. Instead, use portable libraries (OpenBLAS, FFTW) with BLAS-standard APIs, which allows MKL to be swapped in as an optional x86 optimization later if desired

---

## Recommended Libraries

### Tier 1: Core (Start Here)

| Library | Domain | License | Why |
|---------|--------|---------|-----|
| **OpenBLAS** | Dense linear algebra (BLAS + LAPACK) | BSD-3 | Optimized per CPU microarch, mature ARM64 support, standard CBLAS/LAPACKE API |
| **Eigen** | C++ linear algebra, decompositions | MPL2 | Header-only (trivial integration), can use OpenBLAS as backend, great C++ API for `compute()` methods |
| **FFTW** | Fast Fourier Transform | GPL-2.0 | Gold standard for FFT, SIMD auto-tuning, all platforms |

#### OpenBLAS

- Drop-in replacement for Netlib BLAS + LAPACK, but optimized per CPU microarchitecture
- ARM64 support is mature (critical for macOS Apple Silicon)
- Provides the standard CBLAS + LAPACKE C interfaces
- **Skill mapping**: Every BLAS/LAPACK routine becomes a natural skill — `dgemm` → MatrixMultiply, `dgesv` → LinearSolve, `dgesvd` → SVD, etc.
- On macOS, can alternatively use Apple Accelerate (built-in, zero-cost) as the BLAS provider

#### Eigen

- Header-only C++ template library — no linking required, trivial to integrate
- Dense and sparse linear algebra, geometry, decompositions
- Expression templates for lazy evaluation and optimization
- Can optionally use OpenBLAS/MKL as backend for large operations (>~100 elements)
- Nice C++ API for implementing `compute()` methods cleanly
- **Use case**: Higher-level operations, matrix decompositions where you want C++ convenience

#### FFTW

- The gold standard for FFT computation
- 1D, 2D, 3D transforms; real and complex; arbitrary sizes
- Highly optimized with SIMD (SSE, AVX, NEON) auto-tuning
- **Skill mapping**: FFT1D, FFT2D, IFFT, RealFFT, ConvolutionViaFFT

### Tier 2: Specialized Domains

| Library | Domain | License | Why |
|---------|--------|---------|-----|
| **GSL** | Statistics, distributions, special functions, interpolation, numerical integration, RNG | GPL-3.0 | Extremely comprehensive, C API with `double*` params — natural skill fit |
| **SuiteSparse** | Sparse direct solvers (CHOLMOD, UMFPACK, SPQR) | Mixed LGPL/GPL/BSD | Industry standard, used by MATLAB/Julia/SciPy |
| **SUNDIALS** | ODE/DAE solvers, nonlinear solvers | BSD-3 | Production-quality from LLNL; CVODE, ARKODE, IDA, KINSOL |

#### GSL (GNU Scientific Library)

- Extremely comprehensive scientific computing toolkit
- Statistics: mean, variance, correlation, regression, histograms
- Distributions: Gaussian, Poisson, Chi-squared, ~30+ distributions
- Special functions: Bessel, Legendre, gamma, error functions
- Numerical integration, differentiation, interpolation
- Random number generation (Mersenne Twister, etc.)
- C API with `double*` parameters — natural fit for the skill pattern
- **Skill mapping**: StatsSummary, Distribution sampling, NumericalIntegration, Interpolation

#### SuiteSparse

- The standard for sparse direct solvers in scientific computing
- CHOLMOD (Cholesky), UMFPACK (LU), SPQR (QR), AMD/COLAMD (ordering)
- Used by MATLAB, Julia, R, SciPy internally
- **Skill mapping**: SparseSolve, SparseFactorize, SparseLeastSquares
- Note: Sparse matrix serialization in FlatBuffers would use CSR/CSC format (three vectors: values, row_indices, col_pointers)

#### SUNDIALS (from Lawrence Livermore National Lab)

- CVODE: stiff and non-stiff ODE integration
- ARKODE: adaptive Runge-Kutta methods
- IDA: DAE (differential-algebraic equation) solver
- KINSOL: nonlinear algebraic system solver
- Production-quality, used in major simulation codes
- **Note**: ODE skills would need a different pattern — the user defines the RHS function, which would need to be either built-in or specified as a formula

### Tier 3: Additional Options

| Library | Domain | License | Notes |
|---------|--------|---------|-------|
| **Armadillo** | C++ linear algebra (MATLAB-like syntax) | Apache-2.0 | Wraps BLAS/LAPACK with nice C++ API |
| **Boost.Math** | Special functions, distributions | BSL-1.0 | Header-only, extensive |
| **ALGLIB** | Numerical analysis, data processing | GPL/Commercial | Comprehensive but dual-licensed |
| **xtensor** | NumPy-like C++ tensors | BSD-3 | Header-only, modern C++ |

---

## Platform-Specific BLAS Backend Strategy

Use a **swappable BLAS backend** — all provide the same CBLAS/LAPACKE API, so skill code is identical:

| Platform | Recommended Provider | Notes |
|----------|---------------------|-------|
| macOS (ARM64 Apple Silicon) | **Apple Accelerate** | Built-in, zero-cost, highly optimized for M-series |
| macOS (x86_64) | Apple Accelerate | Works on both architectures |
| Linux (x86_64) | **OpenBLAS** | Standard, widely packaged |
| Linux (ARM64) | **OpenBLAS** | Mature ARM64 support |
| Windows (x86_64) | **OpenBLAS** | Pre-built binaries available |

Meson snippet:
```meson
# Platform-adaptive BLAS
if host_machine.system() == 'darwin'
  blas_dep = dependency('accelerate', required: false)
  if not blas_dep.found()
    blas_dep = dependency('openblas', required: true)
  endif
else
  blas_dep = dependency('openblas', required: true)
endif
```

---

## Skill Mapping to Libraries

| Skill | Library | Key Function | Schema Concept |
|-------|---------|-------------|----------------|
| `builtin.blas.Dgemm` | OpenBLAS | `cblas_dgemm()` | Req: `a/b:[double], m/n/k:[int32], alpha/beta:[double]` → Resp: `c:[double]` |
| `builtin.blas.Daxpy` | OpenBLAS | `cblas_daxpy()` | Req: `x/y:[double], alpha:[double]` → Resp: `result:[double]` |
| `builtin.blas.Ddot` | OpenBLAS | `cblas_ddot()` | Req: `x/y:[double]` → Resp: `result:[double]` |
| `builtin.lapack.Solve` | OpenBLAS | `LAPACKE_dgesv()` | Req: `A/b:[double], n:[int32]` → Resp: `x:[double]` |
| `builtin.lapack.SVD` | OpenBLAS | `LAPACKE_dgesvd()` | Req: `A:[double], m/n:[int32]` → Resp: `U/S/Vt:[double]` |
| `builtin.lapack.Eigenvalues` | OpenBLAS | `LAPACKE_dsyev()` | Req: `A/n` → Resp: `eigenvalues/eigenvectors:[double]` |
| `builtin.fft.FFT1D` | FFTW | `fftw_execute()` | Req: `real/imag:[double]` → Resp: `real/imag:[double]` |
| `builtin.stats.Summary` | GSL | `gsl_stats_*` | Req: `data:[double]` → Resp: `mean/var/skew/kurtosis:[double]` |
| `builtin.stats.LinearRegression` | GSL | `gsl_fit_linear()` | Req: `x/y:[double]` → Resp: `slope/intercept/r_squared:[double]` |
| `builtin.sparse.Solve` | SuiteSparse | `umfpack_di_solve()` | Req: `values/row_ind/col_ptr, b` → Resp: `x:[double]` |
| `builtin.ode.Integrate` | SUNDIALS | `CVode()` | Req: `y0/t_span/params:[double]` → Resp: `t/y:[double]` |

---

## Meson Integration Approaches

1. **System dependency** (preferred for widely-available libs):
   - `dependency('openblas')`, `dependency('fftw3')`, `dependency('gsl')` via pkg-config
   - Works on Linux/macOS with package managers; Windows needs vcpkg or manual installation

2. **Meson wrap files** (for reproducible builds):
   - Create `subprojects/openblas.wrap`, etc.
   - OpenBLAS, FFTW, and GSL all have CMake builds that Meson can wrap
   - Ensures consistent versions across platforms

3. **Eigen** — trivially header-only:
   - Already a Meson WrapDB package (`meson wrap install eigen`)
   - Or just clone into `subprojects/`

---

## Implementation Phases

### Phase 1: OpenBLAS/BLAS Foundation

1. Add OpenBLAS dependency to Meson build (with Accelerate fallback on macOS)
2. Create `skills/builtins/blas/` subdirectory for BLAS skills
3. Implement 3–4 core BLAS skills: `Ddot`, `Daxpy`, `Dgemv`, `Dgemm`
4. Create corresponding FlatBuffers schemas
5. Refactor existing VectorMath/FMA to optionally use BLAS backend
6. Verify on all three platforms

### Phase 2: LAPACK Solvers

1. Implement LAPACK skills: `LinearSolve`, `LeastSquares`, `SVD`, `Eigenvalues`
2. Add Cholesky, LU, QR factorization skills
3. These build on the same OpenBLAS dependency (LAPACK is included)

### Phase 3: FFT via FFTW

1. Add FFTW3 dependency to Meson
2. Implement: `FFT1D`, `IFFT1D`, `RealFFT`, optionally `FFT2D`
3. Handle complex number serialization in FlatBuffers (interleaved real/imag or separate vectors)

### Phase 4: Statistics via GSL

1. Add GSL dependency to Meson
2. Implement: `StatsSummary`, `Histogram`, `LinearRegression`, `DistributionSample`
3. GSL's C API with `double*` parameters maps directly to the skill pattern

### Phase 5: Sparse & ODE (Advanced)

1. Add SuiteSparse for sparse solvers
2. Add SUNDIALS for ODE integration
3. Design sparse matrix FlatBuffer schema (CSR/CSC format)
4. Design ODE skill pattern (built-in RHS functions or formula specification)

---

## Open Design Questions

1. **Matrix layout in FlatBuffers** — Store as flat `[double]` in row-major with explicit `rows`/`cols` dimension fields? Or define a reusable `table Matrix { data:[double]; rows:int; cols:int; }` for all linear algebra skills?

2. **In-place operations** — Some LAPACK routines overwrite input buffers (e.g., `dgesv` overwrites `A` with LU factorization). The current FlatBuffer scatter pattern gives mutable response buffers but request data may need copying for destructive operations.

3. **ODE RHS specification** — How to define the differential equation to integrate:
   - (A) Built-in parameterized RHS functions (e.g., Lorenz, van der Pol)
   - (B) A formula/expression string evaluated at runtime
   - (C) Compiled skill references where the RHS is itself another skill
