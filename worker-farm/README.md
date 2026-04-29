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

Phase 2 (Codespaces backend) is the next slice.

## HTTP API

| Method | Path | Notes |
| --- | --- | --- |
| `GET` | `/healthz` | `200 ok` |
| `GET` | `/workers` | JSON array, ordered by start time |
| `POST` | `/workers` | body `{"count": N, "args": ["..."]}`; returns `{"workers":[{"id","ok","pid","error"}]}`; 207 on partial failure |
| `GET` | `/workers/{id}` | single worker JSON |
| `POST` | `/workers/{id}/stop` | 204 on completion |
| `POST` | `/workers/stop-all` | 204 |
| `GET` | `/workers/{id}/log?tail=N` | text/plain |
| `GET` | `/workers/{id}/log/stream` | text/event-stream (SSE; one frame per line) |

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

- Cache (logs, pidfile, recent runs): `~/.cache/tm-worker-controller/`
  on POSIX (or `$XDG_CACHE_HOME/tm-worker-controller/` if set);
  `%LOCALAPPDATA%\tm-worker-controller\` on Windows.
- Default worker config: matches what `extras/scripts/install_linux.sh`
  installs (`~/.config/task-messenger/tm-worker/config-worker.json` on
  POSIX, `%APPDATA%\task-messenger\tm-worker\config-worker.json` on
  Windows).
