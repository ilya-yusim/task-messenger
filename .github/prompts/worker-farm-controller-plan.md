# Worker Farm Controller — Plan

A small local controller that spawns / tracks / stops `tm-worker` processes
on the local machine first, and on remote VMs (starting with GitHub
Codespaces) afterwards. Web UI for the operator.

## Decisions locked in

| Topic | Decision |
| --- | --- |
| Language | **Go** (single static binary; ships well to remote VMs in Phase 3). |
| SIGTERM handling | Trusted clean — corner cases TBD; will surface only if they bite. |
| First remote target | **GitHub Codespaces** over SSH. Inventory schema must allow other backends later. |
| Worker self-reporting | None today (no JSON status, no `/healthz`). Controller treats workers as opaque processes for Phase 1. |
| Worker per-instance identity | Not needed today. Default `config-worker.json` does not set `zerotier.identity_path`, so libzt generates a fresh ephemeral ZT node ID per process. Each spawned worker only needs a unique log file. Revisit if stable per-slot identities or on-disk worker state are introduced (see Open Issue #1). |

---

## Phase 1 — local controller MVP

### Architecture

```
                  ┌────────────────────────────────────┐
   browser ──────►│  controller (Go HTTP server)       │
   localhost:8090 │   - spawns tm-worker --noui as     │
                 │     direct child processes          │
                 │   - per-worker log file ring buffer │
                 │   - in-memory worker registry       │
                 └─────────────────┬──────────────────┘
                                   │ child procs
                                   ▼
                  tm-worker  tm-worker  tm-worker  ...
```

### Data model (in-memory)

```go
type WorkerState string
const (
    Starting WorkerState = "starting"
    Running  WorkerState = "running"
    Stopping WorkerState = "stopping"
    Exited   WorkerState = "exited"
)

type Worker struct {
    ID         string         // ULID or short uuid
    PID        int
    StartedAt  time.Time
    StoppedAt  *time.Time
    State      WorkerState
    ExitCode   *int
    Args       []string       // tm-worker args used to spawn
    LogPath    string         // ~/.cache/tm-worker-controller/<id>.log
    cmd        *exec.Cmd      // not serialised
}
```

### HTTP endpoints (Phase 1)

| Method | Path | Body / params | Response |
| --- | --- | --- | --- |
| `POST` | `/workers` | `{"count": N, "args": []}` | `{"ids": [...]}` |
| `GET` | `/workers` | — | `[Worker, ...]` |
| `GET` | `/workers/{id}` | — | `Worker` |
| `POST` | `/workers/{id}/stop` | — | `204` (SIGTERM, then SIGKILL after 10 s) |
| `POST` | `/workers/stop-all` | — | `204` |
| `GET` | `/workers/{id}/log` | `?tail=N` | text/plain |
| `GET` | `/workers/{id}/log/stream` | — | text/event-stream (SSE) |
| `GET` | `/healthz` | — | `200 ok` |

Static UI is served at `/` from an embedded `embed.FS`.

### UI (Phase 1)

Single-page vanilla JS, no framework:

- Numeric input "Count" + "Start workers" button.
- Optional textarea "Extra args" (passed verbatim).
- Table: ID · State · PID · Started · Uptime · [Stop] [Logs].
- Polls `GET /workers` every 1 s.
- Log modal opens an SSE stream from `/workers/{id}/log/stream`.

### Process lifecycle

1. `POST /workers` — for each requested instance:
   - Generate `ID` (ULID).
   - Open `~/.cache/tm-worker-controller/<id>.log` (truncate).
   - `exec.Cmd("tm-worker", "--noui", ...args)`, redirect stdout/stderr to the log file.
   - `cmd.Start()`; record PID.
   - Goroutine `cmd.Wait()` → updates `State=Exited` and `ExitCode`.
   - Set up a process group (Unix: `Setpgid=true`) so we can signal the whole subtree.
2. `POST /workers/{id}/stop`:
   - `State = Stopping`; SIGTERM the process group.
   - 10 s timer; if still running, SIGKILL.
3. Controller shutdown:
   - On its own SIGINT/SIGTERM, send SIGTERM to all running workers, wait up to 10 s, then SIGKILL.

### Persistence

- Phase 1: nothing on disk except per-worker log files.
- If the controller crashes, surviving workers become orphans; document that. A "Reconcile orphans" button in the UI later (scan `pgrep tm-worker`, offer to adopt or kill) is a Phase 2 nicety.

### Locations

- Controller binary: `tm-worker-farm` (working name).
- Logs: `~/.cache/tm-worker-controller/<id>.log` (Linux/macOS),
  `%LOCALAPPDATA%\tm-worker-controller\<id>.log` (Windows).
- Config: `~/.config/tm-worker-controller/config.yaml` (Phase 2+, holds remote inventory).

### Repo layout

```
extras/worker-farm/
  README.md
  go.mod
  cmd/tm-worker-farm/main.go
  internal/api/         # HTTP handlers
  internal/local/       # local process manager
  internal/registry/    # worker registry (map + RWMutex)
  internal/logbuf/      # per-worker log files
  web/                  # static index.html, app.js, style.css (embed.FS)
```

### Build & ship

- Single binary built with `go build -o tm-worker-farm ./cmd/tm-worker-farm`.
- No external deps at runtime; `tm-worker` must be on `$PATH` (or pass
  `--worker-bin /path/to/tm-worker`).

---

## Phase 2 — local polish

- Persist worker registry to JSON on every state change so the controller
  can reattach to its workers across restarts.
- Reconcile-orphans button (scan all `tm-worker` processes; adopt by
  matching a controller-set env var like `TM_WORKER_FARM_ID`).
- Resource limits per worker (Linux: cgroups v2 via `os/exec` + `systemd-run --user`; Windows: Job Objects).
- Optional: structured log parsing — if `tm-worker` ever emits JSON status
  lines, the UI can show "tasks completed", "current task", etc. Until
  then, the log tail is enough.

---

## Phase 3 — remote VMs (Codespaces first)

### Inventory schema

```yaml
# ~/.config/tm-worker-controller/config.yaml
hosts:
  - id: local
    backend: local                 # spawns directly
  - id: cs-mybox
    backend: ssh
    ssh:
      host: mybox-abc123.github.dev
      user: codespace
      identity_file: ~/.ssh/codespaces_rsa
      worker_bin: ./tm-worker      # path on the remote
      log_dir: /tmp/tm-worker-farm-logs
  - id: gcp-rdv
    backend: gcp-iap               # future
    gcp_iap:
      project: task-messenger-prod
      zone: us-west1-a
      instance: tm-worker-1
```

`backend` is the discriminator — local / ssh / gcp-iap / etc. Adding a new
backend is implementing one Go interface:

```go
type Backend interface {
    Spawn(ctx context.Context, id string, args []string) (RemoteHandle, error)
    Status(ctx context.Context, h RemoteHandle) (WorkerState, *int, error)
    Stop(ctx context.Context, h RemoteHandle, grace time.Duration) error
    Logs(ctx context.Context, h RemoteHandle, sink io.Writer) error
}
```

### Codespaces specifics

- Auth: `gh codespace ssh -c <name>` is the canonical entrypoint and
  handles the keys for you. The controller can shell out to it directly:
  `gh codespace ssh -c <name> -- bash -lc '...'`.
- Use a remote-side helper script (`tm-worker-farm-remote.sh`) installed
  in `~/.local/bin` on the codespace. The local controller invokes:
  - `start <id> <args...>` → `nohup tm-worker --noui <args> >log 2>&1 &`,
    prints the spawned PID.
  - `status <pid>` → prints `running` / `exited:<code>`.
  - `stop <pid>` → `kill -TERM` + wait + `kill -KILL`.
  - `logs <id> [--follow]` → `tail [-f] log`.
- One persistent SSH multiplex connection per host (`ssh -o ControlMaster=auto -o ControlPersist=10m`)
  to keep latency reasonable for status polls. `gh codespace ssh` may not
  expose ControlMaster — fallback is to call `gh codespace ssh ... -- ssh-config`
  and use the resulting `ssh` invocation directly.
- Bootstrap step: a `POST /hosts/{id}/bootstrap` endpoint that pushes the
  remote helper script and a fresh `tm-worker` binary (downloaded from a
  GitHub Release) over SCP/`gh codespace cp`.

### UI changes

- Add a "Host" column to the worker table.
- Add a "Host" dropdown to the start form (defaults to `local`).
- Per-host status badge (reachable / unreachable / bootstrapping).

### Two-stage rollout

1. **A — push model:** controller calls `gh codespace ssh -- helper
   start/stop/status/logs` per request. No agent. Works today.
2. **B — pull model:** ship the controller binary itself as the remote
   agent (`tm-worker-farm --agent` mode listening on a unix socket
   forwarded via `ssh -L`). Better for high-frequency polling and log
   streaming. Defer until A actually hurts.

---

## Phase 4 — only if the simple thing hurts

- Multi-tenant controller (auth, per-user worker quotas).
- Auto-restart / supervision policies (today the controller is dumb on
  purpose).
- Real orchestration (Nomad, k3s, Cloud Run jobs).
- Metrics scraping (Prometheus endpoint on the controller).

---

## Open issues / pre-work

These need decisions before or during Phase 1:

1. **Per-instance identity dir — not needed today.** Code audit (Apr 2026):
   `tm-worker` writes nothing to disk; logs go to stdout. The ZT identity
   is generated in memory by libzt per-process unless
   `zerotier.identity_path` (JSON) / `-Z` / `--zerotier-identity` is set,
   which the default config does not. Each worker spawned from the
   default config gets a fresh, unique ZT node ID — no collisions. The
   scripts therefore only need a unique log file path per worker; no
   `--identity-dir` / `-Z` plumbing required.
   - **Dead code:** `worker/WorkerOptions.cpp` registers `--identity-dir`
     and `worker.identity_dir`, exposes `get_identity_dir_override()`,
     but no caller reads it. Safe to remove in a cleanup pass; the *real*
     identity option lives in `transport/socket/zerotier/ZeroTierNodeService.cpp`
     (`-Z` / `--zerotier-identity`).
   - **Revisit when any of these become true:**
     - We want stable ZT node IDs per worker slot (so each worker is
       authorized in ZT Central once and survives restarts) — then each
       worker needs `-Z <unique-path>`.
     - The worker gains on-disk state (task cache, results, etc.) — then
       each worker needs its own work dir.
     - A user sets `zerotier.identity_path` in their `config-worker.json`
       and then asks the controller to spawn N workers from it — they'd
       all share the same identity path. Either override per spawn, or
       warn if the config has a non-empty `zerotier.identity_path`.
2. **Worker observability.** Without a `/healthz` or JSON status, the
   only signals are exit code and log scraping. Decide whether to add a
   tiny status HTTP endpoint to `tm-worker` (Phase 1 is fine without it,
   Phase 3 SSH polling is much nicer with it).
3. **SIGTERM corner cases.** If we find any, fix them in `tm-worker`
   itself rather than papering over with SIGKILL — orphaned ZT joins are
   a known pain point with libzt embeds.
4. **Spawning model on Windows.** `Setpgid` is Unix-only. Windows path
   uses Job Objects with `CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP`
   so we can `taskkill /T /PID`. The Go stdlib doesn't expose Job
   Objects directly — use `golang.org/x/sys/windows`. Acceptable
   complexity.
5. **Browser hosting.** Phase 1 controller listens on `127.0.0.1:8090`
   only — no auth. If we ever bind to a non-loopback interface, add an
   auth token header.

---

## Acceptance criteria for Phase 1

Demonstrable end-to-end:

- Run `tm-worker-farm` on Windows / Linux / macOS.
- Open `http://127.0.0.1:8090/` in a browser.
- Click "Start 5 workers"; see five rows appear, all `running`.
- Verify on the host: `pgrep -af tm-worker` shows 5 child processes
  parented to the controller.
- Click "Stop" on one row → state goes `running` → `stopping` → `exited`
  within 10 s.
- Click "Stop all" → all five exit cleanly.
- Open a log view on a running worker → see live output.
- Ctrl-C the controller → all workers exit (no orphans on Linux/macOS;
  on Windows, document any leftover until Job Object work lands).

---

## Out of scope for now

- Worker auto-restart on crash.
- Cross-host orchestration (`spawn N workers anywhere`).
- Authentication / multi-user.
- Metrics dashboards.
- Anything that requires changes to `tm-rendezvous` or the dispatcher.
