# Built-in skills

\ingroup skills_module

Bundled skills shipped with the platform. Each skill demonstrates the
public skill-authoring pattern documented in
[skills/README.md](../README.md): a `<Skill>.skill.toml` schema plus a
`<Skill>_impl.hpp` compute implementation.

## Catalog

| Name | Files | Notes |
| --- | --- | --- |
| `builtin.StringReversal` | [StringReversalSkill.skill.toml](StringReversalSkill.skill.toml), [StringReversalSkill_impl.hpp](StringReversalSkill_impl.hpp) | Reverses a UTF-8 string. Smallest reference example. |
| `builtin.MathOperation` | [MathOperationSkill.skill.toml](MathOperationSkill.skill.toml), [MathOperationSkill_impl.hpp](MathOperationSkill_impl.hpp), [MathOperation.hpp](MathOperation.hpp) | Scalar arithmetic with operation tag. |
| `builtin.VectorMath` | [VectorMathSkill.skill.toml](VectorMathSkill.skill.toml), [VectorMathSkill_impl.hpp](VectorMathSkill_impl.hpp) | Element-wise vector ops with a fixed maximum size. |
| `builtin.FusedMultiplyAdd` | [FusedMultiplyAddSkill.skill.toml](FusedMultiplyAddSkill.skill.toml), [FusedMultiplyAddSkill_impl.hpp](FusedMultiplyAddSkill_impl.hpp) | Three-input scalar fused multiply-add. |
| `builtin.Dgemm` (BLAS) | [blas/DgemmSkill.skill.toml](blas/DgemmSkill.skill.toml), [blas/DgemmSkill_impl.hpp](blas/DgemmSkill_impl.hpp) | Double-precision matrix multiply via OpenBLAS / Accelerate. Gated by `enable_blas_skills`. |

## Authoring a new built-in skill

1. Drop `<Skill>.skill.toml` and `<Skill>_impl.hpp` next to the
   existing examples (or under a topical subdirectory like `blas/`).
2. Pick a unique namespaced name (`builtin.<Skill>`) in the schema.
3. Wire the new files into [meson.build](meson.build).
4. Rebuild. The skill auto-registers via the codegen pipeline; no
   central enum to update.

For the runtime API and integration with workers and dispatchers, see
[skills/README.md](../README.md).
