# Test Linux Packaging Script

This guide provides step-by-step instructions for testing the Linux distribution packaging script in a Linux environment.

## Prerequisites

Before testing, verify the environment has:
- Meson build system installed
- Ninja build tool
- C++ compiler (GCC or Clang)
- Git
- Standard Unix tools (tar, sha256sum)
- All project dependencies

## Testing Steps

### 1. Verify Script Permissions

```bash
cd ~/projects/task-messenger  # or your project path
chmod +x extras/scripts/build_distribution.sh
```

### 2. Test Version Extraction

Verify the script can extract version from meson.build:

```bash
grep "project('task-messenger'" meson.build | grep -oP "version:\s*'\K[^']+"
```

Expected output: `1.0.0`

### 3. Test Manager Package Build

```bash
./extras/scripts/build_distribution.sh manager
```

**Expected behavior:**
- Creates `builddir-manager-dist/` directory
- Builds with `--buildtype=release`
- Installs to `dist-staging/manager/opt/task-messenger/`
- Creates `dist/task-messenger-manager-v1.0.0-linux-<arch>.tar.gz`
- Creates SHA256 checksum file

**Expected output structure:**
```
dist/
├── task-messenger-manager-v1.0.0-linux-x86_64.tar.gz
└── task-messenger-manager-v1.0.0-linux-x86_64.tar.gz.sha256
```

### 4. Verify Archive Contents

Extract and inspect the manager archive:

```bash
mkdir -p test-extract-manager
cd test-extract-manager
tar -xzf ../dist/task-messenger-manager-v1.0.0-linux-*.tar.gz
ls -R
```

**Expected structure:**
```
.
├── bin/
│   ├── manager
│   ├── identity.public
│   └── identity.secret
├── lib/
│   └── libzt.so
├── etc/
│   └── task-messenger/
│       └── config-manager.json
└── share/
    └── doc/
        └── task-messenger/
            ├── LICENSE
            ├── README.md
            ├── manager-README.md
            ├── message-README.md
            ├── transport-README.md
            └── worker-README.md
```

### 5. Test Manager Executable

```bash
cd test-extract-manager

# Test version output
./bin/manager --version
# Expected: task-messenger 1.0.0

# Check shared library dependency
ldd ./bin/manager | grep libzt
# Expected: libzt.so => <path>/lib/libzt.so

# Check RPATH configuration
readelf -d ./bin/manager | grep RUNPATH
# Expected: Library runpath: [...$ORIGIN/../lib...]

# Test help output (should not crash)
./bin/manager --help
```

### 6. Test Worker Package Build

```bash
cd ~/projects/task-messenger
rm -rf builddir-worker-dist dist-staging dist
./extras/scripts/build_distribution.sh worker
```

**Expected behavior:**
- Creates `builddir-worker-dist/` directory
- Builds worker executable
- Creates `dist/task-messenger-worker-v1.0.0-linux-<arch>.tar.gz`
- Creates SHA256 checksum

### 7. Verify Worker Archive

```bash
mkdir -p test-extract-worker
cd test-extract-worker
tar -xzf ../dist/task-messenger-worker-v1.0.0-linux-*.tar.gz
ls -R
```

**Expected structure:**
```
.
├── bin/
│   └── worker
├── lib/
│   └── libzt.so
├── etc/
│   └── task-messenger/
│       └── config-worker.json
└── share/
    └── doc/
        └── task-messenger/
            └── [docs...]
```

### 8. Test Worker Executable

```bash
cd test-extract-worker

# Test version output
./bin/worker --version
# Expected: task-messenger 1.0.0

# Check shared library dependency
ldd ./bin/worker | grep libzt
# Expected: libzt.so => <path>/lib/libzt.so

# Check RPATH
readelf -d ./bin/worker | grep RUNPATH
# Expected: Library runpath: [...$ORIGIN/../lib...]

# Test help output
./bin/worker --help
```

### 9. Test "All" Components Build

```bash
cd ~/projects/task-messenger
rm -rf builddir-*-dist dist-staging dist
./extras/scripts/build_distribution.sh all
```

**Expected behavior:**
- Builds both manager and worker
- Creates two separate archives in `dist/`
- Creates two checksum files

Verify both archives exist:
```bash
ls -lh dist/
```

Expected files:
- `task-messenger-manager-v1.0.0-linux-<arch>.tar.gz`
- `task-messenger-manager-v1.0.0-linux-<arch>.tar.gz.sha256`
- `task-messenger-worker-v1.0.0-linux-<arch>.tar.gz`
- `task-messenger-worker-v1.0.0-linux-<arch>.tar.gz.sha256`

### 10. Verify Checksums

```bash
cd dist
sha256sum -c task-messenger-manager-v1.0.0-linux-*.tar.gz.sha256
sha256sum -c task-messenger-worker-v1.0.0-linux-*.tar.gz.sha256
```

Expected output for each:
```
task-messenger-<component>-v1.0.0-linux-<arch>.tar.gz: OK
```

### 11. Test Clean Distribution Install

Simulate installing to a system directory (requires sudo or use /tmp):

```bash
# Manager installation test
cd ~/projects/task-messenger
mkdir -p /tmp/test-install
cd /tmp/test-install
tar -xzf ~/projects/task-messenger/dist/task-messenger-manager-v1.0.0-linux-*.tar.gz

# Test execution from installed location
./bin/manager --version

# Test with absolute path to simulate system install
cd /tmp/test-install/bin
./manager --version
```

### 12. Verify No Missing Dependencies

Check that the executables don't have missing dependencies:

```bash
ldd /tmp/test-install/bin/manager | grep "not found"
ldd /tmp/test-install/bin/worker | grep "not found"
```

**Expected:** No output (no missing dependencies)

## Common Issues and Solutions

### Issue: "libzt.so not found"
**Solution:** RPATH is not configured correctly. Check:
```bash
readelf -d ./bin/manager | grep RUNPATH
```
Should include `$ORIGIN/../lib`

### Issue: Script fails with "version not found"
**Solution:** Ensure meson.build has `version: '1.0.0'` in project() declaration

### Issue: Archive creation fails
**Solution:** Check that all required files exist in staging directory:
```bash
ls -R dist-staging/manager/opt/task-messenger/
```

### Issue: Permission denied when running script
**Solution:** Make script executable:
```bash
chmod +x extras/scripts/build_distribution.sh
```

## Success Criteria

✅ Script completes without errors  
✅ Archives created in `dist/` directory  
✅ SHA256 checksums generated  
✅ Checksum verification passes  
✅ Extracted executables show version 1.0.0  
✅ RPATH configured correctly ($ORIGIN/../lib)  
✅ No missing shared library dependencies  
✅ Executables run successfully (--help, --version)  
✅ Manager archive includes identity files  
✅ Both archives include libzt.so  
✅ Configuration and documentation files present  

## Cleanup

After testing:
```bash
cd ~/projects/task-messenger
rm -rf builddir-*-dist dist-staging dist
rm -rf test-extract-manager test-extract-worker /tmp/test-install
```

## Next Steps

Once Linux packaging is verified:
1. Test on different Linux distributions (Ubuntu, Fedora, Arch)
2. Verify on different architectures (x86_64, aarch64)
3. Test installation to actual system directories
4. Document any distribution-specific requirements
