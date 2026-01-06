# Phase 1 Complete: Linux Shared Library Implementation

## Summary

Phase 1 (converting libzt from static to shared library) has been successfully completed and tested on **Linux**. The implementation is now working on both Windows and Linux platforms.

## What Was Done

### 1. Platform-Specific Library Naming

The key modification made was adding platform-specific library naming in the libzt-wrapper meson.build file to handle the difference between Windows and Linux library conventions.

**File Modified**: `subprojects/libzt-wrapper/meson.build` (lines 140-147)

**Change**:
```meson
# Step 4: Find the built libzt shared library
libzt_lib_path = join_paths(libzt_dir, 'dist', 'native', 'lib')
# On Windows, look for libzt-shared.lib (import library for libzt.dll)
# On Linux, look for libzt.so (just 'zt' after removing 'lib' prefix)
if host_machine.system() == 'windows'
  zt_lib = cc.find_library('libzt-shared', dirs: [libzt_lib_path], required: true, static: false)
else
  zt_lib = cc.find_library('zt', dirs: [libzt_lib_path], required: true, static: false)
endif
```

**Reason**: 
- Windows libzt build produces `libzt-shared.lib` (import library) for linking
- Linux libzt build produces `libzt.so` which Meson searches for as `zt`

### 2. Verified Existing Configuration

