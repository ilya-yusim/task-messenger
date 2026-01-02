# Third-Party Subprojects

This repository vendors all external dependencies inside `subprojects/` so Meson can build Task Messenger without relying on system-wide installs. Below is a quick reference to each third-party package and what it contributes.

| Package | Location | Purpose |
| --- | --- | --- |
| CLI11 | `subprojects/CLI11` | Header-only command-line parser powering manager and worker option handling. |
| FTXUI | `subprojects/ftxui` | Terminal UI library used by the optional worker dashboard. |
| FTXUI Wrapper | `subprojects/ftxui-wrapper` | Meson helper that exposes FTXUI as a subproject dependency. |
| libzt Wrapper | `subprojects/libzt-wrapper` | Pulls in ZeroTier (`libzt`) and patches/build helpers so transport stacks can open virtual-network sockets. |
| nlohmann_json | `subprojects/nlohmann_json` | JSON serialization/deserialization used for configs and task payload metadata. |
| shared | `subprojects/shared` | Common utility headers (logger, queue helpers, options plumbing) shared across targets; kept here to avoid circular subproject references even though it’s maintained in-tree. |

When adding or upgrading dependencies:
1. Drop the source under `subprojects/<name>/` (or update the `.wrap` file).
2. Regenerate `meson.build` references as needed.
3. Update this table so consumers understand which packages ship with the repo and why.
