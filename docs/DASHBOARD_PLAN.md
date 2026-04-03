# Part C: Dispatcher Monitoring Dashboard — Implementation Plan

## Status

- Overall status: completed
- Commits completed: 1, 2, 3, 4, 5, 6

## Phase 1 decisions (confirmed)

| Decision | Choice |
|----------|--------|
| Static asset hosting | Served from MonitoringService routes (same host + port as `/api/monitor`) |
| Default poll interval | 1000 ms |
| Header KPIs | worker_count · generator_status · task_pool_available · task_pool_waiting · uptime_seconds · avg_roundtrip aggregate · failure rate aggregate |
| Frontend stack | Alpine.js + Bootstrap + Tabulator |
| Asset directory | `dispatcher/monitoring/dashboard/` |

---

## Commit sequence

### Commit 1 — Static hosting foundation *(blocks all frontend runtime validation)*

Goal: MonitoringService serves static files from `dispatcher/monitoring/dashboard/` so the dashboard is reachable alongside the existing API.

**Files changed**
- `dispatcher/monitoring/MonitoringService.cpp`
  - Call `http_server_->set_mount_point("/", <dashboard_dir>)` before route registration.
  - Apply explicit MIME mappings for `.html`, `.js`, `.css`.
  - Static assets inherit default httplib caching; API routes keep `Cache-Control: no-store`.
  - Log resolved asset path on startup.
- `dispatcher/monitoring/MonitoringService.hpp`
  - Add private helper `static std::string resolve_dashboard_dir()`.

**Verification**
- Start dispatcher + generator; open `http://127.0.0.1:9090/` → 200 with HTML.
- Open `http://127.0.0.1:9090/api/monitor` → 200 with JSON, unchanged.
- Open `http://127.0.0.1:9090/healthz` → 200 `ok`, unchanged.

---

### Commit 2 — Frontend scaffold and visual skeleton

Goal: Static page with header KPI cards, worker table container, status/error banner; all visible but unpopulated.

**Files created**
- `dispatcher/monitoring/dashboard/index.html` — Alpine component root, Bootstrap layout, Tabulator container.
- `dispatcher/monitoring/dashboard/assets/css/dashboard.css` — custom visual language, badge colours, stale/degraded row classes.
- `dispatcher/monitoring/dashboard/assets/js/app.js` — Alpine state skeleton, all data properties initialised to null/empty.

**Library assets** (vendored locally in `dispatcher/monitoring/dashboard/assets/lib/`)
- Bootstrap CSS + JS
- Alpine.js v3
- Tabulator v6

**Verification**
- Page loads without console errors.
- KPI card placeholders render.
- Empty worker table renders with correct column headers.

---

### Commit 3 — Data adapter and polling controller

Goal: `/api/monitor` is polled every 1000 ms, results normalised and bound to Alpine state.

**Files changed**
- `dispatcher/monitoring/dashboard/assets/js/app.js`
  - Interval fetch with in-flight guard (skip tick if previous request still pending).
  - Null-safe field access for all payload fields.
  - Aggregate computations:
    - `avg_roundtrip_ms` = mean of per-worker `avg_roundtrip_ms` (workers with `tasks_completed > 0` only).
    - `failure_rate_pct` = `total_failed / (total_completed + total_failed) * 100`, zero-safe.
  - Last-success timestamp and consecutive-failure counter for error banner.
  - `uptime_seconds` formatted as `Xh Ym Zs`.

**Verification**
- Compare rendered KPI values against raw `/api/monitor` JSON.
- Confirm no duplicate in-flight requests under DevTools Network tab.
- Error banner appears after stopping dispatcher; clears on recovery.

---

### Commit 4 — Tabulator columns, formatters, and interactions

Goal: Worker table is interactive, stale workers are highlighted, filters work.

