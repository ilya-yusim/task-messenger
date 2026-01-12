# GitHub Actions Release Workflow Plan

**Date:** January 12, 2026  
**Project:** task-messenger

## Overview

Create an automated workflow that builds release artifacts for Windows and Linux, generates installers, and publishes them to GitHub Releases when a version tag is pushed. The Git tag will be the authoritative version source, with parallel matrix builds and automated conventional commits changelog grouped by type. Support `vtest` tag for workflow development with release cleanup.

## Key Decisions

- **Version Source:** Git tag is source of truth (extract from tag, update meson.build during build)
- **Build Strategy:** Parallel matrix execution (2 OS × 2 components = 4 parallel builds)
- **Changelog:** Automatic generation from conventional commits, grouped by type (Features, Bug Fixes, Breaking Changes, etc.)
- **First Release:** Placeholder message when no previous tag exists
- **Test Tag:** `vtest` for development iterations with automatic cleanup/overwrite
- **Permissions:** Explicit `contents: write` declaration for portability and security

## Architecture

### Triggers
- Tag push matching `v*` pattern (production: `v1.2.3`, test: `vtest`)
- Manual workflow dispatch for testing

### Jobs

#### 1. Build Job (Matrix)
**Matrix dimensions:**
- `os`: [windows-latest, ubuntu-latest]
- `component`: [manager, worker]

**Steps:**
1. Checkout repository
2. Extract version from tag (strip `v` prefix)
3. Update `meson.build` version field using:
   - Windows: PowerShell `(Get-Content).replace()`
   - Linux: `sed -i`
4. Install dependencies:
   - Both: Python, Meson, Ninja (via pip)
   - Linux only: CMake, build-essential (via apt)
5. Cache subprojects directory and pip packages
6. Execute build script:
   - Windows: `.\extras\scripts\build_distribution.ps1 -Component $component`
   - Linux: `./extras/scripts/build_distribution.sh $component`
7. Upload artifacts from `dist/` directory

**Artifacts produced per matrix cell:**
- Windows: `.exe` installer + `.sha256`
- Linux: `.tar.gz` + `.run` installer + `.sha256` files

#### 2. Release Job
**Dependencies:** All build jobs must complete successfully

**Steps:**
1. Detect if tag is `vtest` (test iteration)
2. If test tag: Delete existing `vtest` release and tag via GitHub API
3. Fetch previous production tag (for changelog)
4. Generate changelog:
   - Parse commits between current and previous tag
   - Group by conventional commit type (feat, fix, breaking, etc.)
   - Format as markdown sections
   - Use placeholder message if no previous tag (first release)
5. Download all build artifacts
6. Create GitHub release:
   - Use tag name as release title
   - Include generated changelog as body
   - Mark as pre-release if `vtest`
7. Upload all artifacts:
   - 4 installers (2 OS × 2 components)
   - 4+ SHA256 checksum files

## Technical Details

### Version Extraction
```bash
# Extract version from refs/tags/v1.2.3 → 1.2.3
VERSION=${GITHUB_REF#refs/tags/v}
```

### Meson.build Update
```bash
# Linux
sed -i "s/version: '[^']*'/version: '$VERSION'/" meson.build

# Windows PowerShell
$content = Get-Content meson.build -Raw
$content -replace "version: '[^']*'", "version: '$VERSION'" | Set-Content meson.build
```

### Caching Strategy
Cache keys based on:
- Subprojects: Hash of `subprojects/*.wrap` files
- Pip packages: OS + Python version
- Restore keys for partial matches

### Conventional Commit Parsing
Parse commit messages for types:
- `feat:` → Features section
- `fix:` → Bug Fixes section
- `BREAKING CHANGE:` → Breaking Changes section
- `docs:`, `chore:`, `refactor:`, `perf:`, `test:`, `ci:` → grouped accordingly

### Test Tag Cleanup
Use GitHub REST API via `actions/github-script`:
```javascript
// Delete existing vtest release if exists
await github.rest.repos.deleteRelease({ owner, repo, release_id })
// Delete tag
await github.rest.git.deleteRef({ owner, repo, ref: 'tags/vtest' })
```

## Permissions Required

```yaml
permissions:
  contents: write  # Create/delete releases, upload assets
```

## Expected Artifacts

### Windows (per component)
- `task-messenger-{component}-v{version}-windows-x64-installer.exe`
- `task-messenger-{component}-v{version}-windows-x64-installer.exe.sha256`

### Linux (per component)
- `task-messenger-{component}-v{version}-linux-x86_64.tar.gz`
- `task-messenger-{component}-v{version}-linux-x86_64.tar.gz.sha256`
- `task-messenger-{component}-v{version}-linux-x86_64.run`
- `task-messenger-{component}-v{version}-linux-x86_64.run.sha256`

## Usage

### Production Release
```bash
git tag v1.0.0
git push origin v1.0.0
# Workflow runs automatically
# Release created at https://github.com/owner/task-messenger/releases/tag/v1.0.0
```

### Testing Workflow
```bash
git tag vtest
git push origin vtest --force  # Can reuse tag during development
# Workflow runs, overwrites previous vtest release
# Release marked as pre-release
```

### Manual Trigger
- Go to Actions → Release → Run workflow
- Workflow runs but doesn't create release (no tag context)

## Future Enhancements

### macOS Support
- Add `macos-latest` to matrix when ready
- Install dependencies via Homebrew
- Create `.dmg` or `.pkg` installers
- Note: macOS runners cost 10x Linux minutes

### Additional Features
- Release draft mode for review before publishing
- Artifact signing/notarization
- Docker image publishing
- Automated release notes review via PR
- Slack/Discord notifications on release
- Upload to package registries

## Dependencies on Existing Infrastructure

### Build Scripts (Already Exist)
- `extras/scripts/build_distribution.ps1` - Windows build automation
- `extras/scripts/build_distribution.sh` - Linux build automation
- Both scripts handle: version extraction, Meson setup, compilation, packaging, checksum generation

### Build System
- `meson.build` - Project configuration with version field
- `meson_options.txt` - Build options (components, debugging, profiling)
- Subprojects in `subprojects/` - All dependencies vendored

### Configuration
- `config/config-manager.json` - Manager configuration template
- `config/config-worker.json` - Worker configuration template
- ZeroTier identity files in `config/vn-manager-identity/`

## Success Criteria

✅ Tag push triggers automatic builds on both platforms  
✅ Four parallel builds complete successfully  
✅ All artifacts uploaded to GitHub release  
✅ Changelog automatically generated from commits  
✅ Test tag `vtest` can be reused for iteration  
✅ Production releases are immutable  
✅ Build logs available for debugging  
✅ SHA256 checksums generated for all artifacts

## References

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Meson Build System](https://mesonbuild.com/)
- [Conventional Commits](https://www.conventionalcommits.org/)
- Project build scripts: `extras/scripts/build_distribution.*`
