# Dispatcher Dashboard

This directory contains the browser dashboard served by `MonitoringService` at `/`.

## Files

- `index.html`: dashboard shell and library/script includes
- `assets/css/dashboard.css`: custom dashboard styling
- `assets/js/app.js`: polling controller, KPI mapping, and Tabulator setup
- `assets/lib/`: vendored third-party libraries used by the dashboard

## Runtime Contract

The dashboard polls `GET /api/monitor` every 1000 ms.

Expected payload fields (top level):

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

Expected worker fields:

- `worker_node_id`
- `session_id`
- `remote_endpoint`
- `dispatcher_state`
- `dispatcher_fresh`
- `tasks_sent`
- `tasks_completed`
- `tasks_failed`
- `bytes_sent`
- `bytes_received`
- `avg_roundtrip_ms`
- `session_duration_s`
- `last_seen_dispatcher_ts_ms`

## Updating Vendored Libraries

Current vendored libraries:

- Bootstrap 5.3.5 (`bootstrap.min.css`, `bootstrap.bundle.min.js`)
- Tabulator 6.3.0 (`tabulator_bootstrap5.min.css`, `tabulator.min.js`)
- Alpine.js 3.14.9 (`alpine.min.js`)

To refresh versions, download replacement files into `assets/lib/` and update file names in `index.html` if they change.

PowerShell example:

```powershell
$lib = "dispatcher/monitoring/dashboard/assets/lib"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/bootstrap@5.3.5/dist/css/bootstrap.min.css" -OutFile "$lib/bootstrap.min.css"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/bootstrap@5.3.5/dist/js/bootstrap.bundle.min.js" -OutFile "$lib/bootstrap.bundle.min.js"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/tabulator-tables@6.3.0/dist/css/tabulator_bootstrap5.min.css" -OutFile "$lib/tabulator_bootstrap5.min.css"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/tabulator-tables@6.3.0/dist/js/tabulator.min.js" -OutFile "$lib/tabulator.min.js"
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/alpinejs@3.14.9/dist/cdn.min.js" -OutFile "$lib/alpine.min.js"
```