**Files changed**
- `dispatcher/monitoring/dashboard/assets/js/app.js`
  - Tabulator column definitions:
    - `worker_node_id` (truncated + tooltip)
    - `dispatcher_state` (badge formatter: `assigned_active` → green, `assigned_stalled` → amber, `connecting`/`unknown` → red)
    - `dispatcher_fresh` (✓ / stale icon)
    - `tasks_sent`, `tasks_completed`, `tasks_failed`
    - `avg_roundtrip_ms` (formatted to 1 dp + "ms")
    - `session_duration_s` (formatted as m:ss)
    - `bytes_sent` / `bytes_received` (KB/MB auto)
    - `remote_endpoint`
  - Row formatter: dimmed + italic style for `dispatcher_fresh === false`.
  - Header filter controls: state dropdown + freshness checkbox.
  - Preserve horizontal table scroll position across refreshes by updating table data without full reinit.
- `dispatcher/monitoring/dashboard/assets/css/dashboard.css`
  - Stale row CSS class.
  - State badge colour palette.

**Verification**
- Connect and disconnect workers; verify table reflects state transitions.
- Filter by state and freshness; verify rows shown/hidden correctly.
- Stale row styling triggers after freshness window elapses.

---

### Commit 5 — Runtime integration and packaging path

Goal: Asset path resolution works in both dev and installed layouts; Meson updated if needed.

**Files changed**
- `dispatcher/monitoring/MonitoringService.cpp`
  - `resolve_dashboard_dir()` probes candidates in priority order:
    1. `DASHBOARD_DIR` compile-time define (overridable via Meson).
    2. Repository path `dispatcher/monitoring/dashboard` resolved from executable traversal (dev layout).
    3. Executable path `./dashboard` (installed layout).
  - Log warning and skip mount if no directory found (non-fatal).
- `dispatcher/meson.build` *(if needed)*
  - Pass `-DDASHBOARD_DIR="..."` via `cpp_args` for installed target.
- `meson_options.txt`
  - Add `dashboard_dir` string option to override dashboard location at configure time.

**Verification**
- Rebuild; run from `builddir/generators/` devdir — dashboard loads.
- Simulate install path; dashboard still loads.
- Path-not-found case: service starts successfully, `/` returns 404, API routes unaffected.

---

### Commit 6 — Docs and operator handoff *(final gate)*

**Files changed**
- `README.md` — add "Monitoring Dashboard" section with startup URL and quick start.
- `docs/INSTALLATION.md` — add "Accessing the Dispatcher Dashboard" subsection.
- `dispatcher/monitoring/dashboard/README.md` — local dev notes, expected API contract, and vendored library refresh steps.

---

## File worklist at a glance

| File | Commit | Action |
|------|--------|--------|
| `dispatcher/monitoring/MonitoringService.cpp` | 1, 5 | Add `set_mount_point`, MIME map, path resolution |
| `dispatcher/monitoring/MonitoringService.hpp` | 1 | Add `resolve_dashboard_dir()` private helper |
| `dispatcher/monitoring/dashboard/index.html` | 2 | Create |
| `dispatcher/monitoring/dashboard/assets/css/dashboard.css` | 2, 4 | Create, extend |
| `dispatcher/monitoring/dashboard/assets/js/app.js` | 2, 3, 4 | Create, extend |
| `dispatcher/monitoring/dashboard/assets/lib/` | 2 | Vendor Alpine, Bootstrap, Tabulator |
| `meson_options.txt` | 5 | Add `dashboard_dir` option |
| `dispatcher/meson.build` | 5 | Optional: `DASHBOARD_DIR` define |
| `README.md` | 6 | Add dashboard section |
| `docs/INSTALLATION.md` | 6 | Add access instructions |
| `dispatcher/monitoring/dashboard/README.md` | 6 | Create |

---

## Scope boundaries

**Included**
- Read-only dashboard visualization over existing `/api/monitor`
- Alpine.js + Bootstrap + Tabulator stack
- No backend schema changes unless a real blocker is found

**Excluded**
- Authentication / authorization
- WebSocket push / realtime streaming
- Historical metrics persistence
- Cloud / router extraction work
