# Serialization Test Directory

This directory contains standalone test cases for evaluating serialization libraries for skill-based task messaging.

## Concept: Skills

A "skill" is a typed task category with:
- A **request** data structure (manager → worker)
- A **response** data structure (worker → manager)
- A unique `skill_id` for routing and dispatch

The serialization envelope (`SkillRequest`/`SkillResponse`) wraps skill-specific payloads, allowing the transport layer to route messages without knowing the inner payload structure.

## Directory Structure

```
serialize/
├── meson.build           # Top-level build file
├── README.md             # This file
└── flatbuffers/
    ├── meson.build       # FlatBuffers test build
    ├── schemas/
    │   └── skill_task.fbs    # FlatBuffers schema
    └── test_flatbuffers.cpp  # Test demonstrating usage
```

## Building Tests

```bash
# Configure with serialization tests enabled
meson setup builddir -Dbuild_serialize_tests=true

# Build
meson compile -C builddir

# Run tests
meson test -C builddir flatbuffers_skill_test

# Or run the executable directly
./builddir/serialize/flatbuffers/test_flatbuffers
```

## FlatBuffers Overview

[FlatBuffers](https://google.github.io/flatbuffers/) is a cross-platform serialization library from Google.

### Key Features
- **Zero-copy access**: Read data directly from the buffer without parsing/unpacking
- **Schema evolution**: Add fields without breaking backward compatibility
- **Small code footprint**: Header-only runtime library
- **Type safety**: Generated code provides type-safe accessors

### Schema Location
- [`flatbuffers/schemas/skill_task.fbs`](flatbuffers/schemas/skill_task.fbs)

### Example Skills Defined

1. **StringReversalSkill** (skill_id=1)
   - Request: `{ input: string }`
   - Response: `{ output: string, original_length: uint32 }`

2. **MathOperationSkill** (skill_id=2)
   - Request: `{ operand_a: double, operand_b: double, operation: enum }`
   - Response: `{ result: double, overflow: bool }`

## Integration with Task Messenger

After evaluation, the chosen library will be integrated with:
- [`TaskMessage`](../message/TaskMessage.hpp) - payload serialization
- [`TaskGenerator`](../manager/TaskGenerator.hpp) - creating skill-typed tasks
- Worker processor - deserializing and executing skill requests

## Adding New Skills

1. Define request/response tables in the `.fbs` schema
2. Assign a unique `skill_id`
3. Update the worker's skill dispatcher to handle the new skill
4. Regenerate the `_generated.h` file (done automatically by meson)
