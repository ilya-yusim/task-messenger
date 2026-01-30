# TaskMessenger Homebrew Tap

This directory contains Homebrew formulas for TaskMessenger components.

## ⚠️ Important: System Requirements

**Homebrew installation requires write access to `/opt/homebrew` (or `/usr/local` on Intel Macs).**

- ✅ **If you have admin/sudo access**: Use Homebrew (recommended for developers)
- ❌ **If you don't have admin/sudo access**: Use [.command installers](#alternative-command-installers) instead

To fix Homebrew permissions (requires sudo):
```bash
sudo chown -R $(whoami) /opt/homebrew
```

If you cannot run the above command, **skip to the .command installer section below.**

## Setting Up the Tap

### Option 1: Create a Separate Tap Repository (Recommended)

1. Create a new GitHub repository named `homebrew-task-messenger`
2. Copy the `Formula/` directory to the root of that repository
3. Users install with:
   ```bash
   brew tap <username>/task-messenger
   brew install tm-manager tm-worker
   ```

### Option 2: Use Main Repository (Current Setup)

The formulas are kept in this repository and automatically updated on each release:
```bash
brew tap <username>/task-messenger https://github.com/<username>/task-messenger
brew install tm-manager tm-worker
```

Replace `<username>` with your GitHub username or organization name.

## Automated Formula Updates

**Formulas are automatically updated on each release!** 

The `.github/workflows/release.yml` workflow handles everything:

1. **On tag push** (e.g., `git tag v1.0.1 && git push --tags`):
   - Builds macOS distributions for both architectures
   - Generates SHA256 checksums
   - Updates both formulas with new version, URLs, and checksums
   - Auto-commits changes back to the repository

2. **No manual intervention needed** - formulas stay in sync with releases

3. **Dynamic repository handling** - formulas use `GITHUB_REPOSITORY` placeholder that gets replaced with your actual repo during the workflow

### How It Works

The workflow automatically:
- Extracts version from git tag
- Reads SHA256 checksums from generated `.sha256` files
- Updates version numbers in formulas
- Updates download URLs to point to new release
- Replaces placeholder checksums with actual values
- Injects your GitHub repository name dynamically
- Commits changes with message: `chore: update Homebrew formulas for vX.Y.Z`

## Testing Formulas Locally

**Note:** Local Homebrew testing requires write access to `/opt/homebrew/Cellar` (sudo access). If you don't have this, use Method 3 below to test the actual installation.

### Method 1: Test from GitHub Repository (Recommended)

After pushing changes to GitHub, test the actual tap:

```bash
# Add your repository as a tap
brew tap <username>/task-messenger https://github.com/<username>/task-messenger

# Install and test
brew install tm-manager
tm-manager --version

# Uninstall
brew uninstall tm-manager
brew untap <username>/task-messenger
```

### Method 2: Create Local Tap for Testing (Requires sudo)

Homebrew requires formulas to be in a tap. Create a local tap for testing:

```bash
# Create a local tap directory
mkdir -p $(brew --repository)/Library/Taps/local/homebrew-task-messenger

# Link your formula directory
ln -sf $(pwd)/homebrew/Formula $(brew --repository)/Library/Taps/local/homebrew-task-messenger/

# Install from local tap
brew install local/task-messenger/tm-manager

# Test
tm-manager --version

# Cleanup
brew uninstall tm-manager
rm -rf $(brew --repository)/Library/Taps/local/homebrew-task-messenger
```

### Method 3: Test Installation Without Homebrew (No sudo required)

**This is the recommended method for local testing without sudo access.**

Test the actual .command installer or manual tar.gz extraction:

```bash
# Option A: Test .command installer
./extras/scripts/build_distribution_macos.sh worker
open dist/tm-worker-v*.command  # Double-click to install

# Option B: Test tar.gz extraction manually
cd dist
tar -xzf tm-worker-v*-macos-arm64.tar.gz
cd tm-worker-v*-macos-arm64

# Test the binary directly
./bin/tm-worker --version

# Test with libzt.dylib
./bin/tm-worker --help

# Verify RPATH is set correctly
otool -L bin/tm-worker
# Should show: @rpath/libzt.dylib

# Cleanup
cd ../..
rm -rf dist/tm-worker-v*-macos-arm64
```

### What Homebrew Formulas Actually Do

The Homebrew formulas essentially automate what your install_macos.sh script does:
- Extract the tar.gz archive
- Copy binaries to `/opt/homebrew/bin` (or `/usr/local/bin`)
- Copy libraries to `/opt/homebrew/lib`
- Copy config templates to `/opt/homebrew/etc`
- Create user config directories on first run

