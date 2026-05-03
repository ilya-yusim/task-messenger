# tm-worker-farm

`tm-worker-farm` is a controller and Web UI for running multiple
`tm-worker` processes from one place — locally, on a GitHub
Codespace, or on a future remote backend.

This document has two sections:

- [Contribute compute](#contribute-compute) — for end users who want
  to run workers and join a Task Messenger network.
- [Developer reference](#developer-reference) — for contributors
  changing the controller's behaviour.

---

## Contribute compute

### What it does

Start the controller, open the bundled Web UI in a browser, point it
at a dispatcher, and click **Start** to spawn a chosen number of
worker processes. The controller tracks each worker, surfaces its
log, and lets you stop or purge workers individually or in bulk.

### Run it

```sh
./tm-worker-farm                       # listen on 127.0.0.1:8090
./tm-worker-farm --port 9000           # alternate port
./tm-worker-farm --worker-bin /path/to/tm-worker
./tm-worker-farm --config /path/to/config-worker.json
./tm-worker-farm --restart-last        # respawn the most recent run
```

Then open <http://127.0.0.1:8090/> in a browser.

### Run workers locally or on a Codespace

Inventory hosts are configured through a JSON file at
`~/.config/tm-worker-farm/hosts.json` (POSIX) or
`%APPDATA%\tm-worker-farm\hosts.json` (Windows). When the file is
missing the controller synthesises a single `local` host so the
default flow keeps working.

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

`backend` is the discriminator; `local` and `codespace` are wired up
today. Write the file as **UTF-8 without BOM** — PowerShell's
`Set-Content -Encoding utf8` writes a BOM that breaks the JSON
decoder; use `[System.IO.File]::WriteAllText` with
`UTF8Encoding $false` instead.

For a Codespace host, the **Bootstrap tm-worker** button installs a
release into the codespace, then **Start** spawns workers there. The
release tag input next to the button is blank for the latest
non-draft release, or set to a specific tag (e.g. `vtest`).

SSH and other remote backends are roadmap items.

### Codespace troubleshooting

- **`gh codespace list` returns 403 / "admin rights".** The local
  `gh` is missing the `codespace` scope. Run
  `gh auth refresh -h github.com -s codespace`.
- **Bootstrap fails with `Unknown option: --yes`.** The bundled
  `install_linux.sh` inside the `.run` predates the `--yes` flag.
  Cut a fresh release and retry.
- **Spawn fails with `tm-worker: not found`.** The codespace has not
  been bootstrapped yet. Click **Bootstrap tm-worker**.
- **A worker sits in `stopping` forever.** The controller forces the
  transition to `exited` ten seconds past the grace period when SSH
  to the codespace fails. If a worker stays stuck longer, check the
  controller log for `codespace poll host=...:` errors.
- **Log window does not auto-update.** Codespace workers do not
  expose live SSE; the modal polls the tail endpoint every two
  seconds while open. Use the **Refresh** button or **auto-refresh**
  checkbox to control polling.
- **Inventory parses but every host is `unsupported`.** The
  `hosts.json` likely starts with a UTF-8 BOM. Re-write without it.

---

## Developer reference

### Build

Requires Go 1.22+.

```sh
cd worker-farm
go build -o tm-worker-farm ./cmd/tm-worker-farm
```

Or via Meson, gated behind `-Dbuild_worker_farm=true` (default off so
contributors who don't touch this component don't need a Go
toolchain):

```sh
meson setup builddir -Dbuild_worker_farm=true
meson compile -C builddir tm-worker-farm
```

Web assets are embedded via `embed.FS`; no separate static-asset
deployment.

### HTTP API

| Method | Path | Notes |
| --- | --- | --- |
| `GET` | `/healthz` | `200 ok`. |
| `GET` | `/workers` | JSON array, ordered by start time. |
| `POST` | `/workers` | body `{"count": N, "args": ["..."]}`; returns `{"workers":[{"id","ok","pid","error"}]}`; 207 on partial failure. |
| `GET` | `/workers/{id}` | Single worker JSON. |
| `POST` | `/workers/{id}/stop` | 204 on completion. |
| `POST` | `/workers/{id}/purge` | 204; deletes log/pid/sentinel for an exited worker; 400 if still running. |
| `POST` | `/workers/stop-all` | 204. |
| `POST` | `/workers/purge-all` | 200 with `{purged,skipped,failed}`; purges every exited row across all backends. |
| `GET` | `/workers/{id}/log?tail=N` | text/plain. |
| `GET` | `/workers/{id}/log/stream` | text/event-stream (SSE; one frame per line). Local backend only — codespace returns 501. |
| `GET` | `/quarantine` | JSON array of foreign-controller candidates from the startup adoption scan. |
| `POST` | `/quarantine/{run-id}/{NN}/{action}` | `action` ∈ `adopt`/`kill`/`ignore`; 204 on success. |
| `GET` | `/hosts` | JSON array of inventory hosts with `supported` flag. |
| `GET` | `/hosts/{id}/status` | Per-host reachability/auth state. |
| `POST` | `/hosts/{id}/bootstrap` | Install `tm-worker` on a codespace host; body (optional) `{"repo":"OWNER/REPO","tag":"vX.Y.Z"}`; default tag is the latest non-draft release. |

### Runtime architecture

- A single `tm-worker-farm` instance is enforced via a pidfile under
  the cache directory.
- Per-run state lives under
  `<cache>/runs/<run-id>/{manifest.json, worker-NN.log, worker-NN.pid, worker-NN.adopt}`,
  with `<run-id>` of the form `YYYYMMDD-HHMMSS` (with a `-N` suffix
  on collision). `runs/latest.txt` points at the most recent.
- The cache root is `%LOCALAPPDATA%\tm-worker-farm` on Windows and
  `~/.cache/tm-worker-farm` (or `$XDG_CACHE_HOME/tm-worker-farm`) on
  POSIX. A legacy `tm-worker-controller` directory is detected and
  warned about on startup; there is no automatic migration.
- The controller persists its identity in `identity.json` (`ctl-<16hex>`
  with a `previous_ids` history) so adopted workers from prior
  installs can still be recognised.
- Workers outlive the controller by design. POSIX uses `Setsid: true`
  to detach from the controller's session. Windows uses
  `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`. As a consequence on
  Windows there is no console-control channel, so `Stop` is
  `TerminateProcess` for both forked and adopted workers.

### Adoption and quarantine

At startup the controller scans `runs/*/worker-NN.adopt` sentinels and
classifies each PID:

- **mine** — `controller_id` matches the current ID or any
  `previous_ids` entry and the PID is alive. Auto-adopted; polled
  every 2 s for liveness.
- **stale** — sentinel exists, PID is dead. Registered as `exited`
  so the UI can surface the corpse.
- **theirs** — `controller_id` belongs to a different install and
  the PID is alive. Quarantined; the operator chooses
  `adopt`/`kill`/`ignore` from the UI panel.

Liveness probe (used by the supervise loop and the quarantine list
refresh):

- POSIX: `os.FindProcess` + `Signal(0)`. `ESRCH` ⇒ dead, `EPERM` ⇒
  alive (different uid).
- Windows: `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) +
  GetExitCodeProcess`. Alive iff `STILL_ACTIVE (259)`.

Adopted-worker termination uses SIGTERM-then-SIGKILL on POSIX and
`TerminateProcess` directly on Windows.

### Codespace backend internals

- `internal/gh` shells out to `gh codespace ssh` and `gh codespace
  cp` only — never `bash -lc`. Typed errors (`MissingBinaryError`,
  `NotLoggedInError`, `NeedsCodespaceScopeError`, `NotFoundError`)
  carry remediation hints.
- `POST /hosts/{id}/bootstrap` runs `gh release download` locally,
  uploads the `.run` and an embedded helper script via
  `gh codespace cp`, then runs `<asset>.run --accept -- --yes` over
  a single piped `gh codespace ssh -- bash`. Helper-script hash is
  cached per host so repeat bootstraps skip the helper upload.
- The controller — not the codespace — runs `gh release download`,
  because the user's local `gh` is typically authed for foreign-owner
  repos and draft releases while the codespace's is not. Asset names
  are read from `gh release view --json assets`, never reconstructed
  from the tag.
- Spawn over SSH pipes `start_workers_local.sh` to `bash`, parses
  the per-run manifest between sentinel markers, and mirrors it
  locally to `runs/codespace-<host-id>/<run-id>/manifest.json`.
- Liveness polls every five seconds via one batched `kill -0` SSH
  per host. Workers stuck `stopping` past `gracePeriod + 10s` are
  force-marked `exited` so the UI does not hang when the codespace
  is unreachable.
- Per-worker logs use `tail -n N` on demand; the UI auto-refreshes
  every two seconds while the modal is open.

### Default worker config

`tm-worker-farm` uses the same default worker config that
`extras/scripts/install_linux.sh` and `install_windows.ps1` install:

- POSIX: `~/.config/task-messenger/tm-worker/config-worker.json`
- Windows: `%APPDATA%\task-messenger\tm-worker\config-worker.json`

### Codespace smoke checklist

After making any change to the codespace path, run through these in
order:

1. `meson compile -C builddir tm-worker-farm`, restart the
   controller, refresh the UI.
2. Select a codespace host in the dropdown; status badge reads `ok`.
3. Click **Bootstrap tm-worker** with the tag input blank — the
   status line should end with
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
