#!/usr/bin/env python3
"""Apply local patches to the vendored libzt source tree.

Patch (simplified per request):
        Replace any line declaring or defining the global zts_errno variable with '#include "zts_errno.hpp"'.
        Applied to:
            * src/Sockets.cpp
            * ext/lwip/src/api/sockets.c
            * include/ZeroTierSockets.h
        Handles both:
            extern int zts_errno;                (with qualifiers, comments, extern "C")
            int zts_errno; / int zts_errno = 0;  (with optional initialization & comments)

Notes:
    * We do NOT try to position the include near other includes; we just inline-replace the line.
    * Idempotent: once replaced, the original patterns no longer exist so further runs make no changes.
"""
from __future__ import annotations
import sys
import pathlib
import re

ROOT = pathlib.Path(__file__).parent
LIBZT_DIR = ROOT / 'libzt'
SRC_SOCKETS = LIBZT_DIR / 'src' / 'Sockets.cpp'
LWIP_SOCKETS_C = LIBZT_DIR / 'ext' / 'lwip' / 'src' / 'api' / 'sockets.c'
PUBLIC_HEADER = LIBZT_DIR / 'include' / 'ZeroTierSockets.h'
# Also patch the Rust crate's vendored copy to avoid drift when searching globally
CRATE_HEADER = LIBZT_DIR / 'pkg' / 'crate' / 'libzt' / 'src' / 'include' / 'ZeroTierSockets.h'
ERRNO_HEADER_INCLUDE = '#include "zts_errno.hpp"'

# Match variant extern declarations.
EXTERN_PATTERN = re.compile(
    r'^\s*extern'                  # leading extern
    r'(?:\s+"C")?'               # optional "C"
    r'(?:\s+\w+)*'                # optional qualifiers (const, volatile, etc.)
    r'\s+int\s+zts_errno\s*;'     # variable name
    r'\s*(?://.*|/\*.*?\*/)?\s*$',  # optional trailing comment
    re.MULTILINE
)

# Match plain definitions (with optional initialization & qualifiers omitted by design).
DEFINITION_PATTERN = re.compile(
    r'^\s*(?:int)\s+zts_errno'    # base type and name
    r'(?:\s*=\s*[^;]+)?'          # optional initialization
    r'\s*;'                        # semicolon
    r'\s*(?://.*|/\*.*?\*/)?\s*$',  # optional comment
    re.MULTILINE
)


def patch_replace_extern(file: pathlib.Path) -> bool:
    """Replace lines declaring 'extern int zts_errno;' with the include directive."""
    if not file.exists():
        return False
    original = file.read_text(encoding='utf-8')
    # First replace any extern declarations, then plain definitions.
    text = EXTERN_PATTERN.sub(ERRNO_HEADER_INCLUDE, original)
    if text == original:  # Only attempt second pattern if first made no change on that line
        text = DEFINITION_PATTERN.sub(ERRNO_HEADER_INCLUDE, text)
    if text != original:
        file.write_text(text, encoding='utf-8')
        return True
    return False


def main():
    if not LIBZT_DIR.exists():
        print("libzt directory not present; nothing to patch yet", file=sys.stderr)
        return 0
    sockets_changed = patch_replace_extern(SRC_SOCKETS)
    lwip_changed = patch_replace_extern(LWIP_SOCKETS_C)
    header_changed = patch_replace_extern(PUBLIC_HEADER)
    crate_header_changed = patch_replace_extern(CRATE_HEADER)
    if any([sockets_changed, lwip_changed, header_changed, crate_header_changed]):
        print(
            "Applied zts_errno replacement "
            f"(Sockets.cpp={sockets_changed} "
            f"sockets.c={lwip_changed} "
            f"include/ZeroTierSockets.h={header_changed} "
            f"pkg/crate/libzt/src/include/ZeroTierSockets.h={crate_header_changed})"
        )
    else:
        print("zts_errno replacement already applied (no changes)")
    return 0

if __name__ == '__main__':
    sys.exit(main())
