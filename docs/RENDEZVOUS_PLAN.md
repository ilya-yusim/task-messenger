# Plan: Rendezvous Service for Task Messenger

A lightweight `tm-rendezvous` service that joins the ZeroTier network via libzt, accepts
dispatcher registration, serves endpoint discovery, and co-hosts the monitoring dashboard.
**All generator/worker ↔ rendezvous traffic uses framed JSON over `ZeroTierSocket`**.
cpp-httplib is only used inside the rendezvous service to serve the browser dashboard.

## Decisions

- **Security** — deferred; ZT network membership is the trust boundary
- **Multi-generator** — single-dispatcher for now
- **Heartbeat/TTL** — implement from start; snapshot pushes serve as heartbeats; 30 s TTL
- **Dashboard assets** — move to `dashboard/` at repo root
- **ZT joining** — libzt in-process (like worker/dispatcher)
- **Directory** — `services/rendezvous/` for the binary; `rendezvous/` for the shared client library

---

## Transport Design

### Wire protocol — length-prefixed JSON over `ZeroTierSocket` TCP

```
[4 bytes: payload length (LE)] [1 byte: message type] [JSON payload]
```

| Type | Name                 | Direction              | Payload                                                         |
|------|----------------------|------------------------|-----------------------------------------------------------------|
| 1    | REGISTER_REQUEST     | Generator → Rendezvous | `{"address":"...","port":N}`                                    |
| 2    | REGISTER_RESPONSE    | Rendezvous → Generator | `{"ok":true}`                                                   |
| 3    | UNREGISTER_REQUEST   | Generator → Rendezvous | `{}`                                                            |
| 4    | UNREGISTER_RESPONSE  | Rendezvous → Generator | `{"ok":true}`                                                   |
| 5    | DISCOVER_REQUEST     | Worker → Rendezvous    | `{}`                                                            |
| 6    | DISCOVER_RESPONSE    | Rendezvous → Worker    | `{"address":"...","port":N,"stale":false}` or `{"found":false}` |
| 7    | REPORT_SNAPSHOT      | Generator → Rendezvous | `{...monitoring snapshot...}`                                   |
| 8    | REPORT_ACK           | Rendezvous → Generator | `{"ok":true}`                                                   |

### Network map

| Traffic                                        | Network     | Socket layer                  |
|------------------------------------------------|-------------|-------------------------------|
| Generator → Rendezvous (register, snapshot)    | ZeroTier    | `zts_*` via `ZeroTierSocket`  |
| Worker → Rendezvous (discover)                 | ZeroTier    | `zts_*` via `ZeroTierSocket`  |
| Browser → Rendezvous (dashboard)               | Regular TCP | OS sockets via cpp-httplib    |

---

## Phase 0 — Dashboard Asset Relocation

*No dependencies.  Parallel with Phase 1.*

1. Move `dispatcher/monitoring/dashboard/` → `dashboard/` at repo root
2. Update `MonitoringService::resolve_dashboard_dir()` to probe `<repo>/dashboard`
   (dev) and `<bindir>/dashboard` (installed)
3. Update install rules if applicable
4. Verify dashboard still works from the generator

**Files:** `dashboard/` (moved), `dispatcher/monitoring/MonitoringService.cpp`

---

## Phase 1 — Wire Protocol + Shared Client Library (`rendezvous/`)

*No dependencies.  Parallel with Phase 0.*

1. `rendezvous/RendezvousProtocol.hpp` — message type enum, frame header struct,
   read/write helpers for framed JSON over `IBlockingStream`
2. `rendezvous/RendezvousClient.hpp/.cpp`:
   - Constructor: `(rendezvous_zt_host, rendezvous_zt_port, logger)`
   - Opens a `ZeroTierSocket` (`IBlockingStream`) to the rendezvous service
   - `register_endpoint(address, port) → bool`
   - `unregister_endpoint() → bool`
   - `discover_endpoint() → std::optional<Endpoint>` (returns `{address, port, stale}`)
   - `report_snapshot(json_string) → bool`
   - Short-lived connections (connect, send request, read response, close)
   - 2 s connect timeout, 3 retries with 1 s backoff
3. `rendezvous/RendezvousOptions.hpp/.cpp` — auto-registering provider:
   - JSON: `"rendezvous": {"enabled": false, "host": "", "port": 8088}`
   - CLI: `--rendezvous-enabled`, `--rendezvous-host`, `--rendezvous-port`
4. `rendezvous/meson.build` — static library, depends on `transport_dep`, `json_dep`,
   `shared_dep`
5. Wire `rendezvous_dep` into root `meson.build`

---

## Phase 2 — Rendezvous Service Binary (`services/rendezvous/`)

*Depends on Phase 0 (dashboard location) and Phase 1 (protocol + options).*

Two listener threads in `RendezvousServer`:

1. **ZT listener** — `ZeroTierSocket` in server mode, handles rendezvous protocol
   connections.  On each accepted connection: read message type + JSON, dispatch,
   write response, close.
2. **HTTP listener** — cpp-httplib `Server` on regular TCP (e.g. `0.0.0.0:9090`):
   - `GET /` — dashboard static assets
   - `GET /api/monitor` — cached snapshot JSON (or 503)
   - `GET /healthz` — liveness probe

In-memory state (single mutex):
- `optional<RegisteredEndpoint> {address, port, last_seen}`
- `string last_snapshot_json_`
- `steady_clock::time_point last_report_time_`
- TTL: if `now - last_report_time_ > 30 s`, mark endpoint stale

`main.cpp`: parse CLI via shared `Options`, join ZT lazily via `ZeroTierSocket`,
start both listeners, block until signal.

