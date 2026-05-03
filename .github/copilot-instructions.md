# Copilot instructions

This repository is **task-messenger**, a small distributed
computational network. The repo root [README.md](../README.md) and
[docs/TaskMessenger.md](../docs/TaskMessenger.md) are the front doors;
each component documents its own scope in its own README.

## Standing prompts

When the task at hand matches one of the prompts under
[.github/prompts/](prompts/), follow it. The most load-bearing one:

- **Any documentation, README, source-comment, or `.github/prompts/`
  edit** — follow
  [.github/prompts/documentation-conventions.md](prompts/documentation-conventions.md).
  It defines the top-down scope rule, present-tense wording, Doxygen
  per-file rules, the `PLAN:` transient-marker convention (and the
  required final cleanup phase), and the prompts-directory
  conventions.

Operation-specific prompts:

- Adding an in-tree skill →
  [.github/prompts/add-builtin-skill.md](prompts/add-builtin-skill.md).
- Adding a generator →
  [.github/prompts/add-generator.md](prompts/add-generator.md).
- Provisioning a codespace worker →
  [.github/prompts/bootstrap-codespace-worker.md](prompts/bootstrap-codespace-worker.md).
- Operating the GCP-hosted rendezvous service →
  [.github/prompts/rendezvous-deployment-strategy.md](prompts/rendezvous-deployment-strategy.md).

The prompts index lives at
[.github/prompts/README.md](prompts/README.md). Treat
[.github/prompts/archive/](prompts/archive/) as read-only historical
context, not as guidance for new work.

## Build

The project uses Meson:

```powershell
meson setup builddir --buildtype=release
meson compile -C builddir
```

Doxygen site (after README-touching changes):

```powershell
meson compile -C builddir docs
```
