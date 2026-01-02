# Perf Quick Guide

This file contains common, practical `perf` commands for interactive use: quick live checks, how to start a process under `perf`, how to attach to a running process, how to capture call-graphs, and how to convert `perf` data into a FlameGraph.

> Tip: run `perf` with `sudo` on most systems (or grant the needed capabilities). Install `linux-tools-$(uname -r)` on Debian/Ubuntu if you see a kernel-tool mismatch warning.

# Installing perf
```bash
sudo apt update
sudo apt install -y linux-tools-$(uname -r) linux-tools-common
```

## Quick checks (live / lightweight)

- Live hot-functions (interactive):

```bash
sudo perf top
```

Shows a live, updating view of hottest symbols system-wide. Add `-p PID` to focus on a process:

```bash
sudo perf top -p <PID>
```

- High-level counters for a short run:

```bash
sudo perf stat -r 5 ./my_binary arg1 arg2
```

Gives CPU cycles, instructions, cache-misses, etc. `-r 5` repeats the run 5 times.

## Start a process under perf (recording)

- Simple sampling recording while running a program (records call stacks):

```bash
sudo perf record -F 99 -g -- ./my_binary arg1 arg2
```

Meaning:
- `-F 99` sample frequency ≈ 99Hz (adjust for resolution vs overhead)
- `-g` capture call stacks (frame-pointer or DWARF unwinding)
- `--` separates `perf` options from the program and its args

- Record with DWARF unwinding (better when frame-pointers were omitted by compiler):

```bash
sudo perf record -F 99 --call-graph dwarf -g -- ./my_binary
```

Notes:
- DWARF unwinding yields better stacks but needs debug info (`-g` at compile) and `perf` built with libunwind/dwarf support.
- If binary was built with `-fomit-frame-pointer`, DWARF is preferred.

## Attach to an existing (already running) process

- Attach to a PID and sample for a fixed time (e.g., 15s) and save to `perf.data`:

```bash
sudo perf record -F 99 -p <PID> -g -- sleep 15
```

- Variants:
  - `-p <PID>` attaches to that process (samples all its threads).
  - Use `-a` for system-wide profiling (`sudo perf record -a -g`).
  - To attach indefinitely, omit `-- sleep N` and later Ctrl-C to stop and write `perf.data`.

## Inspect collected data

- TUI (interactive):

```bash
sudo perf report
```

- Headless / plain-text (good for CI or to inspect in container):

```bash
sudo perf report --stdio -i perf.data
```

- Dump raw stacks (text) for further processing:

```bash
sudo perf script -i perf.data > perf.unfolded
```

## Make a FlameGraph (visual)

1. Convert `perf.data` to unfolded stacks:

```bash
sudo perf script -i /path/to/perf.data > /tmp/perf.unfolded
```

2. Collapse stacks and generate SVG (FlameGraph scripts by Brendan Gregg):

- If you have the FlameGraph scripts locally:

```bash
/path/to/stackcollapse-perf.pl /tmp/perf.unfolded > /tmp/perf.folded
/path/to/flamegraph.pl /tmp/perf.folded > /tmp/flame.svg
```

- If not, get them quickly:

```bash
git clone https://github.com/brendangregg/FlameGraph.git /tmp/FlameGraph
/tmp/FlameGraph/stackcollapse-perf.pl /tmp/perf.unfolded > /tmp/perf.folded
/tmp/FlameGraph/flamegraph.pl /tmp/perf.folded > /tmp/flame.svg
```

Open `/tmp/flame.svg` in a browser or VS Code.

## Good flags and refinement options

- `-F <freq>` vs `-c <event>`: set sampling frequency (`-F`) or sample specific hardware event (`-e cycles`). Example:

```bash
sudo perf record -F 200 -g -p <PID> -- sleep 10
# or sample user-space cycles only
sudo perf record -e cycles:u -F 99 -g -p <PID> -- sleep 10
```

- `--call-graph dwarf` for DWARF unwinding when frame pointers absent:

```bash
sudo perf record -F 99 --call-graph dwarf -p <PID> -- sleep 15
```

- To include kernel frames too, omit `:u` (or use `-e cycles`), but be careful: kernel + user stacks are larger.

## Troubleshooting & best practices

- Permission: `perf` often needs root or CAP_SYS_ADMIN. Use `sudo` (or grant capabilities).
- Kernel tools match: install `linux-tools-$(uname -r)` (Debian/Ubuntu) to avoid "kernel perf mismatch" warnings:

```bash
sudo apt update
sudo apt install linux-tools-$(uname -r)
```

- Build with debug info: compile with `-g` so symbols are readable. For better native callstacks, avoid `-fomit-frame-pointer` or use DWARF.
- Use reasonable sample frequency (50–200 Hz). Too high increases overhead and noise.
- Run with realistic load and long enough to capture representative behavior (10–30s is typical).
- If in containers / codespaces, you may need extra privileges or to run on the host; `perf` in constrained containers can fail to capture kernel symbols or be limited.
- `perf top -p PID` is great for quick, real-time exploration.
- If `perf script` shows lots of kernel frames or mangled C++ templates, use `--symfs` to point perf to an alternate sysroot or debug symbols.

## Quick example workflows

- Start-and-record a program for 20s:

```bash
sudo perf record -F 99 -g --timeout 20 -- ./build/myapp --config=config.json
# (alternative) perf record -F 99 -g -- ./build/myapp ...  # Ctrl-C to stop
sudo perf report --stdio -i perf.data
```

- Attach to a running PID for 15s and produce a flamegraph:

```bash
sudo perf record -F 99 -g -p 12345 -- sleep 15
sudo perf script -i perf.data > perf.unfolded
/tmp/FlameGraph/stackcollapse-perf.pl perf.unfolded > perf.folded
/tmp/FlameGraph/flamegraph.pl perf.folded > flame.svg
```

## When to use DWARF vs FP (frame-pointer)

- If your binaries were built with frame pointers preserved (no `-fomit-frame-pointer`), `-g` is usually enough:
  - `perf record -F 99 -g ...`
- If frame-pointers were omitted (optimized builds), use DWARF:
  - `perf record --call-graph dwarf -F 99 -g ...`
- DWARF needs extra debug info and increases overhead; test and compare.

---

If you want, I can:
- Re-run the same profiling steps for your repo (I already did one run) and show a short before/after comparison.
- Provide a one-liner script you can reuse to attach, convert, and produce a flamegraph.

Which would you like next?