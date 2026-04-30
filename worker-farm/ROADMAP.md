# tm-worker-farm distribution roadmap

> **Goal:** make `tm-worker-farm` the primary user interface for running
> `tm-worker` on a local machine. Users install one bundle and get a small
> web UI that spawns/monitors workers.

This is the deferred plan. Today (April 2026) `tm-worker-farm` builds
locally via Meson with `-Dbuild_worker_farm=true` (default off) and is
**not** part of any release artifact. The default is off so CI runners
without a Go toolchain — notably the macOS-ARM64 release matrix — can
still configure cleanly.

## Phase A — Bundle into the worker artifact

- Ship `tm-worker-farm[.exe]` **inside** the existing `tm-worker`
  distribution (one ZIP/installer/run/command bundle), not as a separate
  component.
- Rationale: matches the product framing — the user installs "the
  worker" and the farm UI comes along for free.
- Affected matrix: only the `worker` job needs the new toolchain;
  `dispatcher` and `rendezvous` jobs stay untouched.

## Phase B — CI Go toolchain

- Add `actions/setup-go@v5` with `go-version: '1.22'` to
  [.github/workflows/release.yml](../.github/workflows/release.yml).
- Gate the step on `matrix.component == 'worker'` so the dispatcher and
  rendezvous jobs don't pay the cost.
- Pass `-Dbuild_worker_farm=true` only on the worker job's `meson setup`.

## Phase C — Distribution scripts

Update each platform's distribution builder to stage the farm binary
next to the worker:

- [extras/scripts/build_distribution.ps1](../extras/scripts/build_distribution.ps1)
- [extras/scripts/build_distribution.sh](../extras/scripts/build_distribution.sh)
- [extras/scripts/build_distribution_macos.sh](../extras/scripts/build_distribution_macos.sh)

Layout (Windows example):

```
tm-worker/
  bin/
    tm-worker.exe
    tm-worker-farm.exe        # NEW
    zt-shared.dll
    libopenblas.dll
  config/
    config-worker.json
  scripts/
    install_windows.ps1
    uninstall_windows.ps1
```

The Windows installer
([extras/scripts/install_windows.ps1](../extras/scripts/install_windows.ps1))
should:

- Copy `tm-worker-farm.exe` alongside `tm-worker.exe`.
- Add a second Start Menu shortcut **"Worker Farm"** that launches
  `tm-worker-farm.exe`. No CLI args needed once Phase D lands (farm
  auto-discovers the sibling worker binary).
- Optionally: register the farm's listening URL (`http://127.0.0.1:8090/`)
  somewhere discoverable, or open a browser on first launch.

Linux `.run` and macOS `.command` get analogous launchers.

## Phase D — Runtime worker discovery

Currently `tm-worker-farm`'s `--worker-bin` default is `$PATH` lookup
with an OS-specific fallback. Change it to:

1. Sibling of the farm executable (`<exe-dir>/tm-worker[.exe]`).
2. Then `$PATH` lookup.
3. Then OS fallback.

This makes "install both → double-click the farm" Just Work without the
user setting up `$PATH`. Likely a tiny change in
[worker-farm/internal/local/manager.go](../worker-farm/internal/local/manager.go)
or wherever the `--worker-bin` default is resolved.

## Phase E — Homebrew

Decision point:
scm-history-item:c%3A%5CUsers%5Ciyusi%5Cprojects%5Ctask-messenger?%7B%22repositoryId%22%3A%22scm0%22%2C%22historyItemId%22%3A%22220073fcdacba0000e721e7bc74375936a9c6ba4%22%2C%22historyItemParentId%22%3A%22a503be7ed5efcdbd4e5a2b4ab8deb372981bdfe9%22%2C%22historyItemDisplayId%22%3A%22220073f%22%7D
- **Option 1:** new `tm-worker-farm.rb` formula (Go-only build,
  `depends_on "go" => :build`).
- **Option 2:** extend `tm-worker.rb` to compile both — installs
  `tm-worker` (C++) and `tm-worker-farm` (Go) together. Matches Phase A.

Option 2 keeps the user-facing story consistent ("install the worker,
get the farm UI"). Update the auto-bump step in `release.yml` accordingly.

## Phase F — Docs & UX

- Reframe [worker-farm/README.md](../worker-farm/README.md) as the
  primary worker UX, not a developer tool.
- Update [README.md](../README.md) and the worker docs to recommend
  starting workers via the farm.
- Add a screenshot of the web UI to the README.
- Mention the farm in [extras/docs/INSTALLATION.md](../docs/INSTALLATION.md)
  if/when that file documents the worker install flow.

## Out-of-scope (for now)

- Standalone `tm-worker-farm` artifact (separate ZIP). Re-evaluate only
  if there's demand for the controller without a co-located worker.
- Cross-machine farm (controller on host A spawning workers on host B).
- Authentication / TLS for the farm web UI. Today it binds to loopback;
  any move toward non-loopback exposure must add auth first.
