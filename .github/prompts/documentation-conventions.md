# Documentation conventions — agent hand-off

Use this prompt as the standing rulebook for any documentation work in
this repository: README rewrites, new READMEs, Doxygen pages, source
comments, and the language used in `.github/prompts/`. Apply it
incrementally — one edit at a time is fine — but stay consistent with
what is already in tree.

## Top-down scope rule

Each documentation level describes **only its own scope** and links
down for sub-component details. Sub-component details never bubble
upward.

- Repo root [README.md](../../README.md) frames the whole network and
  lists components in a table; no per-component details.
- Component READMEs (e.g. [dispatcher/README.md](../../dispatcher/README.md),
  [worker/README.md](../../worker/README.md),
  [skills/README.md](../../skills/README.md)) describe what the
  component is and link down to sub-component READMEs.
- Sub-component READMEs describe internals and link **up** with a
  short "Related documentation" / "Parent component" breadcrumb.
- If a detail must leak upward, flag it during review rather than
  silently summarising it twice.

## Present-tense rule

All committed documentation — Markdown and source comments — describes
what the code does **today**. Strip dev-history wording during writing,
not at the end:

- No `Phase N`, `Slice N`, `Stage N`, `Pass N`, `Step N`, `Option A/B`,
  `v1 / v2`, "now", "currently", "we will".
- No "this PR adds …" or "next we will …" framing.
- Prefer plain English ordering ("first / then / finally") only when
  the comment is genuinely describing sequential code; otherwise drop
  the marker.
- Procedural end-user docs ([docs/INSTALLATION.md](../../docs/INSTALLATION.md))
  are the one place ordered "Step N" / "Option A/B" headings stay,
  because the reader is following them as a checklist.

## Transient plan-step references — and how to retire them

When executing a multi-phase plan it is fine — encouraged, even — to
leave **transient** comments referencing the plan's step or phase
number while the work is in flight. They make code review and partial
landings easier.

Two hard rules go with that allowance:

1. The plan **must** include a final phase whose only job is to
   remove every transient reference and tie the documentation and
   comments back to the present-tense, top-down format above. No
   plan ships without it.
2. Every transient comment must be findable by a single grep so that
   final phase is mechanical. Use one of these prefixes consistently:

   ```text
   // PLAN: <plan-name> phase 2 — <what this hook is for>
   // PLAN: <plan-name> step 4 — <what this hook is for>
   ```

   The final phase greps for `\bPLAN:` (and any other agreed prefix)
   and rewrites or deletes each match. Once the plan ships, no
   `PLAN:` markers remain in tree.

The plan itself is a session artefact, not a committed file. Keep it
in session memory or a working document; do not commit
phase-by-phase plan files into the repo. Completed plans that are
worth preserving for context belong in
[.github/prompts/archive/](archive/).

## Doxygen rules

The repository ships a Doxygen site, so documentation directives are
load-bearing. Follow these per-file rules:

| File class | Doxygen markup |
| --- | --- |
| Repo root [README.md](../../README.md) | Plain Markdown. No directives — they would conflict with the `\mainpage`. |
| [docs/TaskMessenger.md](../../docs/TaskMessenger.md) | Owns `\mainpage`. Defines the top-level `\defgroup task_messenger` and the dispatcher / worker subgroups. |
| Component READMEs (dispatcher, worker, skills, transport, message, services/rendezvous) | Preserve and use existing `\defgroup` / `\ingroup` / `\page`. New component READMEs that warrant nav presence may add a `\defgroup`. |
| Sub-component READMEs (transport/coro, worker/runtime, skills/registry, …) | Preserve existing `\ingroup`. Do not introduce new groups; sub-components inherit their parent's group. |
| Operator-facing READMEs (config, extras, dashboard, homebrew, generators, worker-farm, services/rendezvous handoff prompts) | No Doxygen directives. They are end-user prose. |
| C++ headers | Preserve every `\brief`, `\param`, `\return`, `\file`, `\ingroup`. Strip only dev-history prose. Never add directives during a comment cleanup. |

After any README-touching change, rebuild Doxygen
(`meson compile -C builddir docs`) and diff the generated `html/`
group/index pages. Any change must be intentional.

## Linking style

- Prefer relative links from the current file's directory.
- Use Markdown links, not bare URLs, when pointing at workspace files.
- Sub-READMEs include a one-line "Parent component" or "Related
  documentation" section near the top so the upward path is always
  one click away.
- When two READMEs naturally cross-reference (registry ↔ codegen ↔
  builtins, dispatcher ↔ rendezvous), keep the link reciprocal.

## Source-comment rules

- Describe the code as it stands. Do not narrate the change that
  introduced the comment.
- File-level `\file` doc comments are welcome where they help Doxygen
  group files into the right module.
- Inline comments explain *why* a non-obvious decision was made (e.g.
  "the dispatch path copies the `shared_ptr` under the lock to avoid
  use-after-free during concurrent unregistration"), not *what* the
  next line does.

## `.github/prompts/` rules

- One operation or component per prompt file.
- Sections in this order: **Inputs → Files of interest → Steps the
  agent performs → Verification → Out of scope.**
- Cross-link to other prompts and to component READMEs rather than
  restating their content. The README and the prompt should never
  drift; the README is the source of truth, the prompt is the
  recipe.
- Speculative material lives in a `proposals/` subdirectory only when
  there is real speculative work to track. Do not pre-create empty
  taxonomies.

## Verification checklist

When reviewing a doc PR, run through these:

1. `grep -RInE "Phase [0-9]|Slice [0-9]|Stage [0-9]|Step [0-9]|Pass [0-9]|Option A|Option B|PLAN:" -- ':!subprojects' ':!.github/prompts/archive' ':!docs/INSTALLATION.md'`
   returns no matches.
2. Each touched README has a "Parent component" / "Related
   documentation" link if it is a sub-component, or a top-down
   components list if it is a component.
3. `meson compile -C builddir docs` succeeds and the Doxygen index
   diff matches the intended structural change.
4. The repo root [README.md](../../README.md) reads end-to-end with
   no leaked sub-component details.
