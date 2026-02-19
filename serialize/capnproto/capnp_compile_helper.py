#!/usr/bin/env python3
"""
Helper script to run the Cap'n Proto compiler and rename output files.
MSVC doesn't recognize .c++ extension, so we rename to .cpp.
"""
import subprocess
import sys
import os
import shutil
from pathlib import Path

def main():
    if len(sys.argv) < 6:
        print("Usage: capnp_compile_helper.py <capnp_tool> <capnpc_cpp> <include_dir> <src_prefix> <output_dir> <input_schema>")
        sys.exit(1)
    
    capnp_tool = sys.argv[1]
    capnpc_cpp = sys.argv[2]
    include_dir = sys.argv[3]
    src_prefix = sys.argv[4]
    output_dir = sys.argv[5]
    input_schema = sys.argv[6]
    
    # Construct the capnp compile command
    # capnp compile -oc++:outdir -I/include --src-prefix=... schema.capnp
    cmd = [
        capnp_tool,
        'compile',
        f'-o{capnpc_cpp}:{output_dir}',
        f'-I{include_dir}',
        f'--src-prefix={src_prefix}',
        input_schema
    ]
    
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"Error running capnp compile:", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        sys.exit(result.returncode)
    
    # Find all .c++ files in output directory and rename to .cpp
    output_path = Path(output_dir)
    for cxx_file in output_path.glob("*.c++"):
        cpp_file = cxx_file.with_suffix('.cpp')
        print(f"Renaming {cxx_file} -> {cpp_file}")
        shutil.move(str(cxx_file), str(cpp_file))
    
    print("Done.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
