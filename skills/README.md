# Skills

\defgroup skills_module Skills

A **skill** is a typed compute kernel that workers execute on behalf
of a dispatcher. Skills are addressed by namespaced string names
(for example `builtin.StringReversal`), looked up through the skill
registry, and dispatched against pre-allocated request and response
buffers.

## Authoring a skill

A skill is two files dropped into [builtins/](builtins/README.md):

- **`<Skill>.skill.toml`** — the skill schema. Declares the skill
  name, version, request and response field shapes, and any
  fixed/maximum sizes. The build pipeline reads this to generate the
  C++ accessors used by both the dispatcher (for shape lookup and
  test-buffer creation) and the worker (for typed compute).
- **`<Skill>_impl.hpp`** — your compute implementation. Reads typed
  request fields from a generated request type and writes typed
  response fields to a generated response type.

Built-in skills (e.g. `StringReversalSkill`, `MathOperationSkill`,
`VectorMathSkill`, `FusedMultiplyAddSkill`, the BLAS skills under
[builtins/blas/](builtins/blas/)) follow this pattern and are good
templates. See [builtins/README.md](builtins/README.md) for the
catalog.

> External user-authored skills (drop-in plugins outside this tree)
> are a roadmap item. Today, adding a skill means adding a pair of
> files in `skills/builtins/` and rebuilding.

## Using a skill

Both the dispatcher and the worker reach skills through a shared
`SkillRegistry`. The dispatcher looks up the skill by name to obtain
its ID and to allocate properly sized request and response buffers;
the worker dispatches incoming requests to the registered handler.

```cpp
#include "skills/registry/SkillRegistry.hpp"
#include "skills/registry/SkillKey.hpp"

using namespace TaskMessenger::Skills;

if (SkillRegistry::instance().has_skill("builtin.StringReversal")) {
    auto id = SkillKey::from_name("builtin.StringReversal");
    auto request  = SkillRegistry::instance().create_test_request_buffer(id);
    auto response = SkillRegistry::instance().create_response_buffer(
        id, request->span());
    // Wrap in a TaskMessage and submit.
}
```

On the worker side:

```cpp
SkillRegistry& registry = SkillRegistry::instance();
bool ok = registry.dispatch(skill_id, task_id, request_span, response_span);
```

## Submodules

| Path | Scope |
| --- | --- |
| [builtins/](builtins/README.md) | Bundled skills shipped with the platform; the public reference for skill authors. |
| [registry/](registry/) | Skill registration, lookup, and dispatch backend. Implementation detail of the public API. |
| [codegen/](codegen/) | Schema-to-C++ pipeline that consumes `.skill.toml` files. Implementation detail of the public API. |

## Build options

The BLAS-backed skills under [builtins/blas/](builtins/blas/) are
gated by the Meson option `enable_blas_skills` (default `true`),
which pulls in OpenBLAS through the
[openblas-wrapper](../subprojects/openblas-wrapper/README.md)
subproject (Apple Accelerate on macOS).

## Related documentation

- Top-level overview: [README.md](../README.md).
- Worker dispatch: [worker/README.md](../worker/README.md).
- Dispatcher generators that submit skill requests:
  [generators/README.md](../generators/README.md).
