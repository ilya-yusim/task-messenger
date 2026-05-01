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

Purging exited rows: every exited worker (whether it died naturally, was Stop'd, or was registered as `stale` during the adoption scan) gets a **Purge** button that deletes its log file, pidfile, and `.adopt` sentinel and removes the row from the registry. The run directory itself is left alone so any sibling slots still on disk keep their state. The header bar also exposes a **Purge all** button that walks the registry once and purges every exited row in a single click; running/starting/stopping rows are skipped.

Phase 3 (remote VMs — Codespaces) is feature-complete. Slices delivered:

- **Slice 3.1 — Backend interface refactor.** `internal/backend` defines `Backend` + `Handle` + `Spec`; `local.Manager` now goes through a `LocalBackend` for spawn/terminate/kill/liveness. No behaviour change; `Worker.Host` is `"local"` everywhere unless an inventory entry says otherwise.
- **Slice 3.2 — Inventory config.** JSON inventory at `~/.config/tm-worker-farm/hosts.json` (or `%APPDATA%\tm-worker-farm\hosts.json` on Windows). Default when missing: synthesise `[{id:"local", backend:"local"}]`. Duplicate `id` rejects with a typed `InventoryError`. UI gets a host dropdown and per-host status badge.
- **Slice 3.3 — `gh` preflight + ssh transport.** `internal/gh` shells out to `gh codespace ssh` / `gh codespace cp` only; never `bash -lc`. Typed errors (`MissingBinaryError`, `NotLoggedInError`, `NeedsCodespaceScopeError`, `NotFoundError`) carry remediation hints (e.g. `gh auth refresh -h github.com -s codespace`). `GET /hosts/{id}/status` exposes auth/reachability state.
- **Slice 3.4 — Bootstrap.** `POST /hosts/{id}/bootstrap` resolves the target release (default = latest non-draft, override via `{"tag":"vtest"}`), `gh release download`s the linux-x86_64 `.run` locally, `gh codespace cp`s the asset + an embedded helper script into `~/.local/share/tm-worker-farm/`, then runs `<asset>.run --accept -- --yes` over a single piped `gh codespace ssh -- bash`. Helper script hash is cached per host so re-bootstraps skip the helper cp.
- **Slice 3.5 — Remote spawn / stop / logs.** `internal/codespace` is a `Manager` analogous to `local.Manager` for codespace-backed hosts. `Spawn` ssh's `start_workers_local.sh` (embedded; piped over stdin), parses the per-run manifest inline between sentinel markers, mirrors it locally to `runs/codespace-<host-id>/<run-id>/manifest.json`. `Stop` sends a single ssh that SIGTERMs the PID and schedules a SIGKILL after `gracePeriod`. Liveness polls every 5 s with one batched `kill -0` ssh per host. Workers stuck in `stopping` past `gracePeriod + 10 s` are force-marked `exited` so the UI doesn't hang when the codespace becomes unreachable. Per-worker logs are tail-on-demand via `tail -n N` (auto-refreshed in the UI every 2 s). SSE log streaming for remote workers returns `501 Not Implemented` and is deferred to Phase 4.
- **Slice 3.6 — Polish + docs.** Per-worker purge dispatches by ownership; Codespace workers get a matching `Purge` (drops registry row + per-run bookkeeping; mirror manifest on disk is kept as audit trail). UI gets a release-tag input next to **Bootstrap tm-worker** and a **Purge all** button. Codespace troubleshooting and a smoke checklist live below.

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
| `POST` | `/workers/purge-all` | 200 with `{purged,skipped,failed}` JSON; purges every exited row across local + codespace backends |
| `GET` | `/workers/{id}/log?tail=N` | text/plain |
| `GET` | `/workers/{id}/log/stream` | text/event-stream (SSE; one frame per line) |
| `GET` | `/quarantine` | JSON array of `theirs` candidates from the startup scan |
| `POST` | `/quarantine/{run-id}/{NN}/{action}` | `action` ∈ `adopt`/`kill`/`ignore`; 204 on success |
| `GET` | `/hosts` | JSON array of inventory hosts with `supported` flag |
| `GET` | `/hosts/{id}/status` | per-host reachability/auth state |
| `POST` | `/hosts/{id}/bootstrap` | install `tm-worker` on a codespace host; body (optional) `{"repo":"OWNER/REPO","tag":"vX.Y.Z"}`; default tag = latest non-draft |

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

## Inventory (Phase 3)

The controller picks up an optional JSON inventory at:

- POSIX: `~/.config/tm-worker-farm/hosts.json` (or
  `$XDG_CONFIG_HOME/tm-worker-farm/hosts.json`).
- Windows: `%APPDATA%\tm-worker-farm\hosts.json`.

When the file is missing the controller synthesises a single
`local` host so the existing flag-driven UX keeps working.

```jsonc
{
  "hosts": [
    { "id": "local", "backend": "local" },
    {
      "id": "cs1",
      "backend": "codespace",
      "codespace": {
        "name": "glorious-space-acorn-97q979gjr9wr3pv5j",
        "worker_bin": "tm-worker",
        "config": "~/.config/task-messenger/tm-worker/config-worker.json"
      }
    }
  ]
}
```

`backend` is the discriminator; today only `local` and `codespace`
are wired up. Duplicate `id`s are rejected at startup with a typed
error naming the offending index. Write the file as **UTF-8 without
BOM** — PowerShell's `Set-Content -Encoding utf8` writes a BOM that
breaks the JSON decoder; use `[System.IO.File]::WriteAllText` with
`UTF8Encoding $false` instead.

## Codespace bootstrap

`POST /hosts/{id}/bootstrap` (button: **Bootstrap tm-worker**)
resolves the configured GitHub release, downloads the
`tm-worker-v*-linux-x86_64.run` asset locally, uploads it via
`gh codespace cp`, and runs `<asset>.run --accept -- --yes` over
`gh codespace ssh`. The release tag is read from the small input
next to the button (blank ⇒ latest non-draft, e.g. `vtest` for the
forced test release). Repository defaults to
`ilya-yusim/task-messenger`; override per-call by POSTing
`{"repo":"OWNER/REPO","tag":"vX.Y.Z"}` to the same endpoint.

The controller — not the codespace — runs `gh release download`,
because the user's local `gh` is typically authed for foreign-owner
repos and draft releases while the codespace's is not. Asset names
are discovered from `gh release view --json assets`, never
constructed from the tag (`workflow_dispatch` builds always ship
`tm-worker-v0.0.1-dev-...` regardless of the tag).

## Codespace troubleshooting

- **`gh codespace list` returns 403 / "admin rights".** The local
  `gh` is missing the `codespace` scope. Run
  `gh auth refresh -h github.com -s codespace`.
- **Bootstrap fails with `Unknown option: --yes`.** The bundled
  `install_linux.sh` inside the `.run` predates the `--yes` flag.
  Cut a fresh release (push or force-push `vtest`, or tag `vX.Y.Z`)
  and retry; the controller passes the tag verbatim to the
  bootstrap endpoint.
- **Spawn fails with `tm-worker: not found`.** Codespace hasn't been
  bootstrapped yet (or `tm-worker` is not on `$PATH`). The host
  status badge surfaces this; click **Bootstrap tm-worker**.
- **A worker sits in `stopping` forever.** The controller forces
  the transition to `exited` after `gracePeriod + 10 s` even when
  SSH to the codespace fails (paused codespace, gh hiccup). If you
  see one stuck longer than that, check the controller log for
  `codespace poll host=...:` errors.
- **Log window does not auto-update.** Codespace workers don't have
  SSE; the modal polls `/workers/{id}/log?tail=N` every 2 s while
  open. The header has a **Refresh** button and an **auto-refresh**
  checkbox if you want to pause polling.
- **Inventory parses but every host is `unsupported`.** The
  `hosts.json` likely starts with a UTF-8 BOM. Re-write without it
  (see Inventory section above).

## Codespace smoke checklist

After making any change to the codespace path, run through these
in order:

1. `meson compile -C builddir tm-worker-farm`, restart the
   controller, refresh the UI.
2. Select a codespace host in the dropdown; status badge reads
   `ok`.
3. Click **Bootstrap tm-worker** with the tag input blank
   (latest) — the status line should end with
   `bootstrap ok (tag=..., asset=tm-worker-v*-linux-x86_64.run)`.
4. Spawn 2 workers on the codespace host; both should reach
   `running` within ~5 s.
5. Open the log modal on each row; you should see worker startup
   output, and the modal should refresh every 2 s while the worker
   is running.
6. Click **Stop** on one row; it should transition `running` →
   `stopping` → `exited` within ~15 s.
7. Click **Stop all**; the remaining worker exits.
8. Click **Purge all**; both rows disappear from the table.
