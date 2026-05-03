# Plan: IGenerator Interface Pattern

## TL;DR

Introduce an **optional** `IGenerator` abstract interface that documents the generator contract without changing the fundamental architecture: **the generator remains the executable, the dispatcher remains a library**. Power users can implement `IGenerator` for structure and testability, or ignore it and write raw `main()` as before. A thin `run_generator()` helper eliminates the repeated boilerplate across all generator mains.

---

## Motivation

Every generator executable repeats the same boilerplate:
1. Create `DispatcherApp`, call `start(argc, argv)`
2. Configure skill verification from `GeneratorOptions`
3. Instantiate generator logic, run its loop
4. Call `app.stop()`, return exit code

The `IGenerator` interface codifies this pattern so that:
- New generator authors have a **documented entry point** instead of copying an existing main
- The generator algorithm is **unit-testable** in isolation (pass a mock `DispatcherApp`)
- Common setup (logger, verification config, registry logging) happens once in a shared launcher

This is a **convention**, not an enforcement mechanism. The dispatcher library does not require it.

---

## Design Principles

1. **Generator owns its control flow.** The `run()` method is the generator's main loop — it decides when to submit tasks, how to poll, when to exit. The dispatcher never calls back into the generator unprompted.

2. **No framework inversion.** `IGenerator` is not a plugin loaded by the dispatcher. It's an interface the generator executable instantiates and drives.

3. **Backward compatible.** Existing generators (interactive, auto-refill) can adopt `IGenerator` or remain as-is. Both patterns coexist.

4. **Minimal surface.** Three methods: `initialize()`, `run()`, `on_shutdown()`. No more.

---

## Interface Definition

Create `generators/common/IGenerator.hpp`:

```cpp
/**
 * \file IGenerator.hpp
 * \brief Optional interface for generator algorithms.
 *
 * Provides a documented contract for generator implementations.
 * Power users implement this interface; the common TaskGenerator
 * is a convenience base class that implements it.
 *
 * This is a convention, not a requirement. Generators can still
 * write raw main() and use DispatcherApp directly.
 */
#pragma once

class DispatcherApp;

/**
 * \brief Optional contract for generator algorithms.
 *
 * Implementors define their initialization, main loop, and shutdown behavior.
 * The generator executable creates a DispatcherApp, constructs an IGenerator,
 * and drives the lifecycle:
 *
 * \code
 * DispatcherApp app;
 * if (int rc = app.start(argc, argv); rc != 0) return (rc == 1) ? 0 : rc;
 * MyGenerator gen;
 * if (!gen.initialize(app)) return 1;
 * int result = gen.run(app);
 * app.stop();
 * return result;
 * \endcode
 */
class IGenerator {
public:
    virtual ~IGenerator() = default;

    /**
     * \brief One-time setup after the dispatcher is started.
     * \param app The running dispatcher (transport is live, logger is available).
     * \return true if initialization succeeded, false to abort.
     *
     * Use this to configure generator-specific state, validate preconditions,
     * or log startup information. The dispatcher is fully operational when
     * this is called.
     */
    virtual bool initialize(DispatcherApp& app) = 0;

    /**
     * \brief The generator's main loop.
     * \param app The running dispatcher for submitting tasks.
     * \return Process exit code (0 = success).
     *
     * This method owns the control flow. It decides when to submit tasks,
     * how to wait for results, and when to exit. The method should return
     * when work is complete or when app.shutdown_requested() becomes true.
     */
    virtual int run(DispatcherApp& app) = 0;

    /**
     * \brief Called when a shutdown signal is received.
     *
     * Must be thread-safe (called from signal handler context via
     * DispatcherApp::request_shutdown). Use this to set internal flags
     * that cause run() to exit its loop.
     *
     * Default implementation does nothing (generators can check
     * app.shutdown_requested() in their run loop instead).
     */
    virtual void on_shutdown() {}
};
```

---

## Launcher Helper

Create `generators/common/run_generator.hpp`:

