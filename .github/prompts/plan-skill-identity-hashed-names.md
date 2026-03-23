# Plan: Skill Identity via Namespaced Names with Deterministic Hashing

## TL;DR

Replace the centralized, compile-time `SkillIds.hpp` enum with a **decentralized naming scheme** where each skill declares a namespaced string name (e.g., `"builtin.StringReversal"`, `"alice.MonteCarloSample"`) and the numeric `skill_id` is derived via a deterministic hash function. Independent power users can develop skills without coordinating ID assignments. Collision detection at registration time ensures safety.

---

## Problem

Today, skill identity is a hard-coded enum in `skills/registry/SkillIds.hpp`:

```cpp
namespace SkillIds {
    constexpr uint32_t StringReversal = 1;
    constexpr uint32_t MathOperation = 2;
    constexpr uint32_t VectorMath = 3;
    constexpr uint32_t FusedMultiplyAdd = 4;
}
```

This requires every skill author to modify the same header and manually assign a unique integer. Two independent users who both pick ID `5` cause a silent, catastrophic collision — the worker invokes the wrong handler.

This is the **distributed naming problem**: who is the authority for mapping a skill's identity to a numeric ID when there is no central coordinator?

---

## Solution: Deterministic Hash from Namespaced String Names

Each skill declares a **canonical namespaced name**. The numeric `skill_id` is computed from this name via a deterministic hash (FNV-1a 32-bit). The name is the source of truth; the numeric ID is derived.

**Naming convention:** `<namespace>.<SkillName>`
- `builtin.StringReversal` — project-provided skills
- `alice.MonteCarloSample` — user Alice's skills
- `acme.ImageResize` — organization-level namespace

---

## Precedents

| System | Identity scheme | Relevance |
|---|---|---|
| Java class loading | Fully-qualified class names (`com.alice.MonteCarlo`) | Decentralized naming via namespace convention |
| gRPC service methods | `/package.Service/Method` strings | String-based dispatch, no central ID registry |
| Minecraft Forge | `modid:itemname` namespaced strings | Independent mod authors, no coordination |
| Cap'n Proto type IDs | 64-bit IDs derived from schema file hashes | Deterministic hash from content |
| COM/DCOM GUIDs | 128-bit UUIDs, often derived from names | Collision-free distributed identity |
| Kubernetes CRDs | `group/version/kind` naming | Decentralized extension identity |

---

## Design

### SkillKey: Name-to-ID Hash Function

Create `skills/registry/SkillKey.hpp`:

```cpp
/**
 * @file skills/registry/SkillKey.hpp
 * @brief Deterministic skill ID derivation from namespaced string names.
 *
 * Eliminates the need for a centralized SkillIds enum. Independent skill
 * authors choose a namespaced name and the numeric ID is derived automatically.
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace TaskMessenger::Skills {

namespace SkillKey {

    /**
     * @brief Derive a deterministic 32-bit skill ID from a canonical name.
     *
     * Uses FNV-1a 32-bit hash. The same name always produces the same ID.
     * Namespaced names (e.g., "alice.MonteCarloSample") prevent practical collisions.
     *
     * @param name Fully-qualified skill name (e.g., "builtin.StringReversal").
     * @return Deterministic 32-bit skill ID.
     *
     * Convention: names must contain a dot separator (namespace.SkillName).
     * Builtin skills use the "builtin." prefix.
     */
    constexpr uint32_t from_name(std::string_view name) {
        uint32_t hash = 2166136261u;  // FNV offset basis
        for (char c : name) {
            hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
            hash *= 16777619u;  // FNV prime
        }
        return hash;
    }

    /**
     * @brief Validate that a skill name follows the namespace convention.
     * @param name The skill name to validate.
     * @return true if the name contains at least one dot and non-empty segments.
     */
    constexpr bool is_valid_name(std::string_view name) {
        if (name.empty()) return false;
        auto dot = name.find('.');
        if (dot == std::string_view::npos) return false;
        if (dot == 0 || dot == name.size() - 1) return false;
        return true;
    }

} // namespace SkillKey

} // namespace TaskMessenger::Skills
```

### Backward-Compatible Builtin IDs

Builtin skills transition to hashed names while preserving wire compatibility during the migration:

```cpp
// Option A: Builtins adopt hashed names (clean break)
static constexpr uint32_t kSkillId = SkillKey::from_name("builtin.StringReversal");

// Option B: Builtins keep legacy IDs, new skills use hashed names (gradual migration)
// SkillIds.hpp remains for builtins only; new skills never touch it.
```

**Recommendation:** Option A (clean break) is preferred. Since both generator and worker are recompiled together today, there is no wire-compatibility concern with existing deployments. The transition happens once and eliminates the legacy path.

### SkillDescriptor Changes

`SkillDescriptor` already has a `name` field. The change is making `name` the **primary identity** rather than the `id` field:

