# Skills Library

Shared skill definitions and handlers used by both manager and worker.

## Directory Structure

```
skills/
├── builtins/        # Built-in skill implementations (FlatBuffers schemas + C++ sources)
│   ├── Common.fbs
│   ├── StringReversalSkill.{fbs,hpp,cpp}
│   ├── MathOperationSkill.{fbs,hpp,cpp}
│   ├── VectorMathSkill.{fbs,hpp,cpp}
│   └── FusedMultiplyAddSkill.{fbs,hpp,cpp}
├── handlers/        # Skill handler interface
│   └── ISkillHandler.hpp
├── registry/        # Skill registration, metadata, and dispatch
│   ├── SkillRegistry.hpp    # Central registry with dispatch
│   ├── SkillDescriptor.hpp  # Skill metadata + handler
│   ├── SkillIds.hpp         # Compile-time skill ID constants
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
- **SkillDescriptor**: Binds a skill's metadata, handler, and payload factory.
- **SkillIds**: Compile-time constants for skill identifiers.
- **PayloadBuffer**: Type-erased owned buffer with typed pointer access for zero-copy transfer.

### Handlers (`handlers/`)
- **ISkillHandler**: Interface for skill handler implementations (worker side).

## Usage

### Manager Side
```cpp
#include "skills/registry/SkillRegistry.hpp"
#include "skills/registry/SkillIds.hpp"

using namespace TaskMessenger::Skills;

// Check skill validity
if (SkillRegistry::instance().has_skill(SkillIds::StringReversal)) {
    // Create typed request + pre-allocated response buffers
    auto request = SkillRegistry::instance().create_test_request_buffer(SkillIds::StringReversal);
    auto response = SkillRegistry::instance().create_response_buffer(
        SkillIds::StringReversal, request->span());
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
