# Session Management Module

The `dispatcher/session/` module orchestrates every dispatcher<->worker conversation. Each accepted socket gets a `Session` coroutine that pulls work from the shared `TaskMessageQueue`, pushes it across the transport layer, and records latency metrics that feed operator statistics.

## Responsibilities
- Translate `TaskMessageQueue` work into serialized wire traffic via `transport::CoroSocketAdapter`.
- Track per-session lifecycle state, throughput, and round-trip timing.
- Coordinate task fan-out and cleanup through `SessionManager`, which owns the shared pool and collection of active sessions.

All public APIs are tagged with `\ingroup session_module` so they appear under the *Session Management Module* section in the generated Doxygen docs.

## Lifecycle Overview (Mermaid)
```mermaid
graph TD
    Accept[Transport acceptor] -->|hand client socket| SessionManager
    SessionManager -->|create_session| Session
    Session -->|co_await get_next_task| Pool[TaskMessageQueue]
    Pool --> Session
    Session -->|async_write/async_read| Transport
    Transport --> Worker
    Session -->|stats + state updates| SessionManager
    SessionManager --> Logger
```

## Cleanup Flow (Mermaid)
```mermaid
sequenceDiagram
    participant SM as SessionManager
    participant Sess as Session
    participant Pool as TaskMessageQueue
    participant Logger

    Sess->>Pool: co_await task
    Pool-->>Sess: TaskMessage
    Sess->>Sess: process task (async write/read)
    Sess-->>SM: state = TERMINATED / ERROR
    SM->>SM: cleanup_completed_sessions()
    SM->>Logger: log completion + stats
    SM->>Pool: (optional) requeue tasks on failure
```

## Authoring Tips
- `Session` encapsulates the coroutine (`Task<void>`) that owns each client lifecycle. Keep shared data (`TaskMessageQueue`, logger) as `std::shared_ptr` to extend lifespan beyond the accept thread.
- `SessionManager` is the public façade. Extend it with diagnostic helpers or throttling logic instead of letting outside callers touch internal session containers directly.
- when adding new state transitions, document them via `SessionState` comments and, if they affect cleanup, update the diagram and `cleanup_completed_sessions()` docs.
- Run `meson compile -C builddir-dispatcher docs` to regenerate HTML docs; Doxygen now scans this directory thanks to the `session_module` annotations.
