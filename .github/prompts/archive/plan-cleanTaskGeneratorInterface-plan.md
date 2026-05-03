# Plan: Clean TaskGenerator / Manager Interface with Library Extraction

## TL;DR

Refactor task-messenger into a set of publishable libraries (`libtm-transport`, `libtm-manager`) so that:
- The **worker** and **manager** share a common transport library instead of duplicating sources.
- The **manager infrastructure** (transport server, sessions, message pool, response context) is a library that external generators link against.
- **Generators are standalone executables** at top-level `generators/` — each owns `main()`, submits tasks via `submit_task()`, and links the libraries.
- The project can be published as a library without generators; users write their own and link in.

---

## Current State

- `managerMain.cpp` owns `main()`: sets up logging, options, transport server, creates `DefaultTaskGenerator`, runs interactive/auto-refill loop — all monolithically compiled.
- All sources (transport, session, message, generator) compiled directly into `tm-manager` — no library boundary.
- Worker duplicates the same 6 transport source files (`coroIoContext`, `coroSocketAdapter`, `SocketFactory`, `SocketTypeOptions`, `ZeroTierSocket`, `ZeroTierNodeService`).
- `DefaultTaskGenerator` uses `TaskSubmitAwaitable` + `TaskMessagePool` + `ResponseContext` — these become the public generator API.

---

## Steps

### Phase 1: Extract `libtm-transport` (shared transport library)

1. **Create `transport/meson.build`** — Define `tm_transport_lib` static library from the 6 sources currently duplicated:
   - `transport/coro/coroIoContext.cpp`
   - `transport/coro/coroSocketAdapter.cpp`
   - `transport/socket/SocketFactory.cpp`
   - `transport/socket/SocketTypeOptions.cpp`
   - `transport/socket/zerotier/ZeroTierSocket.cpp`
   - `transport/socket/zerotier/ZeroTierNodeService.cpp`
   - Dependencies: `shared_dep`, `libzt_dep`, `cli11_dep`, `json_dep`
   - Export as `tm_transport_dep` (declared_dependency with include dirs)

2. **Update `worker/meson.build`** — Remove the 6 duplicated `../transport/...` source files, link `tm_transport_dep` instead. (*parallel with step 3*)

3. **Update root `meson.build`** — Add `subdir('transport')` before `manager/` and `worker/` subdirectories. Export `tm_transport_dep` for downstream use. (*depends on step 1*)

### Phase 2: Extract `libtm-manager` (manager infrastructure library)

4. **Create `manager/meson.build` library target** — Define `tm_manager_lib` static library containing:
   - `manager/transport/AsyncTransportServer.cpp`, `manager/transport/AsyncTransportOptions.cpp`
   - `manager/session/Session.cpp`, `manager/session/SessionManager.cpp`
   - `../message/TaskMessagePool.cpp`
   - Generic `ManagerOptions.cpp` (stripped of verify flags — see step 5)
   - Dependencies: `tm_transport_dep`, `shared_dep`, `libzt_dep`, `cli11_dep`, `json_dep`
   - Export as `tm_manager_dep` (declared_dependency with include dirs)
   - Note: does **not** link `skills_dep` — generators link it directly

5. **Split `ManagerOptions`** — Remove verify-specific options (`--verify`, `--verify-epsilon`, `--verify-rel-epsilon`, `--verify-inject-failure`) from the library. Only transport server options (`listen_host`, `listen_port`, `io_threads`) remain in the library via `AsyncTransportOptions`. (*parallel with step 4*)

6. **Create `ManagerApp` harness class** (in the library) — encapsulates common startup:
   - Logger setup, `Options::load_and_parse()`, `AsyncTransportServer::start()`, signal handler installation
   - Accessors: `task_pool()`, `response_context()`, `server()`, `logger()`
   - `start()` / `stop()` lifecycle only — generators own their entire run loop
   - No `ITaskGenerator` interface — generators call `submit_task()` directly

