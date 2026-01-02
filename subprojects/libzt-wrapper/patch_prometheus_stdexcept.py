#!/usr/bin/env python3
"""
Ensure prometheus-cpp-lite headers include <stdexcept> for std::invalid_argument.
This is idempotent and only touches the two headers if the include is missing.
"""
import sys
import pathlib

def ensure_include(path: pathlib.Path) -> bool:
    if not path.exists():
        return False
    text = path.read_text(encoding='utf-8')
    if '#include <stdexcept>' in text:
        return False
    lines = text.splitlines()
    out = []
    inserted = False
    for i, line in enumerate(lines):
        out.append(line)
        # Insert right after the first '#include <cassert>' if present, else after first block of includes
        if not inserted and line.strip() == '#include <cassert>':
            out.append('#include <stdexcept>')
            inserted = True
    if not inserted:
        # fallback: insert after last leading include
        out = []
        inserted = False
        inserted_line = None
        for i, line in enumerate(lines):
            out.append(line)
            if not inserted and line.strip().startswith('#include '):
                inserted_line = i
            if inserted_line is not None and not line.strip().startswith('#include '):
                # at first non-include after some includes
                out.insert(len(out)-1, '#include <stdexcept>')
                inserted = True
                inserted_line = None
        if not inserted:
            # if file has no includes, put at top after pragma once if present
            out = []
            first = True
            for line in lines:
                if first and line.strip().startswith('#pragma once'):
                    out.append(line)
                    out.append('#include <stdexcept>')
                    first = False
                else:
                    out.append(line)
            inserted = True
    path.write_text('\n'.join(out) + ('\n' if not out[-1].endswith('\n') else ''), encoding='utf-8')
    return True


def main() -> int:
    root = pathlib.Path(__file__).parent
    libzt = root / 'libzt'
    family = libzt / 'ext' / 'ZeroTierOne' / 'ext' / 'prometheus-cpp-lite-1.0' / 'core' / 'include' / 'prometheus' / 'family.h'
    registry = libzt / 'ext' / 'ZeroTierOne' / 'ext' / 'prometheus-cpp-lite-1.0' / 'core' / 'include' / 'prometheus' / 'registry.h'
    changed = False
    changed |= ensure_include(family)
    changed |= ensure_include(registry)
    print(f"prometheus headers updated: {changed}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
