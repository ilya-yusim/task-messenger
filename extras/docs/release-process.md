# Building and Updating a GitHub Release

Authoritative source: [`.github/workflows/release.yml`](../../.github/workflows/release.yml).

The `Release` workflow builds all distributable components on all supported
platforms, uploads artifacts to a GitHub Release, and auto-updates the Homebrew
formulas. It can be triggered by pushing a version tag or manually via
`workflow_dispatch`.

## Automatic release (preferred)

Push a version tag — the workflow builds everything, creates the release, and
commits updated formulas.

```powershell
git tag v1.2.3
git push origin v1.2.3
```

The workflow will:

1. Build matrix: `{windows-latest, ubuntu-latest, macOS-ARM64} × {dispatcher, worker, rendezvous}`.
2. Upload artifacts: `*.exe` (Windows), `*.run` (Linux), `*.command`, `*.tar.gz`, `*.tar.gz.sha256` (macOS).
3. Create the GitHub Release with a generated changelog.
4. Update `homebrew/Formula/tm-dispatcher.rb`, `tm-worker.rb`, `tm-rendezvous.rb`
   (URL, version, sha256) and push the commit to the default branch via
   `stefanzweifel/git-auto-commit-action`.

## Manual / test release

Use `workflow_dispatch` on the Actions tab: **Actions → Release → Run workflow**.

Inputs:

- `os` (optional): restrict to one platform (`windows-latest`, `ubuntu-latest`, `macos`).
- `macos_runner` (optional): `github-hosted` or `self-hosted`. Overrides
  `vars.MACOS_RUNNER` for this run.
- `component` (optional): `dispatcher`, `worker`, or `rendezvous`.
- `skip_build` (optional): changelog-only dry run.

Manual runs produce a **draft** release tagged `draft-<sha>` and **skip** the
Homebrew formula commit.

## Test tag (`vtest`)

Pushing the `vtest` tag regenerates a prerelease. The workflow deletes any
existing `vtest` release and tag first, then rebuilds cleanly.

```powershell
git tag -f vtest
git push -f origin vtest
```

## Local smoke test (before tagging)

Run the platform-specific distribution script to reproduce the CI build locally.
Output lands in `dist/`.

```powershell
# Windows
.\extras\scripts\build_distribution.ps1 -Component rendezvous   # or: dispatcher | worker | all
```

```bash
# Linux
./extras/scripts/build_distribution.sh rendezvous

# macOS (Apple Silicon)
./extras/scripts/build_distribution_macos.sh rendezvous
```

## Typical end-to-end flow

1. Merge changes to `main`.
2. (Optional) Kick a `workflow_dispatch` run for one OS/component to verify.
3. Tag and push:
   ```powershell
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```
4. Watch **Actions → Release**. When green, the release is published and the
   Homebrew formulas are updated automatically.

## Artifact naming

- `tm-<component>-v<VERSION>-windows-x64-installer.exe`
- `tm-<component>-v<VERSION>-linux-x86_64.run`
- `tm-<component>-v<VERSION>-macos-arm64.command`
- `tm-<component>-v<VERSION>-macos-arm64.tar.gz` (+ `.sha256`)

where `<component>` is `dispatcher`, `worker`, or `rendezvous`.

## Choosing a macOS runner

The workflow can build macOS artifacts on either a GitHub-hosted runner
(`macos-latest`, Apple Silicon) or a self-hosted ARM64 Mac. Selection order:

1. `workflow_dispatch` input `macos_runner` (per-run override).
2. Repository variable `vars.MACOS_RUNNER` (persistent default).
3. Fallback: `github-hosted`.

### Setting the repository variable

Web UI: **Settings → Secrets and variables → Actions → Variables → New repository variable**.

- **Name**: `MACOS_RUNNER`
- **Value**: `github-hosted` or `self-hosted`

Direct URL: `https://github.com/<owner>/<repo>/settings/variables/actions`.

CLI alternative:

```bash
gh variable set MACOS_RUNNER --body "github-hosted"
```

Variables are plaintext and visible in logs; only use `${{ secrets.* }}` for
sensitive values.

## Notes

- Apple Silicon is the only supported macOS architecture; Intel Mac builds are
  not produced.
- Homebrew tap install:
  `brew tap <owner>/task-messenger && brew install tm-dispatcher tm-worker tm-rendezvous`.
- The Homebrew formula auto-update step runs only on tag pushes (not on
  `workflow_dispatch` and not for `vtest`).
