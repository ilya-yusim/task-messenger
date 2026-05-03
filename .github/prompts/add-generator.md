# Author a new generator — agent hand-off

Use this prompt to add a new generator (algorithm placeholder) that
drives a `tm-dispatcher`. The generator is the slot where the
production algorithm will eventually live; today the repo ships
interactive and auto-refill generators that exercise the slot.

## Inputs

- A short description of the workload pattern the generator produces
  (one-shot batch, continuous refill, interactive, …).
- Whether the generator needs a UI (FTXUI is available; both today's
  generators use it).

## Files of interest

- [generators/README.md](../../generators/README.md) — generator
  contract and component overview.
- [generators/common/IGenerator.hpp](../../generators/common/IGenerator.hpp) —
  the interface the dispatcher invokes.
- [generators/common/TaskGenerator.hpp](../../generators/common/TaskGenerator.hpp) —
  shared helpers for emitting `TaskMessage`s.
- [generators/common/run_generator.hpp](../../generators/common/run_generator.hpp) —
  process-level entry point that builds the dispatcher around an
  `IGenerator`.
- [generators/interactive/](../../generators/interactive/) and
  [generators/auto-refill/](../../generators/auto-refill/) — reference
  implementations.

## Steps the agent performs

1. Create `generators/<name>/` mirroring the structure of the closest
   existing generator.
2. Implement an `IGenerator` subclass.
3. Wire the executable in `generators/<name>/meson.build` and register
   it from [generators/meson.build](../../generators/meson.build).
4. Drive the dispatcher via `run_generator(...)` from `main()`; do
   not duplicate dispatcher startup code.

## Verification

- `meson compile -C builddir` produces `tm-generator-<name>`.
- Running the generator + a worker drains tasks at the expected rate.
- The dispatcher's local monitoring dashboard
  ([dispatcher/monitoring/README.md](../../dispatcher/monitoring/README.md))
  shows session activity.

## Out of scope

- Replacing the generator slot with the production algorithm — that
  is a forward-looking design item, not part of this prompt.
