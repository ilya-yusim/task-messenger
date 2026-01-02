# Windows Elapsed-Time Profiling ("perf" Analogue)

Most of your wall‑clock time is inactive (waiting) rather than running CPU. Tools focused only on CPU hotspots (like AMDuProf) miss where threads actually spend elapsed time. On Windows, the closest equivalent to Linux `perf` is the Event Tracing for Windows (ETW) ecosystem.

## Core Tools

| Tool | Purpose | When to Use |
|------|---------|-------------|
| **WPR + WPA** (Windows Performance Recorder / Analyzer) | Rich ETW trace collection & analysis (CPU, waits, I/O, memory, stacks) | Primary workflow for native+managed, wait vs run time breakdown |
| **xperf** (part of Windows Performance Toolkit) | Low-level CLI similar to `perf record` | Precise control of providers & stack walking; scripting |
| **PerfView** | Lightweight viewer; great "Thread Time" vs "CPU Time" comparison | Quick identification of blocking vs running stacks |
| **UIforETW** | One-click ETW capture presets | Convenience / fast start |
| **Intel VTune** (works on AMD for many features) | Advanced concurrency, memory, lock contention, micro-architectural counters | Deep dives (locks, cache, bandwidth). Some HW events limited on AMD |
| **AMDuProf** | Hardware counters (cycles, IPC) | Supplement when micro-arch metrics needed |

> Emerging: eBPF for Windows exists but not yet a full replacement for perf in general elapsed-time analysis.

## What to Capture (Minimum Useful Set)
- Sampled CPU profile events (PROFILE / SampledProfile) – running stacks
- Context switches (CSWITCH) + ReadyThread – run vs wait intervals, wake attribution
- Process & thread lifecycle (PROC_THREAD)
- Loader (LOADER) – module mapping for symbol resolution
- Optional: Disk/File I/O, TCP/IP, Heap, GC (if managed), PMC counters

## Quick Start: WPR
```powershell
# Elevated PowerShell recommended
wpr -start GeneralProfile.Light -filemode
# Run workload for N seconds
Start-Sleep -Seconds 30
wpr -stop trace.etl
```
Open `trace.etl` in WPA:
- CPU Usage (Sampled) – hotspots
- Thread Activity / Wait Analysis – blocked vs running intervals with wait types
- Add columns (Wait Type, Blocked Duration, Ready Time) & stack views

## Low-Level (perf-like) via xperf
```powershell
xperf -on PROC_THREAD+LOADER+PROFILE+CSWITCH+READYTHREAD \
      -stackwalk Profile+CSwitch+ReadyThread -buffersize 1024 -FileMode
Start-Sleep -Seconds 30
xperf -stop -d baseline.etl
```
Analyze in WPA or:
```powershell
xperf -i baseline.etl -a cpuprofiler > cpuprofiler.txt
```

## PerfView Thread Time
```powershell
PerfView collect -KernelEvents=Default+CSwitch -CircularMB=400 -MaxCollectSec=30 -NoNGenPdbs threadtime.etl.zip
```
Open in PerfView:
- "CPU Stacks" = actual running time
- "Thread Time Stacks" = elapsed time including waits (difference surfaces blocking)

## Flamegraphs on Windows

WPA cannot render classic flamegraphs directly. Use one of the following:

### 1) PerfView Flame Graph view (fastest)
1. Capture a trace (WPR/xperf/PerfView) and open it in PerfView.
2. Open either:
      - CPU Stacks (for running CPU time), or
      - Thread Time Stacks (for wall/elapsed time; requires CSWITCH enabled as shown above).
3. From the stacks view, open the Flame Graph visualization (available in recent PerfView versions via the view menu).
4. Hover to inspect frames and click to zoom; use “Back” to navigate up the stack.

Tip: Thread Time flamegraphs help when elapsed time is dominated by waits; CPU flamegraphs highlight compute hotspots.

### 2) Export folded stacks and use FlameGraph.pl
1. In PerfView’s stack view, use Save/Export to produce “Folded (Collapsed) Stacks”.
2. Generate an SVG with Brendan Gregg’s FlameGraph tools:
      ```powershell
      # Requires Perl and FlameGraph scripts in PATH
      flamegraph.pl collapsed.txt > flame.svg
      ```
3. Open `flame.svg` in a browser.

### 3) Speedscope (browser-based)
- Export stacks (collapsed) from PerfView and upload to https://www.speedscope.app for interactive flame/sandwich views.

