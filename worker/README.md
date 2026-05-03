# Worker

\ingroup task_messenger_worker

The worker is the compute side of Task Messenger. A worker process
(`tm-worker`) connects back to a dispatcher over ZeroTier, executes
the skills the dispatcher requests, and reports per-task metrics.

## Responsibilities

- Open a ZeroTier connection to a configured dispatcher.
- Receive `TaskMessage` payloads and dispatch each one to the matching
  skill via the skill registry.
- Track per-task counters (tasks completed, bytes sent/received,
  latency) and surface them to the optional terminal UI.
- Expose its lifecycle (running / paused / stopping) so the UI or the
  dispatcher monitoring snapshot can render it.

## Submodules

| Path | Scope |
| --- | --- |
| [runtime/](runtime/README.md) | Pluggable execution engines (`BlockingRuntime`, `AsyncRuntime`) selected via `--mode`. |
| [session/](session/README.md) | `WorkerSession`: lifecycle, configuration parsing, metrics aggregation. |
| [processor/](processor/) | `TaskProcessor` shim that hands incoming tasks to the skill registry. |
| [ui/](ui/README.md) | Optional FTXUI terminal dashboard, behind a build-time check. |

Skill execution happens through the shared registry documented in
[skills/README.md](../skills/README.md). Networking primitives live in
[transport/README.md](../transport/README.md).

## Configuration and runtime

Configuration is read from `config/config-worker.json` or supplied via
CLI flags. See [config/README.md](../config/README.md) for the file
layout.

To run multiple worker processes from a single host (locally or
through a remote backend such as a GitHub Codespace), use
[worker-farm/README.md](../worker-farm/README.md) instead of starting
each process by hand.

## Related documentation

- Top-level overview: [README.md](../README.md).
- Skill authoring and registry: [skills/README.md](../skills/README.md).
- Networking primitives: [transport/README.md](../transport/README.md).
