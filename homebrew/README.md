# Homebrew tap

Homebrew formulas for the macOS distribution of Task Messenger. The
formulas in [Formula/](Formula/) are kept in sync with each release
by the `release.yml` GitHub Actions workflow: tagging a release
rebuilds the macOS distributions, recomputes SHA-256 checksums, and
commits the updated formulas back to the repository.

## Install

```bash
brew tap <username>/task-messenger https://github.com/<username>/task-messenger
brew install tm-dispatcher tm-worker
```

Replace `<username>` with the GitHub user or organisation hosting the
tap. The same formulas can also be hosted in a dedicated
`homebrew-task-messenger` repository if you prefer a separate tap.

After install, configuration files are created under
`~/Library/Application Support/TaskMessenger/`. Run with:

```bash
tm-dispatcher -c "~/Library/Application Support/TaskMessenger/config/dispatcher/config-dispatcher.json"
tm-worker     -c "~/Library/Application Support/TaskMessenger/config/worker/config-worker.json"
```

Update with `brew upgrade tm-dispatcher tm-worker`; uninstall with
`brew uninstall tm-dispatcher tm-worker`.

## Permissions

Homebrew needs write access to its prefix
(`/opt/homebrew` on Apple Silicon, `/usr/local` on Intel). On a
restricted account, use the `.command` installer on the macOS release
page instead — see
[docs/INSTALLATION.md](../docs/INSTALLATION.md#macos).

To repair Homebrew permissions when you do have admin rights:

```bash
sudo chown -R "$(whoami)" /opt/homebrew
```

## Testing a formula locally

Three options, in increasing order of fidelity to a real install:

1. **Test installation without Homebrew (no sudo).** Build the
   distribution and either double-click the `.command` installer or
   extract the tarball manually and exercise the binary. This is the
   recommended local-test path on restricted accounts.

   ```bash
   ./extras/scripts/build_distribution_macos.sh worker
   open dist/tm-worker-v*.command
   ```

2. **Local tap (requires sudo).** Symlink `Formula/` into a
   throwaway tap inside the Homebrew repository, then
   `brew install local/task-messenger/tm-dispatcher`.

3. **Tap from GitHub.** Push the formula change, then
   `brew tap` against the repository and install as a real user
   would.

## What the formulas do

The formulas automate what `extras/scripts/install_macos.sh` does:
extract the tar.gz, copy binaries into the Homebrew prefix, copy
`libzt.dylib` into `lib`, copy config templates into `etc`, and
create user config directories on first run. Each formula:

- detects the architecture (`arm64` / `x86_64`),
- pins to a specific version and SHA-256,
- emits post-install caveats showing where the config lives,
- includes a `test do` block that runs `--version`.

## Comparison with the `.command` installer

| Feature | Homebrew | `.command` installer |
| --- | --- | --- |
| Requires admin rights | Yes (for prefix writes) | No |
| Update flow | `brew upgrade` | Re-download from releases |
| Uninstall | `brew uninstall` | Desktop uninstaller shortcut |
| App bundles in `/Applications` | No | Yes |
| Best for | CLI/dev use | End users on restricted accounts |

See [docs/INSTALLATION.md](../docs/INSTALLATION.md) for the full
end-user install matrix.
