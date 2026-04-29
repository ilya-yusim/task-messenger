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
| Manifest schema | Shell scripts already write a per-run `manifest.json` (`{run_id, started_at, host, os, base_dir, worker_bin, config, args[], workers:[{id,pid,log,pidfile}]}`) under `${XDG_CACHE_HOME:-$HOME/.cache}/tm-worker-farm/runs/<run-id>/` (POSIX) or `%LOCALAPPDATA%\tm-worker-farm\runs\<run-id>\` (Windows), with a sibling `latest.txt` pointer. Phase 2 controller persistence should be a superset of this so the scripts and the controller can read each other's runs. |
| Run directory layout | `tm-worker-farm/runs/<run-id>/{manifest.json,worker-NN.log,worker-NN.log.err,worker-NN.pid}`. Codespace runs are mirrored locally under `tm-worker-farm/runs/codespace-<name>/<run-id>/manifest.json` so the local controller can list/stop remote runs without round-tripping over ssh. |
| Codespace transport | `gh codespace ssh -- <cmd>` and `gh codespace cp` only. **No `bash -lc`** — codespace login profiles run an `nvs` auto-loader that hijacks `-n N` flags. Multi-step bootstraps go as a single `ssh -- bash` with the script piped on stdin (CRLF→LF normalized first). Manifest payloads come back inline between sentinel markers instead of via a follow-up `gh codespace cp`. |
| Codespace bootstrap (release path) | `tm-worker` is installed from a published GitHub Release via a makeself `.run` archive. The PowerShell wrapper resolves and downloads the asset locally (where `gh` is authenticated for foreign-owner repos and draft releases) and pre-stages it into the codespace; the bash installer runs makeself with `--accept -- --yes` to skip both the LICENSE and the upgrade prompt. |

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
worker-farm/
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
- No external deps at runtime; `tm-worker` is resolved as in Tactical Decision #6.

### Phase 1 tactical decisions

These were settled before implementation; defaults are baked in.

1. **Spawn concurrency.** `POST /workers` with `count: N` spawns all N
   workers concurrently (one goroutine per spawn) and returns once all
   have either started or reported a start error. Per-worker errors are
   surfaced individually (see #7).
2. **Worker config.** Controller carries a `--config <path>` flag
   (default: same path the install_linux.sh installer writes —
   `~/.config/task-messenger/tm-worker/config-worker.json` on POSIX,
   `%APPDATA%\task-messenger\tm-worker\config-worker.json` on Windows).
   Every spawn invokes `tm-worker -c <config> --mode blocking --noui ...args`.
   Operator-supplied `args` are appended verbatim; if they include their
   own `-c`, the operator's wins (Go's `exec` will pass both, last one
   typically wins for CLI11-based parsers — document, don't enforce).
3. **Log file growth.** Phase 1 accepts unbounded growth. Operator
   cleans `~/.cache/tm-worker-controller/` periodically. Rotation is a
   Phase 2 item (lumberjack-style, 10 MB × 3) once anyone actually runs
   a worker for >a few hours.
4. **Port collision.** Hardcoded default `127.0.0.1:8090`. Override
   with `--port <N>`. If the chosen port is taken, fail fast with a
   clear message naming the port and suggesting `--port`.
5. **Single-instance guard.** On startup, write a pidfile at
   `~/.cache/tm-worker-controller/controller.pid` (POSIX) /
   `%LOCALAPPDATA%\tm-worker-controller\controller.pid` (Windows). If
   the file exists and the PID is alive, refuse to start with a clear
   message ("controller already running as PID N; logs at ..."). Remove
   on clean shutdown; ignore stale pidfiles whose PID is dead.
6. **`tm-worker` discovery.** Same precedence as the shell scripts:
   1. `--worker-bin <path>` (explicit override wins).
   2. `$PATH` lookup for `tm-worker` (`tm-worker.exe` on Windows).
   3. POSIX fallback: `~/.local/bin/tm-worker`.
   4. Windows fallback: `%LOCALAPPDATA%\Programs\tm-worker\bin\tm-worker.exe`.
   If none resolve to an executable file, controller exits at startup
   with a message naming all four candidate paths.
7. **Per-worker spawn errors.** `POST /workers` returns a JSON body of
   the form `{"workers":[{"id":"...","ok":true,"pid":1234}, {"id":"...","ok":false,"error":"exec: ..."}, ...]}`
   and HTTP 207 (Multi-Status) if any spawn failed, 200 if all succeeded.
   Failed spawns get a registry entry with `State=Exited`, `ExitCode=null`,
   and the error stashed in a new `LastError string` field so the UI can
   render it in the table.
8. **Embedded asset cache headers.** Serve `embed.FS` assets with
   `Cache-Control: no-store` in Phase 1. Revisit when the UI stabilizes
   (Phase 2: hash-bust filenames + `immutable`).
9. **Recent-runs file.** On every spawn, append to
   `~/.cache/tm-worker-controller/recent.json` a record of
   `{timestamp, count, args, config_path}`. `tm-worker-farm --restart-last`
   reads the most recent entry and re-spawns the same set. ~20 LoC; saves
   a retro-fit later.
10. **Worker tagging.** The controller picks a per-process UUID at
    startup (`controller_id`) and exports `TM_WORKER_FARM_ID=<controller_id>`
    in the env of every spawned worker. Used by Phase 2 orphan
    reconciliation (`pgrep -af 'TM_WORKER_FARM_ID=<id>'`); does nothing
    in Phase 1 but costs nothing to set now.

---

## Phase 2 — local polish

- Persist worker registry to JSON on every state change so the controller
  can reattach to its workers across restarts. Reuse the existing
  `manifest.json` schema written by `extras/scripts/start_workers_local.{ps1,sh}`
  so a controller restart can adopt script-started runs and vice versa.
- Reconcile-orphans button (scan all `tm-worker` processes; adopt by
  matching a controller-set env var like `TM_WORKER_FARM_ID`).
- Resource limits per worker (Linux: cgroups v2 via `os/exec` + `systemd-run --user`; Windows: Job Objects).
- Optional: structured log parsing — if `tm-worker` ever emits JSON status
  lines, the UI can show "tasks completed", "current task", etc. Until
  then, the log tail is enough.

---

## Lessons from the script prototype (`extras/scripts/*_workers_*`)

These behaviours surfaced while shipping the shell-only prototype and are
load-bearing for the Go controller's Codespaces backend. Encode them in
the backend implementation, not in operator README prose.

- **Never invoke `bash -lc` over `gh codespace ssh --`.** Codespace login
  profiles run an `nvs` Node-version auto-loader that scans the command
  line and treats e.g. `-n 2` as a Node version selector. Pass commands
  directly: `gh codespace ssh -c <cs> -- <cmd>` runs through the user's
  default non-login shell, which is what we want.
- **Pipe multi-step bootstraps as a single `ssh -- bash` over stdin.**
  Each `gh codespace ssh` invocation pays a multi-second connection
  setup, so chaining 6–8 of them is painfully slow. Instead, send one
  bash script over stdin that does mkdir + mv + chmod + start helper +
  reads any results we need, and returns those results inline between
  sentinel markers (e.g. `__TM_MANIFEST_BEGIN/END`). This collapsed our
  start path from ~8 round trips to 2 (one `gh codespace cp` for
  helpers, one `ssh` for everything else).
- **CRLF kills bash-over-stdin.** PowerShell here-strings on Windows are
  CRLF and bash chokes (`set: invalid optiont: -`, `\r` glued onto every
  path). Normalize with `-replace "`r`n", "`n"` before piping.
- **`gh codespace cp` (scp) does NOT expand `$HOME` or `~` on the remote
  side.** Either use absolute paths the local side already knows, or
  resolve `$HOME` once (via `gh codespace ssh -- 'printf %s "$HOME"'`)
  and substitute it. Inside `gh codespace ssh -- ...` arguments,
  single-quote `'$HOME/...'` so bash receives `$HOME` literally and
  expands it remotely; backslash-escaping mangles the path.
- **`gh codespace cp` accepts multiple sources in one call.** Use it
  to upload all helpers / assets in one round trip rather than a cp
  per file.
- **Codespaces ssh auto-resumes Shutdown codespaces.** State filter
  should accept any state (`Available`/`Running`/`Shutdown`); just warn
  when resuming so the operator knows about the ~30 s stall.
- **Local `gh` may be missing the `codespace` scope.** Symptom: `gh
  codespace list` returns 403 with "admin rights" wording. Backend
  should detect that and surface a precise hint:
  `gh auth refresh -h github.com -s codespace`.
- **Public release assets must be fetched with plain `curl`, not `gh`,
  from the codespace.** A codespace's `gh` is typically not authed for
  foreign-owner repos. **Draft releases** are invisible to
  unauthenticated callers, however, so the controller (running on the
  user's box where `gh` *is* authed) must resolve and download draft
  assets locally and stage them onto the codespace via `gh codespace cp`.
- **Asset filenames embed the meson project version, not the dispatch
  tag.** A `workflow_dispatch`-built draft release tagged `draft-<sha>`
  ships assets like `tm-worker-v0.0.1-dev-linux-x86_64.run` (the release
  workflow hardcodes `0.0.1-dev` for manual dispatches and rewrites
  `meson.build` before building). Always discover the asset name via
  `gh release view --json assets`, never construct it from the tag.
- **`install_linux.sh` (bundled inside the makeself `.run`) is
  interactive on upgrades.** It now honors `--yes` / `TM_ASSUME_YES=1`;
  invoke as `<asset>.run --accept -- --yes` so makeself forwards the
  flag to the embedded installer. Note: only takes effect for releases
  built **after** the flag landed in `install_linux.sh`.
- **`tm-worker` ships its libopenblas next to libzt.** Both live under
  the archive's `lib/` directory and are picked up via RPATH
  (`$ORIGIN/../lib` on Linux, `@executable_path/../lib` on macOS). If a
  worker fails to start with `libopenblas.so.0: cannot open shared
  object file`, the bundling step (`bundle_libopenblas` in
  `extras/scripts/build_distribution.sh`) silently dropped it — that
  function is now fail-loud and resolves the SONAME via
  `readelf -d` + the openblas-wrapper subproject, but verify on every
  release.
- **A run already has a stable identifier the operator can reference.**
  The shell scripts use `<run-id> = YYYYMMDD-HHMMSS` plus a `latest.txt`
  pointer; the controller should reuse the same convention so
  CLI/curl/UI users can interchangeably name a run started by either
  driver.

---

## Phase 3 — remote VMs (Codespaces first)

### Inventory schema

```yaml
# ~/.config/tm-worker-controller/config.yaml
hosts:
  - id: local
    backend: local                 # spawns directly
  - id: cs-mybox
    backend: codespace             # uses gh codespace ssh / cp
    codespace:
      name: glorious-space-acorn-97q979gjr9wr3pv5j
      worker_bin: tm-worker        # on remote PATH after install
      config: ~/.config/task-messenger/tm-worker/config-worker.json
  - id: cs-anyrunning
    backend: codespace
    codespace:
      name: ""                     # empty = pick first available
  - id: gcp-rdv
    backend: gcp-iap               # future
    gcp_iap:
      project: task-messenger-prod
      zone: us-west1-a
      instance: tm-worker-1
```

`backend` is the discriminator — local / codespace / ssh / gcp-iap / etc.
Adding a new backend is implementing one Go interface:

```go
type Backend interface {
    Spawn(ctx context.Context, id string, args []string) (RemoteHandle, error)
    Status(ctx context.Context, h RemoteHandle) (WorkerState, *int, error)
    Stop(ctx context.Context, h RemoteHandle, grace time.Duration) error
    Logs(ctx context.Context, h RemoteHandle, sink io.Writer) error
}
```

### Codespaces specifics

- **Transport.** Shell out to `gh codespace ssh -c <name> -- <cmd>` and
  `gh codespace cp -c <name> -e <src> remote:<dst>` directly. Never
  wrap commands in `bash -lc` (see Lessons). For multi-step operations,
  pipe a single bash script to stdin via `gh codespace ssh -- bash` and
  return structured results between sentinel markers. Normalize
  CRLF→LF before piping.
- **Auth preflight.** On controller start (or first use of a codespace
  backend), call `gh codespace list --json name,state`. On 403, surface
  the precise fix: `gh auth refresh -h github.com -s codespace`. On
  success, cache the codespace state for one bootstrap window — don't
  hammer the API.
- **Auto-resume.** Pick any-state codespace if no name is configured;
  warn when resuming a `Shutdown` one. `gh codespace ssh` does the
  resume for us (~30 s).
- **Remote helper.** Reuse the existing `start_workers_local.sh` /
  `stop_workers_local.sh` from `extras/scripts/` as the codespace-side
  helpers. They already speak the manifest schema the controller will
  consume. Upload via a single multi-source `gh codespace cp` (drop in
  `~/`, mv into `~/.local/share/tm-worker-farm/` inside the bootstrap
  script). Re-upload-on-version-bump only — track helper hash locally.
- **Bootstrap (install `tm-worker`).** `POST /hosts/{id}/bootstrap` runs
  the equivalent of `install_tm_worker_codespace.ps1`:
  1. Resolve target tag (default = latest non-draft) via `gh release view`.
  2. Discover the actual asset name (`tm-worker-v.*-linux-x86_64.run`).
  3. `gh release download` locally (handles draft releases + foreign-owner
     auth quirks where the codespace `gh` would 403).
  4. `gh codespace cp` the `.run` and the helper scripts into the codespace.
  5. `gh codespace ssh -- bash` to run `<asset>.run --accept -- --yes`.
- **Lifecycle.** Once installed, lifecycle calls map 1:1 to today's
  shell helpers: `start_workers_local.sh -n <count> -b <bin> -c <config>`
  / `stop_workers_local.sh -r <run> -g <grace>`. Manifest is read back
  inline in the same ssh that started the run; the controller mirrors it
  to `%LOCALAPPDATA%\tm-worker-farm\runs\codespace-<name>\<run-id>\manifest.json`
  so subsequent stop/list calls are local.
- **Connection multiplexing (later).** `gh codespace ssh` does not
  expose OpenSSH `ControlMaster`. If polling latency hurts, fall back to
  `gh codespace ssh --config` to extract the underlying ssh invocation
  and run our own ssh with `-o ControlMaster=auto -o ControlPersist=10m`.
  Keep this behind a feature flag — the bash-over-stdin batching pattern
  already buys most of the win.

### UI changes

- Add a "Host" column to the worker table.
- Add a "Host" dropdown to the start form (defaults to `local`).
- Per-host status badge (reachable / unreachable / bootstrapping /
  needs-auth-scope).

### Two-stage rollout

1. **A — push model:** controller calls `gh codespace ssh -- bash`
   per request, exactly like the prototype scripts. Works today.
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
6. **Release versioning for controller-driven bootstraps.** The release
   workflow currently stamps every `workflow_dispatch` build with
   `version=0.0.1-dev` and rewrites `meson.build` before building, so
   draft assets are named `tm-worker-v0.0.1-dev-linux-x86_64.run`
   regardless of the source `meson.build` version. This is fine for the
   controller because asset name is discovered from the release JSON,
   not constructed from the tag, but it means `tm-worker --version` on a
   draft-installed codespace will read `0.0.1-dev`. If/when the
   controller wants to display the running worker's release version
   accurately, fix the release workflow to read `meson.build` (or a
   `workflow_dispatch` input) instead of hardcoding.
7. **`bundle_libopenblas` is load-bearing.** `extras/scripts/build_distribution.sh`
   resolves `libopenblas.so.<N>` via the openblas-wrapper subproject and
   exits non-zero if the NEEDED SONAME isn't found. Don't paper over
   future failures here — a release archive without `lib/libopenblas.so.0`
   ships a worker that won't start on minimal codespace images.

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