7. **Define the public API surface** — the generator-facing contract:
   - `ManagerApp` — `start()`, `stop()`, `task_pool()`, `response_context()`, `logger()`
   - `AsyncTransportServer` — `get_task_pool_stats()`, `print_transporter_statistics()` (accessible via `ManagerApp::server()`)
   - `submit_task()` (header-only, from `message/TaskSubmitAwaitable.hpp`)
   - `GeneratorCoroutine` (header-only)
   - `TaskCompletionSource` (header-only)
   - `TaskMessagePool`, `ResponseContext` — obtained as shared_ptrs from `ManagerApp`

### Phase 3: Create top-level `generators/` directory

8. **Create `generators/interactive/`** — Interactive generator executable:
   - `interactiveMain.cpp` — owns `main()`, creates `ManagerApp`, prompts user for task count, dispatches via `submit_task()`, waits for completion, prints stats
   - `TaskGenerator.cpp/.hpp` — adapted from current `DefaultTaskGenerator` (task data generation, `dispatch_parallel`, `process_single_task`)
   - Registers its own CLI options: `--verify`, `--verify-epsilon`, etc.
   - Meson target `tm-generator-interactive` links: `tm_manager_dep`, `skills_dep` (link_whole), `shared_dep`

9. **Create `generators/auto-refill/`** — Auto-refill generator executable: (*parallel with step 8*)
   - `autoRefillMain.cpp` — owns `main()`, creates `ManagerApp`, runs monitoring loop that auto-dispatches when pool < threshold
   - Reuses `TaskGenerator` code (shared via a common generator utility, or duplicated if trivial)
   - Meson target `tm-generator-auto-refill` links: `tm_manager_dep`, `skills_dep` (link_whole), `shared_dep`

10. **Create `generators/meson.build`** — Builds both generator targets. Each links `skills_dep` directly with `link_whole` to ensure static skill registration works.

11. **Update root `meson.build`** — Add `subdir('generators')` with a build option (`build_generators`, default true) to control whether bundled generators are built. (*depends on steps 8-10*)

12. **Remove old `tm-manager` executable target** from `manager/meson.build` — the library replaces it. Old `managerMain.cpp` and `TaskGenerator.cpp` are superseded by the generators. (*depends on step 8*)

### Phase 4: Clean up shared generator code

13. **Extract common generator utilities** — Shared code (`TaskIdGenerator`, `generate_task_data_typed()`, `dispatch_parallel()`) goes into `generators/common/` as compiled sources. Both generators compile these directly (no extra library needed).

14. **Delete `ITaskGenerator` interface** — Remove from `TaskGenerator.hpp`. Generators don't need polymorphism; they directly use `submit_task()`.

### Phase 5: Verify

15. **Build all targets** — `libtm-transport`, `libtm-manager`, `tm-generator-interactive`, `tm-generator-auto-refill`, `tm-worker`
16. **Test `tm-generator-interactive`** — identical behavior to old `tm-manager --interactive` (task dispatch, worker roundtrip, stats)
17. **Test `tm-generator-auto-refill`** — identical behavior to old `tm-manager` auto-refill mode
18. **Test `tm-worker`** — builds and runs unchanged, now linking `tm_transport_dep`
19. **Test `skills_dep` link_whole** — verify static skill registration works in all executables
20. **Create a minimal "hello world" generator** — validates that an external consumer can write a generator using only the public API (`ManagerApp` + `submit_task()`)

---

## Revised Directory Structure