If your .command installer works, the Homebrew formula will work too once properly tapped.

## Formula Structure

Each formula includes:
- **Architecture detection** - Automatic arm64/x86_64 selection
- **Config management** - Copies templates to user directories
- **Post-install setup** - Creates necessary directories and files
- **Helpful messages** - Shows config locations after install
- **Version testing** - Verifies installation works

## User Installation

Users can install with:

```bash
# Add tap
brew tap yourusername/task-messenger

# Install manager
brew install tm-manager

# Install worker
brew install tm-worker

# Update
brew upgrade tm-manager tm-worker

# Uninstall
brew uninstall tm-manager
```

## Benefits of Homebrew Distribution

✅ **Automatic updates** - `brew upgrade` keeps everything current  
✅ **Clean uninstall** - `brew uninstall` removes all files  
✅ **Dependency management** - Homebrew handles library dependencies  
✅ **Version pinning** - Users can install specific versions  
✅ **Famil (replace <username> with your GitHub username)
brew tap <username>/task-messenger

# Install manager
brew install tm-manager

# Install worker
brew install tm-worker

# Update to latest version
brew upgrade tm-manager tm-worker

# Uninstall
brew uninstall tm-manager tm-worker
```

### Configuration Locations

After installation, config files are created at:
- **Manager**: `~/Library/Application Support/TaskMessenger/config/manager/config-manager.json`
- **Worker**: `~/Library/Application Support/TaskMessenger/config/worker/config-worker.json`

To run with config:
```bash
tm-manager -c "~/Library/Application Support/TaskMessenger/config/manager/config-manager.json"
tm-worker -c "~/Library/Application Support/TaskMessenger/config/worker/config-worker.json"
```

## Alternative: .command Installers (No sudo required)

**Recommended for users without admin access or who prefer GUI installation.**

Instead of Homebrew, use the self-extracting .command installers:

1. **Download** from [GitHub Releases](https://github.com/GITHUB_REPOSITORY/releases/latest):
   - `tm-manager-vX.Y.Z-macos-arm64.command` (Apple Silicon)
   - `tm-worker-vX.Y.Z-macos-arm64.command` (Apple Silicon)

2. **Install** by double-clicking the `.command` file in Finder
   - Installs to `~/Library/Application Support/TaskMessenger`
   - Creates `.app` bundles in `/Applications`
   - Creates desktop uninstaller
   - No sudo required!

3. **Run** by double-clicking the `.app` in Applications or:
   ```bash
   tm-manager -c "~/Library/Application Support/TaskMessenger/config/manager/config-manager.json"
   ```

4. **Uninstall** by double-clicking the uninstaller on your Desktop

### .command vs Homebrew

| Feature | .command Installer | Homebrew |
|---------|-------------------|----------|
| **Requires sudo** | ❌ No | ✅ Yes (for setup) |
| **Target users** | End users, restricted accounts | Developers, admins |
| **Installation** | Double-click GUI | Command-line |
| **Updates** | Manual download | `brew upgrade` |
| **Uninstall** | Desktop shortcut | `brew uninstall` |
| **App bundles** | ✅ Yes (with icons) | ❌ No |
| **Auto-start** | ✅ Double-click .app | Command-line only |

**Use .command installers if:**
- You don't have admin/sudo access
- You prefer GUI installation
- You want desktop app shortcuts

**Use Homebrew if:**
- You have admin access
- You prefer command-line tools
- You want automatic updates via `brew upgrade`

## Benefits of Homebrew Distribution

✅ **Automatic updates** - `brew upgrade` keeps everything current  
✅ **Clean uninstall** - `brew uninstall` removes all files  
✅ **Dependency management** - Homebrew handles library dependencies  
✅ **Version pinning** - Users can install specific versions  
✅ **Familiar workflow** - Standard for macOS developers

**Note:** Requires proper Homebrew permissions. For restricted accounts, use .command installers.

## Comparison with .command Installers

| Feature | Homebrew | .command Installer |
|---------|----------|-------------------|
| Target Audience | Developers, CLI users | General users |
| Updates | `brew upgrade` | Manual download |
| Uninstall | `brew uninstall` | Double-click uninstaller |
| GUI Integration | Command-line only | Desktop .app bundle |
| Prerequisites | Homebrew installed | None |
| Best For | Development/server use | End-user desktop use |

**Recommendation:** Provide both methods:
- Homebrew for developers and CLI-focused users
- .command installers for end-users who want desktop apps
