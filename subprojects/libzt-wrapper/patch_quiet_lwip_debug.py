#!/usr/bin/env python3
import os
import re
import sys

# This script tweaks the vendored libzt CMakeLists to suppress SOCKETS_DEBUG prints
# in debug builds by removing -DSOCKETS_DEBUG=128 and setting LWIP_DBG_TYPES_ON=0,
# while preserving -DLWIP_DEBUG=1 for debug asserts if desired. It's idempotent.

def patch_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        s = f.read()

    orig = s
    # Neutralize SOCKETS_DEBUG=128 definitions
    s = re.sub(r"^\s*set\(LWIP_FLAGS\s+\"\$\{LWIP_FLAGS\}[^\n]*-DSOCKETS_DEBUG=128\"\)\s*$",
               lambda m: '# ' + m.group(0) + ' (disabled by Meson libzt_quiet_lwip_debug)\n', s, flags=re.MULTILINE)
    # Force LWIP_DBG_TYPES_ON=0 in debug blocks by annotating and replacing
    s = re.sub(r"set\(LWIP_FLAGS\s+\"\$\{LWIP_FLAGS\}\s+-DLWIP_DBG_TYPES_ON=128\"\)",
               "set(LWIP_FLAGS \"${LWIP_FLAGS} -DLWIP_DBG_TYPES_ON=0\")  # overridden by Meson libzt_quiet_lwip_debug",
               s)

    if s != orig:
        with open(path, 'w', encoding='utf-8') as f:
            f.write(s)
        print(f"Patched {path} to quiet lwIP debug")
    else:
        print(f"No changes needed for {path}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: patch_quiet_lwip_debug.py <libzt_dir>")
        sys.exit(2)
    libzt_dir = sys.argv[1]
    cmake = os.path.join(libzt_dir, 'CMakeLists.txt')
    if not os.path.exists(cmake):
        print(f"CMakeLists not found: {cmake}")
        sys.exit(1)
    patch_file(cmake)
