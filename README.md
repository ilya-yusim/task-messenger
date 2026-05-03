# Task Messenger

Task Messenger is a distributed computational network. A **dispatcher**
hands out tasks to a fleet of **workers** that connect from anywhere on
the internet over a private ZeroTier virtual network. A central
**rendezvous** service publishes a network-wide dashboard so end users
can see what the network is doing and contribute compute. Tasks are
described by **skills**: typed compute kernels authored as a `.skill.toml`
schema and a small C++ implementation file.

## Who is this for?

| Role | What you do | Start here |
| --- | --- | --- |
| Network operator | Run a `tm-rendezvous` instance and publish its address. | [services/rendezvous/](services/rendezvous/) |
| Dispatcher operator | Run an algorithm that produces tasks and consumes results. | [dispatcher/README.md](dispatcher/README.md), [generators/](generators/) |
| Compute contributor | Run one or more `tm-worker` processes that join the network. | [worker/README.md](worker/README.md), [worker-farm/README.md](worker-farm/README.md) |
| Skill author | Add a new compute kernel to the platform. | [skills/README.md](skills/README.md) |

## Components

| Component | Description |
| --- | --- |
| [dispatcher/](dispatcher/README.md) | The `tm-dispatcher` server. Accepts worker connections, runs an algorithm-supplied generator, fans tasks out to workers, and serves a per-dispatcher monitoring dashboard. |
| [worker/](worker/README.md) | The `tm-worker` process. Connects to a dispatcher, executes skills, and reports metrics. Ships with an optional terminal UI. |
| [services/rendezvous/](services/rendezvous/) | The `tm-rendezvous` server. Brokers dispatcher discovery on the ZeroTier network and serves the network-wide end-user dashboard. |
| [worker-farm/](worker-farm/README.md) | `tm-worker-farm`, a Go-based controller and Web UI for running multiple workers locally or on remote backends. |
| [generators/](generators/) | Built-in task generators that drive a dispatcher (interactive REPL, auto-refill load generator). Placeholders for user-authored algorithms. |
| [skills/](skills/README.md) | The skill framework: public authoring API, built-in skills, and the registry/codegen backend. |
| [transport/](transport/README.md) | Coroutine-aware ZeroTier networking shared by every component. |
| [message/](message/README.md) | `TaskMessage` framing and queues used between generators, dispatchers, and workers. |
| [dashboard/](dashboard/README.md) | Browser assets served by both the dispatcher and rendezvous monitoring endpoints. |
| [config/](config/) | Sample configuration files for each component. |
| [extras/](extras/) | Install scripts, launchers, and packaging helpers. |
| [homebrew/](homebrew/README.md) | Homebrew tap and formulas for macOS distribution. |
| [docs/](docs/) | Cross-cutting documentation: installation guide, Doxygen mainpage. |

## Quick start

1. **Install or build.** End users install via a packaged distribution
   (Homebrew on macOS, installer scripts on Windows and Linux). See
   [docs/INSTALLATION.md](docs/INSTALLATION.md). Developers build from
   source with Meson; see [Building from source](#building-from-source)
   below.
2. **Pick a role.** Start a rendezvous, run a dispatcher with a
   generator, or join an existing network as a worker. The component
   READMEs in the table above each include their own quick-start.
3. **Open the dashboard.** The rendezvous service serves the
   network-wide dashboard; each dispatcher additionally exposes its own
   local monitoring page. See [dashboard/README.md](dashboard/README.md).

## Building from source

Task Messenger uses [Meson](https://mesonbuild.com/) and a set of vendored
subprojects under [subprojects/](subprojects/README.md).

```bash
meson setup builddir --buildtype=release
meson compile -C builddir
```

Per-component Meson options (`build_dispatcher`, `build_worker`,
`build_generators`, `enable_blas_skills`, etc.) and full toolchain
requirements are documented in each component README and in
[meson_options.txt](meson_options.txt).

## Documentation

- [docs/INSTALLATION.md](docs/INSTALLATION.md) — packaged distribution
  install/uninstall steps for Windows, Linux, and macOS.
- [docs/TaskMessenger.md](docs/TaskMessenger.md) — Doxygen `\mainpage`
  entry point. Build with `meson compile -C builddir docs` and open
  `builddir/doxygen/html/index.html`.
- Component READMEs (linked above) — scoped reference for each
  subsystem.

## License

See [LICENSE](LICENSE).