```cpp
/**
 * \file run_generator.hpp
 * \brief Thin launcher that eliminates boilerplate across generator mains.
 */
#pragma once

#include "IGenerator.hpp"
#include "GeneratorOptions.hpp"
#include "dispatcher/DispatcherApp.hpp"
#include "skills/registry/SkillRegistry.hpp"
#include "skills/registry/CompareUtils.hpp"

/**
 * \brief Launch a generator with standard dispatcher lifecycle.
 *
 * Handles: DispatcherApp boot, skill registry logging, verification config,
 * generator initialize/run/shutdown, and clean teardown.
 *
 * \param argc From main()
 * \param argv From main()
 * \param generator The generator algorithm to run
 * \return Process exit code
 *
 * Usage:
 * \code
 * int main(int argc, char* argv[]) {
 *     MyGenerator gen;
 *     return run_generator(argc, argv, gen);
 * }
 * \endcode
 */
inline int run_generator(int argc, char* argv[], IGenerator& generator) {
    DispatcherApp app;
    int rc = app.start(argc, argv);
    if (rc != 0) {
        return (rc == 1) ? 0 : rc;
    }

    auto logger = app.logger();

    // Log registered skills
    auto& registry = TaskMessenger::Skills::SkillRegistry::instance();
    logger->info("Registered skills: " + std::to_string(registry.skill_count()));

    // Configure verification from generator options
    auto& cfg = TaskMessenger::Skills::CompareConfig::defaults();
    cfg.enabled = generator_opts::get_verify_enabled();
    cfg.abs_epsilon = generator_opts::get_verify_epsilon();
    cfg.rel_epsilon = generator_opts::get_verify_rel_epsilon();
    cfg.inject_failure = generator_opts::get_verify_inject_failure();

    if (!generator.initialize(app)) {
        logger->error("Generator initialization failed");
        app.stop();
        return 1;
    }

    int result = generator.run(app);

    app.stop();
    return result;
}
```

---

## Refactor Existing Generators

### Interactive Generator

Create `generators/interactive/InteractiveGenerator.hpp`:

```cpp
#pragma once

#include "generators/common/IGenerator.hpp"
#include "generators/common/TaskGenerator.hpp"

class InteractiveGenerator : public IGenerator {
public:
    bool initialize(DispatcherApp& app) override;
    int run(DispatcherApp& app) override;
    void on_shutdown() override;

private:
    TaskGenerator task_gen_;
};
```

Refactor `interactiveMain.cpp` to:

```cpp
#include "InteractiveGenerator.hpp"
#include "generators/common/run_generator.hpp"

int main(int argc, char* argv[]) {
    InteractiveGenerator gen;
    return run_generator(argc, argv, gen);
}
```

`InteractiveGenerator::run()` contains the existing interactive loop logic currently in `interactiveMain.cpp` (the `while (!app.shutdown_requested())` stdin loop).

### Auto-Refill Generator

Same pattern — extract loop into `AutoRefillGenerator::run()`, main becomes a three-liner.

---

## Decouple Skill Iteration from Compile-Time Constants

Currently `TaskGenerator::dispatch_parallel()` hard-codes skill iteration:

```cpp
uint32_t skill_id = (i % SkillIds::Count) + 1;
```

Replace with runtime query:

```cpp
// In TaskGenerator constructor or initialize():
available_skills_ = SkillRegistry::instance().skill_ids();

// In dispatch_parallel():
uint32_t skill_id = available_skills_[i % available_skills_.size()];
```

This makes `TaskGenerator` compatible with dynamically-registered skills (WASM modules, shared library plugins) that won't have compile-time `SkillIds` entries.

---

## Power User Example

A power user implementing a Monte Carlo simulation generator:

```cpp
// monte_carlo_gen.cpp
#include "generators/common/IGenerator.hpp"
#include "generators/common/run_generator.hpp"
#include "dispatcher/DispatcherApp.hpp"
#include "message/GeneratorCoroutine.hpp"
#include "skills/registry/SkillRegistry.hpp"

class MonteCarloGenerator : public IGenerator {
    static constexpr uint32_t kMonteCarloSkillId = 100; // user-defined skill

    bool initialize(DispatcherApp& app) override {
        auto& registry = TaskMessenger::Skills::SkillRegistry::instance();
        if (!registry.has_skill(kMonteCarloSkillId)) {
            app.logger()->error("Monte Carlo skill not registered");
            return false;
        }
        return true;
    }

    int run(DispatcherApp& app) override {
        constexpr uint32_t NUM_SAMPLES = 10000;
        constexpr uint32_t BATCH_SIZE = 100;
        std::vector<GeneratorCoroutine> pending;

        for (uint32_t batch = 0; batch < NUM_SAMPLES / BATCH_SIZE; ++batch) {
            if (app.shutdown_requested()) break;

            // Submit batch of Monte Carlo sample tasks
            for (uint32_t i = 0; i < BATCH_SIZE; ++i) {
                uint32_t task_id = batch * BATCH_SIZE + i;
                auto request = create_sample_request(batch, i);
                auto response = create_response_buffer();
                pending.emplace_back(submit_and_collect(app, task_id, 
                    std::move(request), std::move(response)));
            }

            // Wait for batch completion before submitting next
            while (!all_done(pending) && !app.shutdown_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            pending.clear();
        }

        aggregate_results();
        return 0;
    }

    void on_shutdown() override {
        // Nothing extra — run() checks app.shutdown_requested()
    }

    // ... Monte Carlo-specific methods ...
};

int main(int argc, char* argv[]) {
    MonteCarloGenerator gen;
    return run_generator(argc, argv, gen);
}
```

The power user:
- Writes one class with `initialize()` / `run()` / `on_shutdown()`
- Gets dispatcher lifecycle, logging, options parsing, and skill registry for free
- Owns their control flow entirely within `run()`
- Links `libtm-dispatcher` + `skills_dep` + their own skill library

---

## Build Integration

No changes to `generators/meson.build` or `generators/common/` build — `IGenerator.hpp` and `run_generator.hpp` are header-only. Existing generators adopt at will.

New files:
- `generators/common/IGenerator.hpp`
- `generators/common/run_generator.hpp`
- `generators/interactive/InteractiveGenerator.hpp`
- `generators/interactive/InteractiveGenerator.cpp`
- `generators/auto-refill/AutoRefillGenerator.hpp`
- `generators/auto-refill/AutoRefillGenerator.cpp`

Modified files:
- `generators/interactive/interactiveMain.cpp` — reduce to three-line launcher
- `generators/auto-refill/autoRefillMain.cpp` — reduce to three-line launcher
- `generators/common/TaskGenerator.hpp` — add `available_skills_` member, remove `SkillIds::Count` dependency
- `generators/common/TaskGenerator.cpp` — query `SkillRegistry::skill_ids()` at runtime

---

## Steps

### Phase 1: Interface and Launcher

1. Create `generators/common/IGenerator.hpp` — the abstract interface
2. Create `generators/common/run_generator.hpp` — the inline launcher helper

### Phase 2: Decouple Skill Iteration

3. Update `TaskGenerator` to query skill IDs from the registry at runtime instead of using compile-time `SkillIds::Count`

### Phase 3: Refactor Interactive Generator

4. Create `InteractiveGenerator` class implementing `IGenerator`
5. Move interactive loop logic from `interactiveMain.cpp` into `InteractiveGenerator::run()`
6. Reduce `interactiveMain.cpp` to the three-line launcher pattern

### Phase 4: Refactor Auto-Refill Generator

7. Create `AutoRefillGenerator` class implementing `IGenerator`
8. Move auto-refill loop logic from `autoRefillMain.cpp` into `AutoRefillGenerator::run()`
9. Reduce `autoRefillMain.cpp` to the three-line launcher pattern

### Phase 5: Verify

10. Build all generator targets — ensure no regressions
11. Test interactive generator — identical behavior
12. Test auto-refill generator — identical behavior
13. Verify skill registration still works (`link_whole` unchanged)

---

## Key Decisions

| Decision | Choice | Rationale |
|---|---|---|
| IGenerator required or optional? | **Optional** | Keeps the "raw main()" path for simplest use cases |
| run_generator() inline or compiled? | **Inline** (header-only) | No new library target, no build changes |
| on_shutdown() pure virtual or defaulted? | **Defaulted** (empty) | Most generators just check `app.shutdown_requested()` |
| TaskGenerator inherits IGenerator? | **No** | TaskGenerator is a utility, not a generator. Generators *use* TaskGenerator |
| Dispatcher-as-plugin alternative? | **Rejected** | Generator must own its control flow; inverting lifecycle adds complexity for no deployment benefit (1:1 same-server topology) |