```cpp
struct SkillDescriptor {
    std::string name;          // PRIMARY KEY — e.g., "builtin.StringReversal"
    uint32_t id{0};            // DERIVED — computed from name via SkillKey::from_name()
    std::string description;
    uint32_t version{1};
    // ... rest unchanged
};
```

Factory methods compute the ID from the name:

```cpp
static SkillDescriptor create(
    std::unique_ptr<ISkill> skill_impl,
    std::string_view name,      // e.g., "builtin.MathOperation"
    std::string_view description,
    uint32_t version = 1,
    size_t req_size = 256,
    size_t resp_size = 256
) {
    SkillDescriptor desc;
    desc.name = name;
    desc.id = SkillKey::from_name(name);  // derived, not assigned
    desc.description = description;
    desc.version = version;
    desc.skill = std::move(skill_impl);
    desc.typical_request_size = req_size;
    desc.typical_response_size = resp_size;
    return desc;
}
```

### SkillRegistry: Collision Detection and Name Lookup

```cpp
class SkillRegistry {
public:
    // Existing
    void register_skill(SkillDescriptor descriptor);
    [[nodiscard]] bool has_skill(uint32_t skill_id) const;

    // New: name-based lookup
    [[nodiscard]] bool has_skill(std::string_view name) const;
    [[nodiscard]] uint32_t get_skill_id(std::string_view name) const;

    // ...
};
```

`register_skill()` gains collision detection:

```cpp
void SkillRegistry::register_skill(SkillDescriptor descriptor) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = skills_.find(descriptor.id);
    if (it != skills_.end() && it->second.name != descriptor.name) {
        // Hash collision between different skills — reject registration
        log_error("Skill ID collision: \"" + descriptor.name +
                  "\" and \"" + it->second.name +
                  "\" both hash to " + std::to_string(descriptor.id));
        return;
    }

    skills_[descriptor.id] = std::move(descriptor);
}
```

Additionally, add an index by name for O(1) name-based lookups:

```cpp
// In SkillRegistry private:
std::unordered_map<std::string, uint32_t> name_to_id_;
```

### Skill Class Changes

Each builtin skill transitions from a `SkillIds::` constant to a hashed name:

**Before:**
```cpp
class MathOperationSkill : public Skill<MathOperationSkill> {
    static constexpr uint32_t kSkillId = SkillIds::MathOperation;  // = 2
};
```

**After:**
```cpp
class MathOperationSkill : public Skill<MathOperationSkill> {
    static constexpr uint32_t kSkillId = SkillKey::from_name("builtin.MathOperation");
};
```

### Registration Macro Changes

**Before:**
```cpp
REGISTER_SKILL_CLASS(MathOperationSkill, "MathOperation",
    "Performs scalar math operations", 1, 64, 64);
```

**After:**
```cpp
REGISTER_SKILL_CLASS(MathOperationSkill, "builtin.MathOperation",
    "Performs scalar math operations", 1, 64, 64);
```

The name passed to `REGISTER_SKILL_CLASS` must match the name used in `kSkillId = SkillKey::from_name(...)`. A `static_assert` in the `Skill<Derived>` base class can catch mismatches at compile time (see Phase 2).

### Power User Skill Example

```cpp
// alice/MonteCarloSkill.hpp
class MonteCarloSkill : public Skill<MonteCarloSkill> {
public:
    using RequestPtrs = MonteCarloRequestPtrs;
    using ResponsePtrs = MonteCarloResponsePtrs;

    // No need to edit any central header — name is self-contained
    static constexpr uint32_t kSkillId =
        SkillKey::from_name("alice.MonteCarloSample");

    // ... scatter_request, scatter_response, compute ...
};

// alice/MonteCarloSkill.cpp
REGISTER_SKILL_CLASS(MonteCarloSkill, "alice.MonteCarloSample",
    "Monte Carlo sampling for simulation", 1, 1024, 512);
```

Alice doesn't touch `SkillIds.hpp`. Bob doesn't touch `SkillIds.hpp`. Their skills coexist because `"alice.MonteCarloSample"` and `"bob.ImageResize"` hash to different IDs.

---

## Wire Protocol: Skill Manifest (Future)

When capability negotiation is added to the transport layer, string names travel in the handshake, not on every task message:

```
Dispatcher → Worker (on connect):
  SkillManifest {
    { name: "builtin.StringReversal",     id: 0xA1B2C3D4, version: 1, schema_hash: 0xAB12 },
    { name: "alice.MonteCarloSample",     id: 0xE5F6A7B8, version: 1, schema_hash: 0xCD34 }
  }

Worker → Dispatcher:
  WorkerCapabilities {
    { name: "builtin.StringReversal",     version: 1 },
    { name: "alice.MonteCarloSample",     version: 1 }
  }
```

Task payloads on the wire continue to use compact 32-bit IDs — no per-message string overhead. Both sides derived the same ID from the same name, so they agree.

