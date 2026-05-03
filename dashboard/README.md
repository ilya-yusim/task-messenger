# Dashboard assets

Browser assets served by both the dispatcher's local monitoring
endpoint and the rendezvous service. Both services resolve this
directory at runtime (compile-time `DASHBOARD_DIR` define, then
dev/installed walk-up), so this is a single source of truth.

## Files

| Path | Role |
| --- | --- |
| [index.html](index.html) | Dashboard shell and library/script includes. |
| [assets/css/dashboard.css](assets/css/dashboard.css) | Custom styling. |
| [assets/js/app.js](assets/js/app.js) | Polling controller, KPI mapping, Tabulator setup. |
| [assets/lib/](assets/lib) | Vendored third-party libraries. |

## Runtime contract

The dashboard polls `GET /api/monitor` every 1000 ms.

Top-level payload fields:

- `schema_version`
- `generator_id`
- `dispatcher_node_id`
- `listen_host`
- `listen_port`
- `uptime_seconds`
- `snapshot_timestamp_ms`
- `generator_status`
- `worker_count`
- `task_queue_size`
- `workers_waiting`
- `workers` (array)

`generator_status` values: `starting`, `running`, `no_tasks`,
`no_workers`, `stopping`, `stopped`, `error`.

The `WORKERS (WAITING)` header card renders as `N (M)` where `N` is
`worker_count` and `M` is `workers_waiting`. The backend computes
`workers_waiting` by counting worker rows in the `waiting_for_task`
state.

Worker fields:

- `worker_node_id`, `session_id`, `remote_endpoint`, `worker_state`,
  `dispatcher_fresh`, `tasks_sent`, `tasks_completed`, `tasks_failed`,
  `bytes_sent`, `bytes_received`, `avg_roundtrip_ms`,
  `session_duration_s`, `last_seen_dispatcher_ts_ms`.

## Vendored libraries

- Bootstrap 5.3.5 (`bootstrap.min.css`, `bootstrap.bundle.min.js`)
- Tabulator 6.3.0 (`tabulator_bootstrap5.min.css`, `tabulator.min.js`)
- Alpine.js 3.14.9 (`alpine.min.js`)

Refresh by downloading replacements into `assets/lib/` and updating
file names in `index.html` if they change. Example (PowerShell):

```powershell
$lib = "dashboard/assets/lib"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/bootstrap@5.3.5/dist/css/bootstrap.min.css"      -OutFile "$lib/bootstrap.min.css"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/bootstrap@5.3.5/dist/js/bootstrap.bundle.min.js" -OutFile "$lib/bootstrap.bundle.min.js"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/tabulator-tables@6.3.0/dist/css/tabulator_bootstrap5.min.css" -OutFile "$lib/tabulator_bootstrap5.min.css"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/tabulator-tables@6.3.0/dist/js/tabulator.min.js"              -OutFile "$lib/tabulator.min.js"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/alpinejs@3.14.9/dist/cdn.min.js"                              -OutFile "$lib/alpine.min.js"
```

## Consumers

- [dispatcher/monitoring/](../dispatcher/monitoring/) serves these
  assets at the per-dispatcher local dashboard.
- [services/rendezvous/](../services/rendezvous/README.md) serves
  the same assets at the network-wide end-user dashboard.
