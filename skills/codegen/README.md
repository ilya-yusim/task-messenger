# Skill code generation

\ingroup skills_module

The codegen pipeline turns a `.skill.toml` schema into the C++ glue
that registers a skill with the runtime. It is the build-time backend
behind the public skills API documented in
[skills/README.md](../README.md). Skill authors interact with it only
indirectly, by writing `<Skill>.skill.toml` and `<Skill>_impl.hpp`.

## Components

| File | Role |
| --- | --- |
| [skill_generator.py](skill_generator.py) | Reads a `.skill.toml`, emits a FlatBuffers `.fbs` schema, a C++ generated header (`<ClassName>_gen.hpp`) bundling the skill class with all boilerplate, and a registration `.cpp` that invokes `REGISTER_SKILL_CLASS`. |
| [field_types.py](field_types.py) | Field-role definitions (`scalar`, `string`, `vector`, `matrix`, `dim`) and the per-role accessor handlers used by the generator. |

## Pipeline

For each `<Skill>.skill.toml`:

1. The Meson build invokes `skill_generator.py --outdir <build>`.
   This produces:
   - `<ClassName>.fbs` — FlatBuffers schema in the
     `TaskMessenger.Skills` namespace.
   - `<ClassName>_gen.hpp` — generated skill class (subclasses
     `Skill<...>`) wiring request/response field accessors to
     `<ClassName>_impl.hpp`.
   - `<ClassName>.cpp` — registration translation unit.
2. `flatc` then turns `<ClassName>.fbs` into
   `<ClassName>_generated.h` for FlatBuffers access.
3. The generated `.cpp` is linked into the runtime so the registry
   sees the skill at static-init time.

The author-supplied `<ClassName>_impl.hpp` is included by the
generated header and provides the actual compute body.

## TOML schema

A `.skill.toml` file declares:

- `[skill]` block: `name`, `class_name`, `description`, `version`,
  optional `table_prefix`.
- `[[request.fields]]` and `[[response.fields]]`: a `name`, a `type`
  (FlatBuffers field type), and a `role` (`scalar` / `string` /
  `vector` / `matrix` / `dim`). Matrix fields with `rows_dim` and
  `cols_dim` auto-inject the corresponding dim fields.

Existing skills under [skills/builtins/](../builtins/README.md) are
the working examples.

## Related documentation

- Public skill API and authoring: [skills/README.md](../README.md).
- Built-in examples: [skills/builtins/README.md](../builtins/README.md).
- Runtime registry that consumes the generated registrations: [skills/registry/README.md](../registry/README.md).
