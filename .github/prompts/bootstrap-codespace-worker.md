# Bootstrap a codespace worker — agent hand-off

Use this prompt to provision a tm-worker on a GitHub Codespace and
attach it to a running rendezvous-backed network. The worker-farm
controller (`tm-worker-farm`) drives the bootstrap; this prompt lists
the pieces the agent needs to touch when the bootstrap path itself
needs maintenance.

## Inputs

- The codespace's name (as listed by `gh codespace list`).
- The release tag whose `tm-worker-*-linux-x86_64*.run` asset should be
  installed on the codespace.
- A worker config (`config-worker.json`) that points at the rendezvous
  service.

## Files of interest

- [worker-farm/README.md](../../worker-farm/README.md) — operator
  documentation.
- [worker-farm/internal/bootstrap/bootstrap.go](../../worker-farm/internal/bootstrap/bootstrap.go) —
  resolves the release asset, ships it via `gh codespace cp`, runs the
  installer over `gh codespace ssh`.
- [worker-farm/internal/gh/gh.go](../../worker-farm/internal/gh/gh.go) —
  thin wrapper around the `gh` CLI.
- [worker-farm/internal/codespace/codespace.go](../../worker-farm/internal/codespace/codespace.go) —
  remote backend; runs the same `start_workers_local.sh` /
  `stop_workers_local.sh` helpers as the local backend, piped through
  `bash -s`.
- [extras/scripts/install_tm_worker_codespace.ps1](../../extras/scripts/install_tm_worker_codespace.ps1) —
  reference implementation that the Go bootstrap mirrors.

## Steps the agent performs

1. Ensure `gh` is on `$PATH` and authenticated for the target
   codespace's repo.
2. Resolve the requested release tag and confirm the
   `tm-worker-*-linux-x86_64*.run` asset exists.
3. Use `bootstrap` to copy the `.run` plus the helper script onto the
   codespace and run the installer with `--accept`.
4. Use `codespace.Manager.Spawn` to start one or more workers on the
   codespace. The remote side runs `start_workers_local.sh` exactly
   like a local host would.
5. Confirm the workers register with the rendezvous service and pick
   up tasks.

## Verification

- `tm-worker-farm`'s UI lists the codespace host and shows live
  worker rows.
- The rendezvous dashboard
  ([services/rendezvous/README.md](../../services/rendezvous/README.md))
  shows the new workers connecting to the dispatcher.
- Stopping the controller does not stop the codespace workers — they
  are detached and persist across controller restarts.

## Out of scope

- Generic SSH and GCP-IAP backends are accepted by the inventory
  parser but not yet wired up.
- Streaming logs over SSE is not supported for codespace workers; the
  controller falls back to `/log?tail=N` polling.
