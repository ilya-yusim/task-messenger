# Skills Library

Shared skill definitions and handlers used by both manager and worker.

## Directory Structure

```
skills/
├── schemas/         # FlatBuffers schema definitions
│   └── skill_task.fbs
├── registry/        # Skill registration, metadata, and dispatch
│   ├── SkillRegistry.hpp    # Central registry with dispatch
│   ├── SkillDescriptor.hpp  # Skill metadata + handler
│   └── SkillIds.hpp         # Compile-time skill ID constants
├── handlers/        # Skill handler implementations (header-only)
│   ├── ISkillHandler.hpp
│   ├── StringReversalHandler.hpp
│   ├── MathOperationHandler.hpp
│   ├── VectorMathHandler.hpp
│   └── FusedMultiplyAddHandler.hpp
└── SkillPayloadFactory.hpp  # Manager-side payload creation
```

## Components

### Schemas (`schemas/`)
FlatBuffers schema definitions for skill request/response messages. The schema
defines the serialization format for all skill payloads.

### Registry (`registry/`)
- **SkillDescriptor**: Complete skill definition (metadata + handler)
- **SkillRegistry**: Central registry for skills with registration, lookup, and dispatch
- **SkillIds**: Compile-time constants for skill identifiers

### Handlers (`handlers/`)
- **ISkillHandler**: Interface for skill handler implementations
- Handler classes implement actual skill logic (string reversal, math ops, etc.)

## Usage

### Manager Side
```cpp
#include "skills/registry/SkillRegistry.hpp"
#include "skills/registry/SkillIds.hpp"

using namespace TaskMessenger::Skills;

// Check skill validity
if (SkillRegistry::instance().has_skill(SkillIds::StringReversal)) {
    // Create task with this skill
}
```

### Worker Side
```cpp
#include "skills/registry/SkillRegistry.hpp"

using namespace TaskMessenger::Skills;

// Create registry instance with logger
SkillRegistry registry(logger);

// Dispatch payload to appropriate handler
std::vector<uint8_t> response;
registry.dispatch(skill_id, task_id, payload, response);
```
