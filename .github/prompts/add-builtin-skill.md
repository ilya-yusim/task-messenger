# Add a new built-in skill — agent hand-off

Use this prompt to add a new built-in skill to the in-tree
`skills/builtins/` directory. The result is a skill that the registry
discovers automatically at process start in both the dispatcher and
the worker.

## Inputs

- **Skill name** — namespaced string, conventionally
  `builtin.<ClassName>` (for example `builtin.MatrixMultiply`).
- **C++ class name** — the type that subclasses `Skill<...>`.
- **Request fields** — list of `(name, type, role)` triples; roles are
  `scalar` / `string` / `vector` / `matrix` / `dim`.
- **Response fields** — same shape.
- **Compute body** — the C++ code that produces the response from the
  request.

## Files of interest

- [skills/README.md](../../skills/README.md) — public skills API.
- [skills/codegen/README.md](../../skills/codegen/README.md) — schema
  generator and the `.skill.toml` → `.fbs` + `_gen.hpp` + `.cpp`
  pipeline.
- [skills/registry/README.md](../../skills/registry/README.md) —
  registry, hashed identity, and dispatch.
- [skills/builtins/README.md](../../skills/builtins/README.md) —
  catalogue of existing built-ins; mirror their structure.
- [skills/builtins/string_reversal/](../../skills/builtins/string_reversal/) —
  small reference for a new skill that does not need BLAS.
- [skills/builtins/blas/DgemmSkill.skill.toml](../../skills/builtins/blas/DgemmSkill.skill.toml) —
  reference for a matrix-shaped skill (uses `enable_blas_skills`).

## Steps the agent performs

1. Decide whether the skill needs BLAS. If yes, place it under
   `skills/builtins/blas/` and ensure `meson_options.txt`'s
   `enable_blas_skills` remains true; otherwise place it under
   `skills/builtins/<my_skill>/`.
2. Author `<ClassName>.skill.toml`. Declare `[skill]` (`name`,
   `class_name`, `description`, `version`) and the
   `[[request.fields]]` / `[[response.fields]]` blocks.
3. Author `<ClassName>_impl.hpp`. Implement the compute body against
   the typed pointers exposed by the generated header. Do not include
   any FlatBuffers headers from `_impl.hpp`.
4. Add the skill's `meson.build` (mirror the closest existing
   built-in). The build wires `skill_generator.py` and `flatc` for
   you; do not hand-edit the generated `.fbs`, `_gen.hpp`, `.cpp`,
   or `_generated.h`.
5. Register the skill's `meson.build` from
   `skills/builtins/meson.build`.
6. Build with `meson compile -C builddir`. The skill is registered at
   static-init time via `REGISTER_SKILL_CLASS`; no other wiring is
   required.

## Verification

- The skill appears in the dispatcher's task pool and runs to
  completion against a connected worker.
- `SkillRegistry::get_skill_id("builtin.<ClassName>")` returns a value
  (non-empty `std::optional`).
- The interactive generator at
  [generators/interactive/](../../generators/interactive/) lists the
  new skill automatically.

## Out of scope

- External (out-of-tree) skill loading is a roadmap item, not
  available today.
- The `_impl.hpp` body must not assume any persistent state across
  invocations; skills are stateless.
