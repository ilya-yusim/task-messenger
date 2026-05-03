# `.github/prompts/`

This directory holds present-tense agent hand-off documents. Each
file scopes a single operation or component and lists inputs,
files-of-interest, the steps the agent performs, and verification.
The historical plans these prompts replaced live in
[archive/](archive/) for reference only and should not drive new work.

## Live prompts

| File | Use when you need to … |
| --- | --- |
| [documentation-conventions.md](documentation-conventions.md) | Write or review any documentation, source comment, or prompt — the standing rulebook (top-down scope, present-tense wording, Doxygen rules, transient `PLAN:` markers and how to retire them). |
| [add-builtin-skill.md](add-builtin-skill.md) | Add a new in-tree skill under [skills/builtins/](../../skills/builtins/). |
| [add-generator.md](add-generator.md) | Add a new generator (algorithm placeholder) that drives `tm-dispatcher`. |
| [bootstrap-codespace-worker.md](bootstrap-codespace-worker.md) | Provision a `tm-worker` on a GitHub Codespace via [worker-farm/](../../worker-farm/). |
| [rendezvous-deployment-strategy.md](rendezvous-deployment-strategy.md) | Operate, deploy, or update the GCP-hosted `tm-rendezvous` service. |

## Archive

[archive/](archive/) preserves the original implementation plans for
work that has shipped (skills identity hashing, IGenerator interface,
worker-farm controller, BLAS skills, GCP rendezvous deployment, the
original macOS port, the network dashboard design). They are kept for
historical context. Treat them as read-only — when a present-tense
operation needs documentation, add or update a live prompt here
instead of editing an archived file.

## Conventions

- One operation or component per file. Cross-link to other prompts
  rather than duplicating their content.
- Inputs first, files-of-interest second, steps third, verification
  fourth, out-of-scope last.
- Link to component READMEs (which document the public API and the
  long-form design) rather than re-stating their content here.
- Prefer present-tense, declarative wording. Avoid Phase / Slice /
  Stage / Step N / Option A/B framing.
