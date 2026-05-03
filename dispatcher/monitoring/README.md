# Dispatcher monitoring

\ingroup task_messenger_dispatcher

In-process HTTP monitoring endpoint for a single `tm-dispatcher`
instance. Exposes a JSON snapshot of the dispatcher state and serves
the [dashboard/](../../dashboard/README.md) browser assets. The
endpoint surfaced here is **per-dispatcher and local**; the
network-wide view is served by
[services/rendezvous/](../../services/rendezvous/README.md).

## Components

| File | Role |
| --- | --- |
| [MonitoringService.{hpp,cpp}](MonitoringService.hpp) | Owns the cpp-httplib server, the acceptor thread, and the periodic snapshot reporter. |
| [HttpRequestHandler.{hpp,cpp}](HttpRequestHandler.hpp) | Routes registration: `/healthz`, `/api/monitor`, and the static dashboard mount. |
| [MonitoringSnapshot.hpp](MonitoringSnapshot.hpp), [MonitoringSnapshotBuilder.{hpp,cpp}](MonitoringSnapshotBuilder.hpp) | Builds the JSON snapshot from `AsyncTransportServer` and `SessionManager` state. |
| [SnapshotReporter.{hpp,cpp}](SnapshotReporter.hpp) | Optionally pushes snapshots to the rendezvous server so its network-wide dashboard can render them. |
| [MonitoringOptions.{hpp,cpp}](MonitoringOptions.hpp) | CLI / JSON option parsing for the listening endpoint. |

## HTTP endpoints

| Path | Purpose |
| --- | --- |
| `GET /healthz` | Liveness probe. |
| `GET /api/monitor` | Cached JSON snapshot consumed by the dashboard. |
| `GET /` | Mounts the dashboard assets at the resolved [dashboard/](../../dashboard/README.md) directory. |

## Snapshot resolution

`MonitoringService::resolve_dashboard_dir()` checks, in order:

1. Compile-time `DASHBOARD_DIR` define (set via Meson).
2. Development layout: walk up from the executable to the repository
   root and append `dashboard/`.
3. Installed layout: a `dashboard/` directory next to the executable.

The same resolution logic is used by the rendezvous server, so a
single source-of-truth `dashboard/` directory ships to both. If no
directory is found, the JSON endpoints continue to work and only
static UI serving is skipped.

## Related documentation

- Parent component: [dispatcher/README.md](../README.md).
- Dashboard runtime contract and asset structure: [dashboard/README.md](../../dashboard/README.md).
- Network-wide dashboard relay target: [services/rendezvous/README.md](../../services/rendezvous/README.md).
