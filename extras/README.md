# Extras

Auxiliary assets that are not part of the Task Messenger build itself
but ship alongside it: install scripts, distribution builders,
desktop integration files, launchers, and operational helpers.

## Subdirectories

| Path | Contents |
| --- | --- |
| [scripts/](scripts/) | Install / uninstall / packaging / deployment / profiling shell and PowerShell scripts. The scripts are self-documenting via comment headers. |
| [launchers/](launchers/) | Desktop and shell launchers that invoke the installed binaries with the right configuration paths. |
| [desktop/](desktop/) | `.desktop` files and icons for Linux desktop integration. |
| [worker_ui/](worker_ui/) | Optional terminal-UI launcher assets for the worker. |
| [docs/](docs/) | Operational documentation that does not belong inside a component (release process, deployment notes). |

## Install scripts (entry points)

The scripts most users run are:

| Script | Purpose |
| --- | --- |
| `scripts/install_linux.sh` | Install a Linux distribution into `~/.local/share` and `~/.config`. |
| `scripts/install_macos.sh` | Install a macOS distribution into `~/Library/Application Support/TaskMessenger`. |
| `scripts/install_windows.ps1` | Install a Windows distribution into `%LOCALAPPDATA%` and `%APPDATA%`. |
| `scripts/build_distribution.{sh,ps1}` | Build a Linux or Windows distribution archive plus a self-extracting installer. |
| `scripts/build_distribution_macos.sh` | Build a macOS distribution. |

End-user instructions for these scripts live in
[docs/INSTALLATION.md](../docs/INSTALLATION.md).

## Operational scripts

Selected helpers used outside of installation:

- `scripts/start_workers_local.{sh,ps1}` and
  `stop_workers_local.{sh,ps1}` — spawn and tear down a fleet of
  local workers without `tm-worker-farm`.
- `scripts/start_workers_codespace.ps1` and
  `stop_workers_codespace.ps1` — counterparts for a GitHub Codespace
  host.
- `scripts/install_tm_worker_codespace.ps1` and
  `install_tm_worker_release.sh` — bootstrap a worker into a remote
  host from a release artifact. The Codespace variant is also driven
  by `tm-worker-farm`'s bootstrap endpoint.
- `scripts/setup_gcp_deployer.ps1`,
  `scripts/cloud-init-rendezvous.yaml`,
  `scripts/create_rendezvous_vm.ps1` — provision a GCP VM running
  `tm-rendezvous`.
- `scripts/perf_record_all_threads.sh` and `perf_per_thread_flames.sh`
  — Linux perf-recording helpers (used by the VS Code tasks
  `Perf: Record Dispatcher` and `Perf: Record worker`).

## Related documentation

- Top-level overview: [README.md](../README.md).
- End-user install: [docs/INSTALLATION.md](../docs/INSTALLATION.md).
- Homebrew distribution: [homebrew/README.md](../homebrew/README.md).
