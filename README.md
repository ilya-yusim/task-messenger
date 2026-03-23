# Task Messenger

Task Messenger is a dispatcher/worker platform for streaming computational tasks from a central coordinator to a dynamic fleet of workers. It links directly against ZeroTier (`libzt`), so all transport flows through ZeroTier sockets and workers reach the dispatcher over a secure virtual network. The platform exposes asynchronous networking, coroutine-friendly session orchestration, and an optional worker UI that lets operators monitor and pause/resume work in real time.

## Subsystems

- **Dispatcher** (`dispatcher/`): Accepts worker connections, runs coroutine sessions, and coordinates task fan-out via `AsyncTransportServer`, `SessionManager`, and mock `TaskGenerator` integrations.
- **Workers** (`worker/`): Connect back to the dispatcher, execute tasks under pluggable runtimes (`BlockingRuntime`/`AsyncRuntime`), track metrics, and optionally expose a terminal UI using FTXUI.
- **Messaging Primitives** (`message/`): Defines `TaskMessage`, `TaskMessagePool`, and helpers that serialize payloads, enforce framing, and provide coroutine-friendly hand-off between producers and sessions.
- **Transport Layer** (`transport/`): Shared networking stack (coroutines, ZeroTier adapters, socket factories) powering both dispatcher and worker runtimes.

## System Flow (Mermaid)
```mermaid
graph LR
    App[Domain App / TaskGenerator] --> Pool[TaskMessagePool]
    Pool --> Dispatcher[Dispatcher Sessions]
    Dispatcher --> Transport[Async Transport Layer]
    Transport --> WorkerFleet[Workers]
    WorkerFleet --> Metrics[Metrics / UI / Logs]
    Workers[Workers] --> Results[Result Channels]
```

## Project Structure

```
task-messenger/
├── config/                     # Configuration files
│   ├── config-dispatcher.json     # Dispatcher configuration
│   ├── config-worker.json      # Worker configuration
│   └── vn-dispatcher-identity/    # Dispatcher ZeroTier identity files
│       ├── identity.public     # Public identity key
│       └── identity.secret     # Private identity key (secret)
├── dispatcher/                    # Dispatcher component
├── worker/                     # Worker component
├── message/                    # Messaging primitives
├── transport/                  # Transport layer
├── subprojects/                # Dependencies
└── extras/                     # Build and installation scripts
```

## Building

Task Messenger uses Meson as its build system.

### Build All Components (Dispatcher + Worker)

```bash
meson setup builddir --buildtype=release
meson compile -C builddir
```

### Build Only Dispatcher (no FTXUI dependency)

```bash
meson setup builddir-dispatcher -Dbuild_worker=false --buildtype=release
meson compile -C builddir-dispatcher
```

### Build Only Worker

```bash
meson setup builddir-worker -Dbuild_dispatcher=false --buildtype=release
meson compile -C builddir-worker
```

### Build Options

- `-Dbuild_dispatcher=true|false`: Build the dispatcher component (default: true)
- `-Dbuild_worker=true|false`: Build the worker component (default: true)
- `-Ddebug_logging=true|false`: Enable debug logging (default: false)
- `-Dprofiling_unwind=true|false`: Enable profiling-friendly unwind flags (default: false)

## Creating Distribution Packages

Task Messenger provides automated scripts to build distribution packages for deployment:

### Windows Distributions

```powershell
# Build dispatcher distribution (ZIP + self-extracting installer)
.\extras\scripts\build_distribution.ps1 -Component dispatcher

# Build worker distribution (ZIP + self-extracting installer)
.\extras\scripts\build_distribution.ps1 -Component worker

# Build both
.\extras\scripts\build_distribution.ps1 -Component dispatcher
.\extras\scripts\build_distribution.ps1 -Component worker
```

**Output Files** (in `dist/` directory):
- `task-messenger-{component}-v{version}-windows-x64-installer.exe` - Self-extracting installer
- `.sha256` checksum file

The self-extracting installer automatically extracts and runs the installation script, providing a one-click installation experience.

### Linux Distributions

```bash
# Build dispatcher distribution
./extras/scripts/build_distribution.sh --component dispatcher

# Build worker distribution
./extras/scripts/build_distribution.sh --component worker

# Build both
./extras/scripts/build_distribution.sh --component dispatcher
./extras/scripts/build_distribution.sh --component worker
```

**Output Files** (in `dist/` directory):
- `task-messenger-{component}-v{version}-linux-x64.tar.gz` - Compressed tarball
- `.sha256` checksum file

## Configuration

Configuration files are located in the `config/` directory:
- `config-dispatcher.json`: Dispatcher settings including ZeroTier network ID and identity path
- `config-worker.json`: Worker settings
- `vn-dispatcher-identity/`: Dispatcher's ZeroTier identity directory (only identity.public and identity.secret are version-controlled)

## Installation

Task Messenger provides distribution packages for both dispatcher and worker components. Installation scripts follow XDG directory standards:

### Windows Installation Paths

**Binaries** (in `%LOCALAPPDATA%`):
- Dispatcher: `%LOCALAPPDATA%\TaskMessenger\tm-dispatcher\tm-dispatcher.exe`
- Worker: `%LOCALAPPDATA%\TaskMessenger\tm-worker\tm-worker.exe`

**Configuration and Identity** (in `%APPDATA%` - roaming):
- Dispatcher config: `%APPDATA%\TaskMessenger\tm-dispatcher\config-dispatcher.json`
- Dispatcher identity: `%APPDATA%\TaskMessenger\tm-dispatcher\vn-dispatcher-identity\`
- Worker config: `%APPDATA%\TaskMessenger\tm-worker\config-worker.json`

**Installation:**
```powershell
# Extract distribution archive, then run from extracted directory:
.\extras\scripts\install_windows.ps1

# Or specify archive manually:
.\extras\scripts\install_windows.ps1 -Archive tm-dispatcher-v1.0.0-windows-x64.zip
```

**Uninstallation:**
```powershell
# From installation directory:
.\uninstall_windows.ps1

# Or run from extras/scripts:
.\extras\scripts\uninstall_windows.ps1 -Component dispatcher
```

### Linux Installation Paths

**Binaries** (in `~/.local/share`):
- Dispatcher: `~/.local/share/task-messenger/tm-dispatcher/bin/tm-dispatcher`
- Worker: `~/.local/share/task-messenger/tm-worker/bin/tm-worker`

**Configuration and Identity** (in `~/.config` - XDG standard):
- Dispatcher config: `~/.config/task-messenger/tm-dispatcher/config-dispatcher.json`
- Dispatcher identity: `~/.config/task-messenger/tm-dispatcher/vn-dispatcher-identity/`
- Worker config: `~/.config/task-messenger/tm-worker/config-worker.json`

**Installation:**
```bash
# Extract distribution archive, then run from extracted directory:
./extras/scripts/install_linux.sh

# Or specify archive manually:
./extras/scripts/install_linux.sh --archive tm-dispatcher-v1.0.0-linux-x64.tar.gz
```

**Uninstallation:**
```bash
./extras/scripts/uninstall_linux.sh --component dispatcher
```

## Documentation
- Generated API/user docs: `meson compile -C builddir-dispatcher docs` then open `builddir-dispatcher/doxygen/html/index.html`.
- High-level modules: see `docs/TaskMessenger.md`, `dispatcher/README.md`, `worker/README.md`, and the README files inside `message/` and `transport/`.
