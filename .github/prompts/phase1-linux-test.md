# Phase 1 Linux Test: LibZT Shared Library Conversion

## Context

This is a test of the Phase 1 implementation (converting libzt from static to shared library) on Linux. Phase 1 has been successfully completed and tested on Windows. This prompt will guide you through applying the same changes and verifying they work on Linux.

## Background

The task-messenger project uses Meson build system with two components:
- **Manager**: Server component that distributes tasks
- **Worker**: Client component that processes tasks

Both components depend on libzt (ZeroTier networking library) which was originally built as a static library. We're converting it to use a shared library instead.

## Windows Changes Already Made

The following files have been modified for Windows:

1. **subprojects/libzt-wrapper/build-libzt.ps1** - Added DLL import library copying
2. **subprojects/libzt-wrapper/meson.build** - Changed to find shared library, removed static-only link flags, added RPATH for Linux
3. **subprojects/libzt-wrapper/README.md** - Updated documentation

## Linux-Specific Tasks

### Step 1: Verify Current State

Check what currently exists:

```bash
# Check if libzt wrapper meson.build has the shared library changes
cat subprojects/libzt-wrapper/meson.build | grep -A 2 "Step 4: Find the built"

# Check if RPATH configuration exists
cat subprojects/libzt-wrapper/meson.build | grep -A 5 "Configure RPATH"
```

**Expected**: You should see:
- `cc.find_library('libzt-shared', dirs: [libzt_lib_path], required: true, static: false)`
- RPATH configuration with `$ORIGIN/../lib`

### Step 2: Create/Verify Linux Build Script

Linux uses libzt's own `build.sh` script (located inside the cloned libzt repository). The wrapper does NOT have a `build.sh` equivalent to Windows' `build-libzt.ps1`.

Check if libzt's build.sh exists after cloning:
```bash
# After meson setup, check if libzt was cloned
ls -la subprojects/libzt-wrapper/libzt/build.sh 2>/dev/null && echo "Found libzt build.sh" || echo "libzt not cloned yet"
```

### Step 3: Clean Build Test

Perform a clean build to test the shared library configuration:

```bash
# Remove existing build directories
rm -rf builddir-manager builddir-worker

# Setup manager build
meson setup builddir-manager --buildtype=debug

# Check for any errors during libzt build
# Look for messages about building libzt for Linux

# Compile manager
meson compile -C builddir-manager

# If successful, setup and compile worker
meson setup builddir-worker --buildtype=debug
meson compile -C builddir-worker
```

### Step 4: Verify Shared Library Output

After successful build, check what library files were created:

```bash
# Find libzt dist directory
find subprojects/libzt-wrapper/libzt/dist -name "*.so*" -o -name "*.a" 2>/dev/null

# Expected output should include:
# - libzt.so (shared library)
# - Possibly libzt.a (static library, but we won't use it)
```

Check the actual library path used:
```bash
# Look in the native symlink/directory
ls -lh subprojects/libzt-wrapper/libzt/dist/native/lib/
```

### Step 5: Verify Linking

Check that executables are linked against the shared library:

```bash
# Check manager executable dependencies
ldd builddir-manager/manager/manager | grep libzt

# Check worker executable dependencies  
ldd builddir-worker/worker/worker | grep libzt

# Check RPATH configuration
readelf -d builddir-manager/manager/manager | grep -E "RPATH|RUNPATH"
```

**Expected Results**:
- `ldd` should show `libzt.so => <path>` (shared library)
- RPATH/RUNPATH should include `$ORIGIN/../lib`

### Step 6: Runtime Test

Test that executables can load the shared library:

```bash
# Copy shared library to a location relative to executable
mkdir -p builddir-manager/lib
cp subprojects/libzt-wrapper/libzt/dist/native/lib/libzt.so builddir-manager/lib/

# Try running manager with --help (should not crash on library load)
cd builddir-manager
./manager/manager --help
cd ..
```

## Expected Differences: Windows vs Linux