---

## Phase 3 — Generator-Side Integration

*Depends on Phase 1.*

1. Add `rendezvous_dep` to `dispatcher/meson.build`
2. `DispatcherApp::start()` — after transport binds, if rendezvous enabled:
   - Get ZT IP via `ZeroTierNodeService::instance().get_ip_v4(net_id)`
   - `register_endpoint(zt_ip, listen_port)`
3. `MonitoringService` — after each local snapshot build, fire-and-forget
   `report_snapshot(json)` if client available
4. `DispatcherApp::stop()` — best-effort `unregister_endpoint()`

---

## Phase 4 — Worker-Side Integration

*Depends on Phase 1.*

1. Add `rendezvous_dep` to `worker/meson.build`
2. `WorkerSession::start()` — before connection loop, if rendezvous enabled:
   - `discover_endpoint()` → override `host_`/`port_`
   - Fall back to config values if discovery fails
3. On reconnect after I/O error, re-discover in case generator moved

---

## Phase 5 — Config & Documentation

1. Add `"rendezvous"` section to `config/config-dispatcher.json` and
   `config/config-worker.json`
2. Add config template for the rendezvous service itself
3. Update docs

---

## Phase 6 — Transport Interface Cleanup

*No dependencies on Phases 3–5.  Can be done at any point after Phase 2.*

### Problem

`RendezvousServer` must reference `ZeroTierSocket` directly because no abstract
interface combines server listen/accept with blocking-stream delivery:

- `IServerSocket::blocking_accept()` returns `shared_ptr<IAsyncStream>`, which
  does not expose blocking `read()`/`write()`.  The server must
  `dynamic_pointer_cast<IBlockingStream>` on every accepted connection.
- `IAsyncStream` inherits from `IServerSocket` — semantically wrong; a connected
  stream is not a server.  This diamond also forces `CoroSocketAdapter` to
  forward server methods through a stream interface.

### Design

Introduce `IBlockingServerSocket` — a server-role interface that accepts
blocking streams:

```cpp
// transport/socket/IBlockingServerSocket.hpp
struct IBlockingServerSocket : public virtual ISocketLifecycle {
    virtual bool start_listening(const std::string& host, int port, int backlog) = 0;

    virtual std::shared_ptr<IBlockingStream> accept_blocking(
        std::error_code& error,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) = 0;
};
```

Interface hierarchy becomes:

```
ISocketLifecycle
├── IClientSocket : virtual ISocketLifecycle
│   ├── IBlockingStream  (+blocking read/write)
│   └── IAsyncStream     (+try_read/write, check_alive)  ← NO LONGER inherits IServerSocket
├── IServerSocket : virtual ISocketLifecycle              ← async accept (existing)
└── IBlockingServerSocket : virtual ISocketLifecycle      ← blocking accept → IBlockingStream (NEW)
```

`ZeroTierSocket` implements all four leaf interfaces.  Code that only needs
blocking server behaviour (like `RendezvousServer`) depends on
`IBlockingServerSocket` and never sees `ZeroTierSocket`.

### Steps

#### 6a — Add `IBlockingServerSocket` (required)

1. Create `transport/socket/IBlockingServerSocket.hpp`
2. `ZeroTierSocket` inherits `IBlockingServerSocket`, implements `accept_blocking()`
   (creates child socket from accepted fd in blocking mode)
3. Add `SocketFactory::create_blocking_server(logger)` → `shared_ptr<IBlockingServerSocket>`
4. Update `RendezvousServer`:
   - Member type: `shared_ptr<IBlockingServerSocket>` instead of `shared_ptr<ZeroTierSocket>`
   - `accept_blocking()` returns `shared_ptr<IBlockingStream>` — no more `dynamic_pointer_cast`
   - Remove `#include "ZeroTierSocket.hpp"` from `RendezvousServer.cpp`
   - Use `SocketFactory::create_blocking_server()` in `start()`
5. Update `RendezvousClient` the same way — use `SocketFactory::create_blocking_client()`
   instead of `ZeroTierSocket::create_blocking()` directly (if not already)

#### 6b — Decouple `IAsyncStream` from `IServerSocket` (optional, larger refactor)

1. Remove `IServerSocket` from `IAsyncStream`'s inheritance list
2. `CoroSocketAdapter` stores two pointers in server mode:
   - `shared_ptr<IAsyncStream>` for I/O on connected streams
   - `shared_ptr<IServerSocket>` for listen/accept (populated by `create_server()`)
3. `AsyncTransportServer` continues using `CoroSocketAdapter` unchanged
4. `IAsyncStream::try_accept()` and `start_listening()` move out of the stream
   interface
5. Smoke-test dispatcher and worker (blocking + async paths)

**Files:** `transport/socket/IBlockingServerSocket.hpp` (new),
`transport/socket/IAsyncStream.hpp`, `transport/socket/zerotier/ZeroTierSocket.hpp/.cpp`,
`transport/socket/SocketFactory.hpp/.cpp`, `transport/coro/CoroSocketAdapter.hpp`,
`services/rendezvous/RendezvousServer.hpp/.cpp`

---

## Verification

1. **Unit:** `RendezvousClient` + `RendezvousServer` protocol round-trip (in-process)
2. **Integration:** `tm-rendezvous` → generator (registers) → worker (discovers) → task flow
3. **Fallback:** rendezvous disabled → worker uses config host/port
4. **Dashboard:** browser to rendezvous VM real IP:9090 → live data
5. **Lifecycle:** stop generator → stale indicator → restart → updates
6. **TTL:** stop generator, 30 s, `/discover` returns `stale: true`