If a hash collision ever occurs between two skills that both need to coexist in the same session, the manifest can remap one to a session-local ID. This is a future escape hatch, not needed initially.

---

## Steps

### Phase 1: SkillKey and Collision Detection

1. **Create `skills/registry/SkillKey.hpp`** — `from_name()` and `is_valid_name()` constexpr functions
2. **Add collision detection to `SkillRegistry::register_skill()`** — reject if computed ID matches an existing skill with a different name
3. **Add name-based lookup to `SkillRegistry`** — `has_skill(string_view)`, `get_skill_id(string_view)`, internal `name_to_id_` index

### Phase 2: Migrate Builtin Skills

4. **Update `MathOperationSkill`** — change `kSkillId` to `SkillKey::from_name("builtin.MathOperation")`
5. **Update `StringReversalSkill`** — change `kSkillId` to `SkillKey::from_name("builtin.StringReversal")`
6. **Update `VectorMathSkill`** — change `kSkillId` to `SkillKey::from_name("builtin.VectorMath")`
7. **Update `FusedMultiplyAddSkill`** — change `kSkillId` to `SkillKey::from_name("builtin.FusedMultiplyAdd")`
8. **Update all `REGISTER_SKILL_CLASS` calls** — use namespaced names (e.g., `"builtin.MathOperation"`)

### Phase 3: Update SkillDescriptor Factory Methods

9. **Update `SkillDescriptor::create()` overloads** — derive `id` from `name` via `SkillKey::from_name()` instead of accepting an explicit ID parameter
10. **Update `REGISTER_SKILL` and `REGISTER_SKILL_CLASS` macros** — remove explicit `id` parameter; derive from name

### Phase 4: Remove SkillIds.hpp

11. **Remove `skills/registry/SkillIds.hpp`** — no longer needed
12. **Update all references to `SkillIds::` constants** — replace with `SkillKey::from_name("builtin.X")` or registry name-based lookup
13. **Remove `SkillIds::Count` and `SkillIds::MaxSkillId`** — these are meaningless with hashed IDs; use `SkillRegistry::skill_count()` and `SkillRegistry::skill_ids()` instead

### Phase 5: Validation and Documentation

14. **Add unit tests** — verify `from_name()` produces stable values, verify collision detection rejects, verify name lookup works
15. **Add a compile-time consistency check** — `static_assert` or registration-time check that the name passed to `REGISTER_SKILL_CLASS` matches the name used in `kSkillId`
16. **Document the naming convention** — update `skills/README.md` with the `<namespace>.<SkillName>` convention, examples for power users
17. **Build and test all targets** — generators, worker, skills — ensure no regressions

---

## Files Changed

**New:**
- `skills/registry/SkillKey.hpp`

**Modified:**
- `skills/registry/SkillRegistry.hpp` — add name-based lookup, `name_to_id_` index
- `skills/registry/SkillRegistry.cpp` — collision detection, name index maintenance
- `skills/registry/SkillDescriptor.hpp` — factory methods derive ID from name
- `skills/registry/SkillRegistration.hpp` — macros derive ID from name
- `skills/builtins/MathOperationSkill.hpp` — `kSkillId` uses `SkillKey::from_name()`
- `skills/builtins/StringReversalSkill.hpp` — same
- `skills/builtins/VectorMathSkill.hpp` — same
- `skills/builtins/FusedMultiplyAddSkill.hpp` — same
- `skills/builtins/MathOperationSkill.cpp` — namespaced name in `REGISTER_SKILL_CLASS`
- `skills/builtins/StringReversalSkill.cpp` — same
- `skills/builtins/VectorMathSkill.cpp` — same
- `skills/builtins/FusedMultiplyAddSkill.cpp` — same
- `generators/common/TaskGenerator.cpp` — remove `SkillIds::` references (already done if IGenerator plan was applied)
- `skills/README.md` — document naming convention

**Deleted:**
- `skills/registry/SkillIds.hpp`

---

## Key Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Hash algorithm | **FNV-1a 32-bit** | Fast, constexpr-friendly, well-distributed, widely used (e.g., LLVM, Rust) |
| Name format | **`namespace.SkillName`** | Simple, readable, mirrors Java/gRPC conventions |
| Collision handling | **Reject at registration** | Fail-fast with clear error message; collisions are near-impossible with namespaced names |
| Builtin migration | **Clean break** (Option A) | No existing deployed workers to maintain wire compat with; eliminates legacy path |
| Central ID header | **Delete `SkillIds.hpp`** | Keeping it invites the old pattern; removing it forces adoption of the new scheme |
| Wire format | **Compact 32-bit IDs** (unchanged) | Names travel in handshake manifest only; per-task overhead is zero |
| Future escape hatch | **Session-local ID remapping** via manifest | Available if a collision ever occurs; not needed initially |
