# Generators

A **generator** is the algorithm that drives a Task Messenger
dispatcher. It produces task messages, hands them to the dispatcher
to fan out to workers, and consumes the results. The dispatcher
itself is a library; a generator binary links the dispatcher in,
provides the `IGenerator` implementation, and runs the event loop.

The generators shipped here are reference implementations and load
generators. They are explicit placeholders for **user-authored
algorithms** that will eventually drive real workloads.

## Bundled generators

| Path | Binary | Purpose |
| --- | --- | --- |
| [interactive/](interactive/) | `tm-generator-interactive` | REPL/menu-driven submission. Pick a skill, supply inputs, watch results. Useful for development and demos. |
| [auto-refill/](auto-refill/) | `tm-generator-auto-refill` | Steady-state load generator: keeps a configurable backlog of tasks in flight against the connected workers. |

Each generator binary is launched with a dispatcher config; see
[config/README.md](../config/README.md).

## Authoring a new generator

The contract is small:

- [common/IGenerator.hpp](common/IGenerator.hpp) — the generator
  interface (`initialize`, `run`, `on_shutdown`).
- [common/run_generator.hpp](common/run_generator.hpp) — the entry
  point a generator's `main()` calls. Handles option parsing,
  dispatcher startup, signal handling, and shutdown.
- [common/TaskGenerator.{hpp,cpp}](common/TaskGenerator.hpp) — a
  helper for the common case of producing tasks from a skill iterator
  and pushing them through the dispatcher's task queue.
- [common/SkillTestIterator.{hpp,cpp}](common/SkillTestIterator.hpp) —
  iterates over registered skills' test request buffers; convenient
  for "exercise every skill once" generators.
- [common/VerificationHelper.{hpp,cpp}](common/VerificationHelper.hpp) —
  optional response-verification utilities.

A new generator typically:

1. Provides a class that implements `IGenerator`.
2. Calls `run_generator(...)` from `main()`.
3. Adds itself to [meson.build](meson.build) as a new executable
   target.

## Related documentation

- Dispatcher lifecycle: [dispatcher/README.md](../dispatcher/README.md).
- Skill catalog generators iterate over: [skills/README.md](../skills/README.md).
- Top-level overview: [README.md](../README.md).
