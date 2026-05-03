# Socket Layer

Role-based socket interfaces and implementations for both non-blocking and blocking I/O. This layer decouples higher-level logic from concrete backends (e.g., ZeroTier/libzt) via small, composable interfaces and a factory.

## Components

- `ISocketLifecycle`: Common lifecycle primitives (`close()`, `is_open()`, endpoint accessors).
- `IAsyncStream`: Non-blocking read/write stream API (`try_read`, `try_write`).
- `IBlockingStream`: Blocking read/write/connect counterparts.
- `IClientSocket`: Client connect interface with blocking semantics.
- `IServerSocket`: Server role providing `start_listening` (non-throwing, bool-returning) and a unified timed `accept` that returns `shared_ptr<IClientSocket>`. The factory controls whether children are non-blocking (`IAsyncStream`) or blocking (`IBlockingStream`).
- `SocketFactory`: Resolves a concrete backend at runtime and constructs role objects.
- `SocketTypeOptions`: Registers and retrieves backend type options (CLI/JSON integration).
- `message/TaskMessage.hpp`: Lightweight framing structures (via `TaskHeader`) used by higher layers when exchanging task payloads.
- `zerotier/`: Concrete backend using ZeroTier (libzt), implementing stream + server roles.

## Design Principles

- **Role separation**: Small interfaces for specific responsibilities keep implementations simple and testable.
- **Non-blocking semantics**: `try_*` methods return a boolean completion status. They clear `std::error_code` on pending states and set it only on non-transient failures.
- **Blocking semantics**: Blocking methods report completion through `std::error_code` and never throw for normal I/O errors.
- **Timed accept**: `IServerSocket::accept` provides a timed, responsive accept. Backends use native timed accept (e.g., SO_RCVTIMEO on ZeroTier/lwIP) to keep the thread nearly idle under no load while bounding shutdown latency. The factory determines child socket mode (non-blocking or blocking).
- **Backend encapsulation**: Factories and options hide library-specific headers and flags from higher layers.

## Typical Flows

### Timed Accept (Non-Blocking Children)
```cpp
#include "SocketFactory.hpp"
#include "IServerSocket.hpp"
#include "IAsyncStream.hpp"
#include <system_error>

std::error_code ec;
auto server = SocketFactory::create_server(/*logger*/ nullptr);
if (!server->start_listening("0.0.0.0", 5555, /*backlog*/ 128)) {
  return;
}

while (!shutdown_requested()) {
    auto client = server->accept(ec);
    if (client) {
        // Factory created a NonBlocking server → children are IAsyncStream
        auto stream = std::dynamic_pointer_cast<IAsyncStream>(client);
        // Handle connected client (non-blocking I/O)
    } else if (ec) {
        break; // Non-transient failure
    }
    // Timeout or transient; loop and re-check shutdown flag
}
```

### Timed Accept (Blocking Children)
```cpp
#include "SocketFactory.hpp"
#include "IServerSocket.hpp"
#include "IBlockingStream.hpp"
#include <system_error>

std::error_code ec;
auto server = SocketFactory::create_blocking_server(/*logger*/ nullptr);
if (!server->start_listening("0.0.0.0", 5555, 128)) {
  return;
}

while (!shutdown_requested()) {
    auto client = server->accept(ec);
    if (client) {
        // Factory created a Blocking server → children are IBlockingStream
        auto stream = std::dynamic_pointer_cast<IBlockingStream>(client);
        // Use blocking read()/write() directly
    } else if (ec) {
        break;
    }
}
```

### Non-Blocking Client Connect + I/O
```cpp
#include "SocketFactory.hpp"
#include "IAsyncStream.hpp"
#include <system_error>

std::error_code ec;
auto conn = SocketFactory::create_async_client("zerotier");
conn->connect("10.0.0.2", 8080, ec);
if (ec) { /* handle failure */ }

char buf[1024]; size_t n = 0;
auto stream = std::static_pointer_cast<IAsyncStream>(conn);
if (stream->try_read(buf, sizeof(buf), n, ec)) {
    if (!ec) { /* consumed n bytes */ }
}
```

## Options and Factory

`SocketTypeOptions` integrates backend selection with your app’s configuration. Typical providers:

- CLI: `--socket-type zerotier`
- JSON: `{ "socket": { "type": "zerotier" } }`

`SocketFactory` inspects the configured type and returns appropriate implementations for server/client/stream roles. See the ZeroTier backend for a concrete example.

## Frames

`message/TaskMessage.hpp` exposes the `TaskHeader` POD used for both request and response framing. It remains transport-agnostic and can be serialized over any `IAsyncStream`/`IBlockingStream` implementation.

## Mermaid: Server Accept Overview

```mermaid
sequenceDiagram
  participant App
  participant Server as IServerSocket
  participant Client as IClientSocket
  App->>Server: start_listening(host, port, backlog)
  loop Accept Loop
    App->>Server: accept(ec)
    alt Available
      Server-->>App: IClientSocket (connected)
      App->>App: dynamic_pointer_cast to IAsyncStream or IBlockingStream
    else Timeout / Transient
      Server-->>App: nullptr (ec cleared)
    else Error
      Server-->>App: nullptr (ec set)
      App-->>App: handle fatal
    end
  end
```

## Backend: ZeroTier

The `zerotier/` directory provides a libzt-based implementation:
- `ZeroTierSocket`: `IAsyncStream` + `IBlockingStream` + server accept.
- `ZeroTierNodeService`: Starts/stops the node and manages network joins via RAII leases.
- `ZeroTierErrnoCompat`: Normalizes libzt/lwIP errno values and provides helpers.

See `zerotier/README.md` for detailed design, configuration, and examples.

## Documentation

This directory is included in the Doxygen target. Many headers use `\ingroup socket_backend` for grouping. Regenerate docs with:

```powershell
meson compile -C builddir docs
```

## Extending the Socket Layer

1. Define a new backend directory (e.g., `transport/socket/<backend>`).
2. Implement the relevant roles (`IAsyncStream`, `IBlockingStream`, `IServerSocket`, etc.).
3. Register the backend in `SocketTypeOptions` and plug it into `SocketFactory`.
4. Add Doxygen `\file` headers and tag the new code with the socket group.

## Related documentation

- Parent component: [transport/README.md](../README.md).
- Coroutine adapter wrapping these interfaces: [transport/coro/README.md](../coro/README.md).
- ZeroTier backend: [transport/socket/zerotier/README.md](zerotier/README.md).