### Visual Studio option
If you profile with Visual Studio Performance Profiler, its CPU Usage tool includes a flame graph view out of the box (CPU time focused).

## Interpreting Elapsed vs CPU
- Running time = between context switch in/out or sampled stacks
- Waiting time = intervals thread is unscheduled; attributed to blocking API (WaitForSingleObject, socket, I/O, lock)
- Use Wait Analysis (WPA) or Thread Time Stacks (PerfView) to classify: Synchronization, I/O, Sleep, Network

### Common Sources of Inactivity
1. Lock contention (CriticalSection, SRWLock, Mutex)
2. Network latency (TCP Receive, Connect)
3. File / disk I/O
4. Thread pool starvation / timers (Sleep)
5. External services (RPC waits)

## Hardware Counters (PMC)
- WPR custom profiles or VTune can gather cycles, branches, cache references.
- On AMD, VTune supports many generic events; some Intel-specific events unavailable.
- Use HW counters only after identifying logical waits; they refine root cause (e.g., low IPC inside critical section).

## Symbols Setup
Set symbol path before analysis:
```powershell
$env:_NT_SYMBOL_PATH = "srv*C:\symbols*https://msdl.microsoft.com/download/symbols"
```
Ensure your build produces PDBs alongside binaries. WPA & PerfView auto-load them.

## Minimal Combined Workflow
```powershell
# 1. Baseline ETW trace (elapsed vs CPU)
xperf -on PROC_THREAD+LOADER+PROFILE+CSWITCH+READYTHREAD -stackwalk Profile+CSwitch+ReadyThread -buffersize 1024 -FileMode
Start-Sleep -Seconds 30
xperf -stop -d baseline.etl

# 2. Thread time via PerfView
PerfView collect -KernelEvents=Default+CSwitch -CircularMB=400 -MaxCollectSec=30 -NoNGenPdbs threadtime.etl.zip
```
Compare WPA Wait Analysis with PerfView Thread Time Stacks.

## Refinement Steps
| Scenario | Action |
|----------|--------|
| Unknown wait type | Enable additional ETW providers (FileIO, TCPIP) |
| Lock contention suspected | Add Contention ETW provider; use VTune Locks & Waits |
| High network latency | Correlate TCP Receive / Connect events timeline |
| Thread pool starvation | Inspect ReadyThread & queue depths; instrument enqueue/dequeue |
| Intermittent spikes | Increase trace duration or use circular buffer mode |

## Pitfalls & Remedies
| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Not elevated | Missing context switch stacks | Run admin PowerShell |
| Missing symbols | Raw addresses in stacks | Set `_NT_SYMBOL_PATH`, ensure PDBs |
| Over-instrumentation | High overhead, perturbed timings | Start lean; add providers iteratively |
| Relying only on CPU samples | "Idle" time unexplained | Include CSWITCH, ReadyThread |
| Heavy I/O provider stacks | Large ETL, slow analysis | Filter time range; narrow providers |

## When ETW Isn’t Enough
- Add high-resolution custom event instrumentation (TraceLogging) around critical waits.
- Use Wait Chain Traversal API for deadlock chain inspection.
- For cross-process correlations, include Activity IDs / correlation IDs in events.

## WSL Option
You can run the workload inside WSL2 and use Linux `perf`, but WSL changes kernel behavior for waits; prefer native ETW for Windows wait/lock insight.

## Example: Focusing on Blocking Elapsed Time
1. Capture baseline trace (xperf command above).
2. In WPA: open baseline.etl -> Graph Explorer -> Thread Activity -> Filter to long Blocked Duration > threshold.
3. Right-click a heavy wait -> View Stacks -> Identify top frame initiating wait.
4. Cross-reference with PerfView Thread Time Stacks to confirm call path.
5. Optimize (reduce contention, pipeline I/O, async overlapped operations).

## Summary
Use ETW-based workflows (WPR/WPA or xperf + WPA) to obtain both CPU and wait attribution, then PerfView for quick Thread Time comparison. Augment with VTune for advanced lock/memory analysis and AMDuProf for raw CPU counters. Symbols and minimal provider selection are critical for actionable elapsed-time profiling.

## Next Steps
- Automate trace collection in CI for performance regressions.
- Add lightweight TraceLogging events around suspected blocking regions.
- Introduce contention metrics (counts, average hold times) into periodic telemetry.

---
*This guide summarizes a Windows approach analogous to Linux `perf` for diagnosing wall-clock (elapsed) time dominated by inactivity.*
