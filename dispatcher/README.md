# Dispatcher

\ingroup task_messenger_dispatcher

The dispatcher is the server-side half of Task Messenger. A dispatcher
process (`tm-dispatcher`) accepts worker connections over ZeroTier,
hands tasks out to those workers, collects results, and exposes a
local monitoring dashboard for the operator who started it.

## Responsibilities

- Run the **algorithm** that produces tasks and consumes results. The
  algorithm is supplied by a generator linked into the dispatcher
  binary; see [generators/](../generators/).
- Serve worker connections through the dispatcher transport server,
  fanning task messages out and collecting replies.
- Track per-worker session state and expose it through a local-only
  monitoring HTTP endpoint and dashboard.
- Optionally register with a `tm-rendezvous` service so the broader
  network can discover this dispatcher; see
  [services/rendezvous/](../services/rendezvous/).

## Submodules

| Path | Scope |
| --- | --- |
| [transport/](transport/README.md) | `AsyncTransportServer`: accepts ZeroTier worker connections and hands tasks off to sessions. |
| [session/](session/README.md) | `SessionManager` and per-worker coroutine sessions; tracks metrics. |
| [monitoring/](monitoring/) | In-process HTTP server, monitoring snapshot builder, and reporter feeding the local dashboard. |

The dashboard assets served by the monitoring endpoint live at
[dashboard/](../dashboard/README.md). The dashboard surfaced here is
**per-dispatcher and local**; the network-wide view is served by
`tm-rendezvous`.

## Configuration and runtime

The dispatcher is normally launched indirectly via a generator binary
(for example `tm-generator-interactive` or `tm-generator-auto-refill`).
The generator reads `config/config-dispatcher.json` and constructs a
`DispatcherApp` from it.

See [config/README.md](../config/README.md) for the configuration
file layout and [generators/README.md](../generators/README.md) for
the launch flow.

## Related documentation

- Top-level overview: [README.md](../README.md).
- Skill execution: [skills/README.md](../skills/README.md).
- Networking primitives: [transport/README.md](../transport/README.md).