The following were already present from the Windows implementation:
- ✅ RPATH configuration for Linux (`$ORIGIN/../lib`)
- ✅ Shared library preference (`static: false`)
- ✅ Build script delegation (libzt's own `build.sh`)

## Test Results

### Build Success

```bash
# Clean build from scratch
rm -rf builddir-manager builddir-worker
meson setup builddir-manager --buildtype=debug   # SUCCESS
meson compile -C builddir-manager                 # SUCCESS
meson setup builddir-worker --buildtype=debug     # SUCCESS
meson compile -C builddir-worker                  # SUCCESS
```

### Library Output

```
subprojects/libzt-wrapper/libzt/dist/native/lib/
├── libzt.so    (17M) - Shared library ✅
└── libzt.a     (53M) - Static library (not used)
```

### Linking Verification

```bash
$ ldd builddir-manager/manager/manager | grep libzt
libzt.so => /workspaces/task-messenger/builddir-manager/manager/../../subprojects/libzt-wrapper/libzt/dist/native/lib/libzt.so

$ ldd builddir-worker/worker/worker | grep libzt
libzt.so => /workspaces/task-messenger/builddir-worker/worker/../../subprojects/libzt-wrapper/libzt/dist/native/lib/libzt.so
```

Both executables correctly link against the **shared library** (not static).

### RUNPATH Configuration

```bash
$ readelf -d builddir-manager/manager/manager | grep RUNPATH
Library runpath: [$ORIGIN/../../subprojects/libzt-wrapper/libzt/dist/native/lib:$ORIGIN/../lib]

$ readelf -d builddir-worker/worker/worker | grep RUNPATH
Library runpath: [$ORIGIN/../../subprojects/libzt-wrapper/libzt/dist/native/lib:$ORIGIN/../lib]
```

The RUNPATH includes:
1. **Build-time path**: `$ORIGIN/../../subprojects/libzt-wrapper/libzt/dist/native/lib`
2. **Distribution path**: `$ORIGIN/../lib` (for Phase 2)

### Runtime Test

```bash
$ builddir-manager/manager/manager --help
[SUCCESS - displays help output]

$ builddir-worker/worker/worker --help
[SUCCESS - displays help output]
```

Both executables load the shared library and run successfully.

## Platform Comparison

| Aspect | Windows | Linux |
|--------|---------|-------|
| **Shared Library File** | `libzt.dll` | `libzt.so` |
| **Import/Link Library** | `libzt-shared.lib` | Same as shared (libzt.so) |
| **find_library() name** | `libzt-shared` | `zt` |
| **Build Script** | `build-libzt.ps1` (wrapper) | `build.sh` (inside libzt) |
| **Runtime Library Path** | Same dir or PATH | RUNPATH: `$ORIGIN/../lib` |
| **Size (approx)** | ~12MB DLL | ~17MB .so |
| **Verification Tools** | dumpbin /dependents | ldd, readelf |

## Current State

### Files Changed (Phase 1)

1. **subprojects/libzt-wrapper/meson.build** 
   - Added platform-specific library name detection
   - Already has RPATH configuration for Linux
   - Already has shared library preference

2. **subprojects/libzt-wrapper/build-libzt.ps1** (Windows only)
   - Already has DLL import library copying

3. **subprojects/libzt-wrapper/README.md** (Windows)
   - Already documents shared library approach

### What Works Now

✅ Clean builds on Linux from scratch  
✅ libzt builds as shared library (17MB .so)  
✅ Manager and Worker link dynamically  
✅ RUNPATH configured for distribution  
✅ Executables run successfully  
✅ Both platforms (Windows + Linux) working  

## Ready for Phase 2

Phase 1 is **complete and verified** on both platforms. The project is ready for Phase 2: Distribution Structure Implementation.

### Phase 2 Goals

The next phase will:
1. Create proper distribution directory structure
2. Implement install targets in Meson
3. Copy shared libraries to distribution locations
4. Package executables with dependencies
5. Test distribution packages

### Distribution Structure Target

```
dist/
├── bin/
│   ├── manager       (or manager.exe)
│   └── worker        (or worker.exe)
├── lib/
│   └── libzt.so      (or libzt.dll)
└── config/
    ├── config-manager.json
    └── config-worker.json
```

The RUNPATH is already configured to find `libzt.so` in `../lib` relative to the executables.

## Notes for Windows Development (Phase 2)

When working on Phase 2 in Windows:

1. **Library Location**: On Windows, DLLs can be in the same directory as the executable OR in `../lib`. Windows searches the executable directory first by default.

2. **RPATH Equivalent**: Windows doesn't use RPATH. Instead:
   - DLL search order: executable dir → system dirs → PATH
   - Consider setting PATH or using same-directory deployment

3. **Install Targets**: Create Meson install targets that:
   - Copy executables to `dist/bin/`
   - Copy `libzt.dll` (and `libzt-shared.lib` if needed for dev) to `dist/lib/`
   - Copy config files to `dist/config/`

4. **Testing**: After implementing install:
   ```bash
   meson install -C builddir-manager --destdir dist
   # Verify: dist/bin/manager.exe can find dist/lib/libzt.dll
   ```

5. **Platform Differences**:
   - Linux: Uses RUNPATH, already configured
   - Windows: May need to copy DLL to bin/ directory OR modify PATH

## Verification Commands (For Reference)

### Linux
```bash
# Check linking
ldd builddir-manager/manager/manager

# Check RPATH
readelf -d builddir-manager/manager/manager | grep RUNPATH

# Check library dependencies
objdump -p builddir-manager/manager/manager | grep NEEDED
```

### Windows
```bash
# Check linking (if dumpbin available)
dumpbin /dependents builddir-manager\manager\manager.exe

# Check DLL dependencies
ldd builddir-manager/manager/manager.exe  # If in Git Bash/MSYS2
```

## Git Status

The following changes have been made and should be committed:

```
modified:   subprojects/libzt-wrapper/meson.build
```

Consider committing this as:
```bash
git add subprojects/libzt-wrapper/meson.build
git commit -m "feat: Add platform-specific library naming for libzt shared library

- Windows: Use 'libzt-shared' (import library for libzt.dll)
- Linux: Use 'zt' (standard naming for libzt.so)
- Phase 1 complete: Both platforms now use shared library"
```

## Success Criteria Met

✅ **Build Success**: Clean builds work on Linux  
✅ **Shared Library**: libzt.so created and used  
✅ **Dynamic Linking**: ldd confirms shared library usage  
✅ **RPATH Configured**: $ORIGIN/../lib set for distribution  
✅ **Runtime Success**: Executables load library and run  
✅ **Cross-Platform**: Works on both Windows and Linux  

## Contact/Questions

If issues arise when switching back to Windows:
1. Verify libzt-shared.lib exists in dist/native/lib on Windows
2. Check that Windows build still produces libzt.dll
3. Ensure build-libzt.ps1 copies the import library correctly

Phase 1 is solid and ready for Phase 2 implementation! 🚀