```
task-messenger/
  meson.build                  # Root: builds transport, skills, manager lib, worker, generators
  transport/
    meson.build                # NEW: libtm-transport static library
    coro/                      # (unchanged sources)
    socket/                    # (unchanged sources)
  message/                     # (unchanged, header-only + TaskMessagePool.cpp)
  skills/                      # (unchanged, libskills with link_whole)
  manager/
    meson.build                # CHANGED: builds libtm-manager library only (no executable)
    ManagerApp.hpp/cpp         # NEW: startup harness
    transport/                 # AsyncTransportServer, AsyncTransportOptions
    session/                   # Session, SessionManager
  worker/
    meson.build                # CHANGED: links tm_transport_dep instead of raw sources
    ...                        # (otherwise unchanged)
  generators/
    meson.build                # NEW: builds generator executables
    common/                    # NEW: shared generator utilities
      TaskGenerator.hpp/cpp    # Adapted from DefaultTaskGenerator
      TaskIdGenerator.hpp      # Extracted from TaskGenerator.hpp
    interactive/
      meson.build
      interactiveMain.cpp      # NEW: owns main(), interactive loop
    auto-refill/
      meson.build
      autoRefillMain.cpp       # NEW: owns main(), monitoring loop
```

---

## Relevant Files

**Modified:**
- `meson.build` — add `subdir('transport')`, `subdir('generators')`; build order changes
- `manager/meson.build` — convert from executable to library target
- `worker/meson.build` — replace raw transport sources with `tm_transport_dep`
- `manager/ManagerOptions.hpp/.cpp` — strip verify flags (move to generators)

**New:**
- `transport/meson.build` — `libtm-transport` library definition
- `manager/ManagerApp.hpp/.cpp` — startup harness class
- `generators/meson.build` — top-level generator build
- `generators/interactive/interactiveMain.cpp` — interactive generator `main()`
- `generators/auto-refill/autoRefillMain.cpp` — auto-refill generator `main()`
- `generators/common/TaskGenerator.hpp/.cpp` — shared task generation logic

**Deleted:**
- `manager/managerMain.cpp` — replaced by generator mains
- `manager/TaskGenerator.hpp/.cpp` — moved to `generators/common/`

**Unchanged:**
- `message/TaskSubmitAwaitable.hpp` — key generator API (header-only)
- `message/GeneratorCoroutine.hpp`, `TaskCompletionSource.hpp`, `ResponseContext.hpp`
- `skills/` — `libskills` with `link_whole`, linked directly by generators and worker
- Worker source files (other than meson.build linkage change)

---

## Verification

1. `tm-generator-interactive` builds and behaves identically to old `tm-manager --interactive`
2. `tm-generator-auto-refill` builds and behaves identically to old `tm-manager` auto-refill mode
3. `tm-worker` builds and runs unchanged (now linking `tm_transport_dep`)
4. `skills_dep` `link_whole` works correctly when linked directly to generators (not transitively through `tm_manager_dep`)
5. Generator code only includes public headers — not session/transport internals
6. A minimal "hello world" generator builds using only `tm_manager_dep` + `skills_dep`

---

## Decisions

1. **Three-library split**: `libtm-transport` (shared by manager+worker), `libtm-manager` (manager infrastructure), `libskills` (linked separately with `link_whole`)
2. **Static libraries** — matches existing `skills_lib` pattern, no symbol visibility issues
3. **Generators at top-level `generators/`** — project publishable as library without generators; users write their own
4. **No `ITaskGenerator` interface** — generators call `submit_task()` directly, no polymorphic abstraction needed
5. **`ManagerApp` harness provides lifecycle only** — `start()`/`stop()`/accessors; generators own their run loop entirely
6. **`libskills` linked separately** to generators and worker (not transitively through manager lib) — avoids `link_whole` propagation issues
7. **Interactive and auto-refill are separate generator executables** — clean separation of concerns, each owns its `main()`
8. **Verify flags move to generators** — library has no opinion on verification; that's generator-specific logic

---

## Resolved Considerations

1. **Shared generator code granularity** — `TaskIdGenerator`, `generate_task_data_typed()`, `dispatch_parallel()` go into `generators/common/` as compiled sources.
2. **Config file structure** — Keep one config file; each component reads its own section via the existing CLI11/JSON provider pattern.
3. **Executable naming** — `tm-generator-interactive` and `tm-generator-auto-refill`.