| Aspect | Windows | Linux |
|--------|---------|-------|
| Library file | `libzt.dll` | `libzt.so` |
| Import library | `libzt-shared.lib` (for linking) | Not needed (same .so) |
| Build script | `build-libzt.ps1` (wrapper) | `build.sh` (inside libzt repo) |
| Link-time | Uses import .lib | Directly links .so |
| Runtime path | Same dir or PATH | RPATH: `$ORIGIN/../lib` |
| Verification | dumpbin (if available) | ldd, readelf |

## Troubleshooting

### Issue: "library 'libzt-shared' not found"

**Cause**: Build script didn't create the expected library file.

**Solution**: Check what files were actually created:
```bash
find subprojects/libzt-wrapper/libzt -name "libzt*" -type f
```

If you see `libzt.so` but not `libzt-shared.so`, the meson.build may need adjustment. Check the actual library name produced by CMake:
```bash
cat subprojects/libzt-wrapper/libzt/CMakeLists.txt | grep -A 5 "DYNAMIC_LIB"
```

### Issue: "cannot find -llibzt-shared"

**Cause**: The library name doesn't match what's being searched for.

**Solution**: Update meson.build to find the correct library name. On Linux, if libzt's CMake produces `libzt.so`, then search for 'zt' not 'libzt-shared':

```meson
# Try finding 'zt' instead of 'libzt-shared'
zt_lib = cc.find_library('zt', dirs: [libzt_lib_path], required: true, static: false)
```

### Issue: Runtime "libzt.so not found"

**Cause**: Shared library not in expected location.

**Solution**: 
1. Verify RPATH: `readelf -d ./manager/manager | grep RPATH`
2. Copy library to expected location based on RPATH
3. Or set LD_LIBRARY_PATH temporarily: `export LD_LIBRARY_PATH=../lib:$LD_LIBRARY_PATH`

## Success Criteria

✅ Meson setup completes without errors  
✅ libzt builds as shared library (`.so` file created)  
✅ Manager and worker compile and link successfully  
✅ `ldd` shows `libzt.so` as a dependency  
✅ RPATH is configured correctly (`$ORIGIN/../lib`)  
✅ Executables can be run (at minimum, `--help` works)  

## Key Files to Check

1. **subprojects/libzt-wrapper/meson.build** (lines 130-165)
   - Should find 'libzt-shared' or 'zt' with `static: false`
   - Should have RPATH configuration for Linux

2. **subprojects/libzt-wrapper/libzt/dist/native/lib/**
   - Should contain `libzt.so` or similar shared library

3. **builddir-manager/build.ninja** 
   - Search for "manager.exe" or "manager:" and check LINK_ARGS
   - Should reference the shared library path

## Report Back

After completing the test, report:

1. ✅ or ❌ Build success
2. Library files created (ls output from dist/native/lib/)
3. ldd output for manager and worker
4. readelf RPATH output
5. Any errors encountered
6. Any modifications needed for Linux (if different from Windows changes)

## Next Steps After Success

If Phase 1 works on Linux:
- Document any Linux-specific quirks
- Consider whether the library name search needs to be platform-specific
- Proceed to Phase 2: Distribution Structure Implementation

## Additional Context

### Project Structure
```
task-messenger/
├── manager/          - Manager component source
├── worker/           - Worker component source  
├── message/          - Shared message definitions
├── transport/        - Network transport layer
├── subprojects/
│   ├── libzt-wrapper/  - ZeroTier library wrapper
│   ├── CLI11/          - Command-line parsing
│   ├── ftxui/          - Terminal UI (optional)
│   └── shared/         - Common utilities
├── builddir-manager/ - Manager build output
└── builddir-worker/  - Worker build output
```

### Dependencies
- C++20 compiler (GCC 10+ or Clang 11+)
- Meson build system
- CMake (for libzt)
- Git (for submodule management)
- Python 3 (for patch scripts)

### Build Types
- `debug` - Debug symbols, no optimization
- `debugoptimized` - Debug symbols with optimization  
- `release` - Full optimization, no debug symbols
