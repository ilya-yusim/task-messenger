# Skills Library

Shared skill definitions and handlers used by both dispatcher and worker.

## Directory Structure

```
skills/
├── builtins/        # Built-in skill implementations (FlatBuffers schemas + C++ sources)
│   ├── Common.fbs
│   ├── StringReversalSkill.{fbs,hpp,cpp}
│   ├── MathOperationSkill.{fbs,hpp,cpp}
│   ├── VectorMathSkill.{fbs,hpp,cpp}
│   └── FusedMultiplyAddSkill.{fbs,hpp,cpp}
├── registry/        # Skill registration, metadata, and dispatch
│   ├── SkillRegistry.hpp    # Central registry with dispatch
│   ├── ISkill.hpp           # Complete skill interface (processing + factory + identity)
│   ├── Skill.hpp            # CRTP base class for skill implementations
│   ├── SkillKey.hpp         # Deterministic skill ID derivation from names
│   ├── PayloadBuffer.hpp    # Type-erased owned payload buffers
│   ├── IPayloadFactory.hpp  # Factory interface for payload creation
│   ├── CompareUtils.hpp/.cpp# Floating-point comparison helpers
│   └── VerificationResult.hpp
└── schemas/         # (reserved for cross-skill shared schemas)
```

## Components

### Built-ins (`builtins/`)
Each built-in skill bundles its FlatBuffers `.fbs` schema, a C++ implementation
(`.cpp`), and a header (`.hpp`) containing the skill's `IPayloadFactory`
subclass. Schemas are compiled at build time using `flatc`.

### Registry (`registry/`)
- **SkillRegistry**: Central registry for skills with registration, lookup, and dispatch.
  Can be used as a singleton via `instance()` or instantiated directly.
- **ISkill / Skill\<Derived\>**: Combined interface and CRTP base class providing identity
  (name, description, version, ID), handler, and payload factory in a single object.
- **SkillKey**: Derives deterministic 32-bit skill IDs from namespaced string names via FNV-1a hash.
- **PayloadBuffer**: Type-erased owned buffer with typed pointer access for zero-copy transfer.

### Handlers
Task processing is defined by the `ISkill::process()` virtual method.
The `Skill<Derived>` CRTP base implements it by delegating to
`scatter_request()` / `scatter_response()` / `compute()` on the derived class.

## Usage

### Dispatcher Side
```cpp
#include "skills/registry/SkillRegistry.hpp"
#include "skills/registry/SkillKey.hpp"

using namespace TaskMessenger::Skills;

// Check skill validity by name
if (SkillRegistry::instance().has_skill("builtin.StringReversal")) {
    auto id = SkillKey::from_name("builtin.StringReversal");
    // Create typed request + pre-allocated response buffers
    auto request = SkillRegistry::instance().create_test_request_buffer(id);
    auto response = SkillRegistry::instance().create_response_buffer(
        id, request->span());
    // Wrap in a TaskMessage and submit
}
```

### Worker Side
```cpp
#include "skills/registry/SkillRegistry.hpp"

using namespace TaskMessenger::Skills;

// Use the global singleton (or construct with a logger)
SkillRegistry& registry = SkillRegistry::instance();

// Dispatch request payload to the appropriate handler.
// 'response' must be a pre-allocated writable span sized for the expected output.
bool ok = registry.dispatch(skill_id, task_id, request_span, response_span);
```
