# macOS Build Testing Guide

## Context

You are connected to a MacInCloud server via VS Code Remote-SSH to test the macOS build system for task-messenger.

The following changes have been implemented:
- ✅ Meson build files updated for macOS/darwin support
- ✅ `extras/scripts/build_distribution_macos.sh` created
- ✅ GitHub Actions workflow updated (but GitHub doesn't provide macOS runners)

**Goal**: Test the macOS build locally on this server, then set up a self-hosted GitHub Actions runner.

---

## Step 1: Verify System Information

```bash
# Check macOS version
sw_vers

# Check architecture
uname -m
# Should show: x86_64 (Intel) or arm64 (Apple Silicon)

# Check available disk space
df -h

# Check current user
whoami
```

---

## Step 2: Install Dependencies

### Install Homebrew (if not present)

```bash
# Install Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Follow any post-install instructions (e.g., adding to PATH)
# Usually something like:
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"
```

### Install Build Tools

```bash
# Update Homebrew
brew update

# Install required tools
brew install cmake ninja pkg-config python3 git

# Verify installations
cmake --version
ninja --version
pkg-config --version
python3 --version
git --version
```

### Install Python Dependencies

```bash
# Install meson (specific version required)
pip3 install meson==1.9.2 ninja

# Verify meson version
meson --version
# Should show: 1.9.2

# Add Python bin to PATH if needed
echo 'export PATH="$HOME/Library/Python/3.x/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

---

## Step 3: Clone Repository

```bash
# Navigate to your projects directory
cd ~
mkdir -p projects
cd projects

# Clone the repository with submodules
git clone --recurse-submodules https://github.com/YOUR_USERNAME/task-messenger.git
cd task-messenger

# Verify submodules are initialized
git submodule status

# If submodules are missing:
git submodule update --init --recursive
```

---

## Step 4: Test the Build Script

### Check Script Permissions

```bash
# Make script executable
chmod +x extras/scripts/build_distribution_macos.sh

# Verify it's executable
ls -la extras/scripts/build_distribution_macos.sh
```

### Test Manager Build

```bash
# Run the build script for manager
./extras/scripts/build_distribution_macos.sh manager

# Expected output:
# - Build logs showing meson setup, compilation
# - Archive creation messages
# - Final summary with generated files
```

### Verify Build Output

```bash
# Check dist directory
ls -lh dist/

# Expected files:
# - tm-manager-v{version}-macos-{arch}.tar.gz
# - tm-manager-v{version}-macos-{arch}.tar.gz.sha256

# Verify archive contents
tar -tzf dist/tm-manager-*.tar.gz | head -20

# Should show:
# tm-manager/
# tm-manager/bin/tm-manager
# tm-manager/lib/libzt.dylib
# tm-manager/config/
# tm-manager/doc/
# tm-manager/scripts/
# etc.
```

### Test Worker Build

```bash
# Clean previous build
rm -rf dist/ dist-staging/

# Build worker
./extras/scripts/build_distribution_macos.sh worker

# Verify output
ls -lh dist/
```

### Test Both Components

```bash
# Clean
rm -rf dist/ dist-staging/

# Build all
./extras/scripts/build_distribution_macos.sh all

# Should generate 4 files:
# - tm-manager-*.tar.gz + .sha256
# - tm-worker-*.tar.gz + .sha256
ls -lh dist/
```

---

## Step 5: Verify Binary Functionality

### Extract and Test Manager

```bash
# Create test directory
mkdir -p ~/test-install
cd ~/test-install

# Extract manager archive
tar -xzf ~/projects/task-messenger/dist/tm-manager-*.tar.gz
cd tm-manager

# Check binary
file bin/tm-manager
# Should show: Mach-O 64-bit executable {arch}

# Check library
file lib/libzt.dylib
# Should show: Mach-O 64-bit dynamically linked shared library

# Check RPATH
otool -l bin/tm-manager | grep -A2 LC_RPATH
# Should show: @executable_path/../lib

# Check library dependencies
otool -L bin/tm-manager
# Should show: @rpath/libzt.dylib

# Test version flag (if implemented)
./bin/tm-manager --version || echo "Version flag not implemented"

# Test help flag
./bin/tm-manager --help || echo "Help flag not implemented"
```

### Test Library Loading

```bash
# Try to run the binary (may fail without config, but should not fail due to library loading)
./bin/tm-manager
# Expected: Error about missing config (GOOD)
# NOT expected: dyld library not found error (BAD)
```

---

## Step 6: Troubleshooting Common Issues

### Issue: Meson Not Found

```bash
# Find where pip installed meson
pip3 show meson

# Add to PATH
export PATH="$HOME/Library/Python/3.x/bin:$PATH"
```

### Issue: libzt Build Fails

```bash
# Check libzt build logs
cat subprojects/libzt-wrapper/libzt/build.log

# Try building libzt manually
cd subprojects/libzt-wrapper/libzt
./build.sh host release
cd ../../..
```

### Issue: CMake Errors

```bash
# Check CMake version (need 3.10+)
cmake --version

# Update if needed
brew upgrade cmake
```

### Issue: Permission Denied

```bash
# Check file permissions
ls -la extras/scripts/build_distribution_macos.sh

# Fix permissions
chmod +x extras/scripts/build_distribution_macos.sh
```

### Issue: Library Not Found at Runtime

```bash
# Check if dylib is in the right place
ls -la tm-manager/lib/libzt.dylib

# Check RPATH
otool -l tm-manager/bin/tm-manager | grep -A2 RPATH

# Check dylib install name
otool -L tm-manager/lib/libzt.dylib
```

### Issue: Version Extraction Fails

```bash
# Check meson.build
grep "project('task-messenger'" meson.build

# Manually extract version
VERSION=$(grep "project('task-messenger'" meson.build | grep -oP "version:\s*'\K[^']+")
echo "Version: $VERSION"
```

---

## Step 7: Clean Build Test

```bash
# Go back to project root
cd ~/projects/task-messenger

# Clean everything
rm -rf builddir* dist dist-staging

# Try a completely fresh build
./extras/scripts/build_distribution_macos.sh manager
```

---

## Step 8: Architecture-Specific Notes

### Intel (x86_64)

- Archive name: `tm-{component}-v{version}-macos-x86_64.tar.gz`
- libzt dist: `dist/macos-x64-host-release/lib/libzt.dylib`

### Apple Silicon (arm64)

- Archive name: `tm-{component}-v{version}-macos-arm64.tar.gz`
- libzt dist: `dist/macos-arm64-host-release/lib/libzt.dylib`

---

## Step 9: Report Results

### If Build Succeeds

Report:
- ✅ macOS version
- ✅ Architecture (x86_64 or arm64)
- ✅ Generated file sizes
- ✅ Binary verification results (file, otool output)
- ✅ Any warnings during build

### If Build Fails

Provide:
- ❌ Error message
- ❌ Build logs from `builddir-*-dist/meson-logs/meson-log.txt`
- ❌ Output of troubleshooting commands
- ❌ Any libzt build.log if relevant

---

## Step 10: Next Steps - Self-Hosted GitHub Runner (Optional)

Once builds work locally, you can set up this macOS server as a self-hosted GitHub Actions runner:

```bash
# Navigate to your GitHub repo
# Settings → Actions → Runners → New self-hosted runner → macOS

# Download the runner
mkdir actions-runner && cd actions-runner
curl -o actions-runner-osx-{version}.tar.gz -L https://github.com/actions/runner/releases/download/{version}/actions-runner-osx-{arch}.tar.gz
tar xzf ./actions-runner-osx-{version}.tar.gz

# Configure (follow GitHub's instructions - you'll need a token)
./config.sh --url https://github.com/YOUR_USERNAME/task-messenger --token YOUR_TOKEN

# Install as a service (runs builds automatically)
./svc.sh install
./svc.sh start

# Check status
./svc.sh status
```

Then update the workflow to use:
```yaml
runs-on: [self-hosted, macOS]
```

---

## Quick Reference Commands

```bash
# Full build test sequence
cd ~/projects/task-messenger
rm -rf dist dist-staging builddir*
./extras/scripts/build_distribution_macos.sh manager
ls -lh dist/
tar -tzf dist/tm-manager-*.tar.gz | head -10

# Quick verification
cd ~/test-install && rm -rf * && \
tar -xzf ~/projects/task-messenger/dist/tm-manager-*.tar.gz && \
cd tm-manager && \
file bin/tm-manager && \
otool -L bin/tm-manager && \
./bin/tm-manager --help
```

---

## Important Files

- **Build script**: `extras/scripts/build_distribution_macos.sh`
- **Meson files with macOS support**:
  - `meson.build`
  - `manager/meson.build`
  - `worker/meson.build`
  - `subprojects/libzt-wrapper/meson.build`
- **Build logs**: `builddir-*-dist/meson-logs/meson-log.txt`
- **libzt logs**: `subprojects/libzt-wrapper/libzt/build.log`

---

## Summary

1. ✅ Install dependencies (Homebrew, CMake, meson, etc.)
2. ✅ Clone repository with submodules
3. ✅ Run build script: `./extras/scripts/build_distribution_macos.sh manager`
4. ✅ Verify output in `dist/` directory
5. ✅ Extract and test binary
6. ✅ Check RPATH and library loading
7. ✅ Report results

Good luck with the macOS build! 🚀
