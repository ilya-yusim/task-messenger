# tm-worker-farm

Local controller for spawning, tracking, and stopping `tm-worker`
processes. See [`.github/prompts/worker-farm-controller-plan.md`](../.github/prompts/worker-farm-controller-plan.md)
for the full design.

## Status

Phase 1 (local backend) — feature-complete. Implements:

- Flag parsing: `--addr`, `--port`, `--config`, `--worker-bin`, `--restart-last`.
- Pidfile-based single-instance guard.
- `tm-worker` discovery (override → `$PATH` → OS fallback).
- Concurrent spawn with per-worker error reporting (HTTP 207 multi-status on partial failure).
- Graceful stop: SIGTERM (POSIX) / `CTRL_BREAK_EVENT` (Windows), SIGKILL/`TerminateProcess` after grace.
- Per-worker log file with `?tail=N` and SSE live stream.
- Recent-runs JSONL + `--restart-last`.
- `TM_WORKER_FARM_ID` env var on every spawn (for Phase 2 orphan reconciliation).
- Embedded vanilla-JS UI: spawn form, polling worker table, stop / stop-all, log modal with live tail.

Phase 2 (local polish) — complete. Slices delivered:

- **Slice 1** — unified cache layout: `runs/<run-id>/{manifest.json, worker-NN.log, worker-NN.pid}` plus `runs/latest.txt`. Cache root: `%LOCALAPPDATA%\tm-worker-farm` on Windows, `~/.cache/tm-worker-farm` on POSIX.
- **Slice 2** — write-through manifest (atomic temp+rename guarded by a per-run mutex to fix a Windows rename race) and persistent controller identity in `identity.json` (`ctl-<16hex>` + `previous_ids` history).
- **Slice 3** — orphan reconciliation. At startup the controller scans `runs/*/worker-NN.adopt` sentinels and classifies each PID into one of three buckets:
  - **mine** — `controller_id` matches our current ID or any entry in `previous_ids` and the PID is alive → auto-adopted, polled every 2 s for liveness.
  - **stale** — sentinel exists but PID is dead → registered as `exited` so the UI can surface the corpse.
  - **theirs** — `controller_id` belongs to a different install and the PID is alive → quarantined; operator picks `adopt` / `kill` / `ignore` from the UI panel.
- **Slice 4** — docs (this section).

Liveness probe (used by both the supervise loop for adopted workers and the quarantine list refresh):

- POSIX: `os.FindProcess` + `Signal(0)`. `ESRCH` ⇒ dead, `EPERM` ⇒ alive (different uid).
- Windows: `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) + GetExitCodeProcess`. Alive iff `STILL_ACTIVE (259)`.

Workers outlive the controller by design:

- POSIX: spawned with `Setsid: true` so they detach from the controller's session and don't receive SIGHUP when the terminal closes.
- Windows: spawned with `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP` so closing the controller window doesn't deliver `CTRL_CLOSE_EVENT` to them. Trade-off: with no console attached, console-control signals (`CTRL_BREAK`) cannot reach them either, so `Stop` becomes `TerminateProcess` for both forked and adopted workers. Graceful in-process shutdown on Windows is deferred to Phase 4 (Job Object + side-channel signal).

Adopted-worker termination:

- POSIX: SIGTERM, then SIGKILL after `gracePeriod`.
- Windows: `TerminateProcess` directly (see above).

Purging exited rows: every exited worker (whether it died naturally, was Stop'd, or was registered as `stale` during the adoption scan) gets a **Purge** button that deletes its log file, pidfile, and `.adopt` sentinel and removes the row from the registry. The run directory itself is left alone so any sibling slots still on disk keep their state.

Phase 3 (Codespaces backend) is the next phase.

## HTTP API

| Method | Path | Notes |
| --- | --- | --- |
| `GET` | `/healthz` | `200 ok` |
| `GET` | `/workers` | JSON array, ordered by start time |
| `POST` | `/workers` | body `{"count": N, "args": ["..."]}`; returns `{"workers":[{"id","ok","pid","error"}]}`; 207 on partial failure |
| `GET` | `/workers/{id}` | single worker JSON |
| `POST` | `/workers/{id}/stop` | 204 on completion |
| `POST` | `/workers/{id}/purge` | 204; deletes log/pid/sentinel for an exited worker; 400 if still running |
| `POST` | `/workers/stop-all` | 204 |
| `GET` | `/workers/{id}/log?tail=N` | text/plain |
| `GET` | `/workers/{id}/log/stream` | text/event-stream (SSE; one frame per line) |
| `GET` | `/quarantine` | JSON array of `theirs` candidates from the startup scan |
| `POST` | `/quarantine/{run-id}/{NN}/{action}` | `action` ∈ `adopt`/`kill`/`ignore`; 204 on success |

## Build

Requires Go 1.22+.

### Standalone (Go toolchain)

```sh
cd worker-farm
go build -o tm-worker-farm ./cmd/tm-worker-farm
```

### Via Meson (alongside the C++ build)

The Go controller is gated behind `-Dbuild_worker_farm=true` (default
off) so contributors who don't touch this component don't need a Go
toolchain. When enabled, `meson compile` produces the binary at
`builddir/worker-farm/tm-worker-farm[.exe]` and `meson install` places
it in `bindir`.

```sh
meson setup builddir -Dbuild_worker_farm=true
meson compile -C builddir tm-worker-farm
```

The web assets are embedded via `embed.FS`; no separate static-asset
deployment.

## Run

```sh
./tm-worker-farm                       # listens on 127.0.0.1:8090
./tm-worker-farm --port 9000           # custom port (fail-fast on collision)
./tm-worker-farm --worker-bin /path/to/tm-worker
./tm-worker-farm --config /path/to/config-worker.json
./tm-worker-farm --restart-last        # respawn the most recent run
```

Open `http://127.0.0.1:8090/` in a browser.

## Locations

- Cache (per-run state, pidfile, recent runs, controller log):
  `~/.cache/tm-worker-farm/` on POSIX (or `$XDG_CACHE_HOME/tm-worker-farm/`
  if set); `%LOCALAPPDATA%\tm-worker-farm\` on Windows. Per-run state
  lives under `runs/<run-id>/` where `<run-id>` is `YYYYMMDD-HHMMSS`
  (with a `-N` suffix on collision). Worker logs are
  `runs/<run-id>/worker-NN.log`.
  The pre-Phase-2 cache directory was named `tm-worker-controller`;
  the controller logs a one-line warning on startup if that legacy
  directory still exists. There is no automatic migration.
- Default worker config: matches what `extras/scripts/install_linux.sh`
  installs (`~/.config/task-messenger/tm-worker/config-worker.json` on
  POSIX, `%APPDATA%\task-messenger\tm-worker\config-worker.json` on
  Windows).
