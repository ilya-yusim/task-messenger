# Skill registry

\ingroup skills_module

The registry is the runtime backend behind the public skills API
documented in [skills/README.md](../README.md). Both the dispatcher
and the worker call into it; skill authors normally do not.

## Components

| File | Role |
| --- | --- |
| [ISkill.hpp](ISkill.hpp) | Public skill interface: identity (name, description, version, ID), payload factory, and the `process()` handler. |
| [Skill.hpp](Skill.hpp) | CRTP base implementing `ISkill::process()` by delegating to a derived class's `scatter_request`/`scatter_response`/`compute`. |
| [SkillKey.hpp](SkillKey.hpp) | Deterministic 32-bit skill ID derived from the namespaced string name via FNV-1a. Returns `std::optional<uint32_t>` (because FNV-1a can theoretically produce 0). |
| [SkillRegistration.hpp](SkillRegistration.hpp) | `REGISTER_SKILL_CLASS` macro for static-initialisation registration. |
| [SkillRegistry.{hpp,cpp}](SkillRegistry.hpp) | Thread-safe registry: registration, lookup, dispatch. Singleton via `instance()` or directly constructible for tests. |
| [PayloadBuffer.hpp](PayloadBuffer.hpp), [IPayloadFactory.hpp](IPayloadFactory.hpp) | Type-erased owned buffers and factories used to allocate request/response payloads. |
| [VerificationResult.hpp](VerificationResult.hpp), [CompareUtils.{hpp,cpp}](CompareUtils.hpp) | Floating-point comparison helpers used by the optional response-verification pipeline. |
| [MatrixSpan.hpp](MatrixSpan.hpp) | Lightweight matrix view used by the BLAS skills. |

## Identity model

Skills are identified by a namespaced string name (for example
`builtin.StringReversal`). The registry derives a deterministic 32-bit
ID from that name via `SkillKey::from_name`. There is no central
`enum` of IDs and there are no compile-time skill lists; adding a
skill is a pure additive operation.

`SkillRegistry::get_skill_id()` returns `std::optional<uint32_t>`.
Callers must use `has_value()` to check for presence — comparing
against `0` is incorrect because FNV-1a can produce zero.

## Thread safety

`SkillRegistry` stores skills as `std::shared_ptr<ISkill>`. The
dispatch path copies the `shared_ptr` under the registry mutex and
then releases the lock before invoking the handler, preventing
use-after-free if a skill is concurrently unregistered.

## Related documentation

- Public skill API and authoring: [skills/README.md](../README.md).
- Schema-to-C++ pipeline that produces concrete `ISkill` subclasses: [skills/codegen/README.md](../codegen/README.md).
- Built-in examples: [skills/builtins/README.md](../builtins/README.md).
