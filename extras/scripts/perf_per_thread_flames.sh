#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-/tmp/perf_thread_svgs}"
PERF_DATA="${2:-/tmp/perf.data}"
FLAME_DIR="${3:-/tmp/FlameGraph}"

mkdir -p "$OUT_DIR"

if [ ! -f "$PERF_DATA" ]; then
  echo "perf data not found: $PERF_DATA" >&2
  exit 1
fi

# Ensure FlameGraph scripts
if [ ! -d "$FLAME_DIR" ]; then
  git clone https://github.com/brendangregg/FlameGraph.git "$FLAME_DIR"
fi

UNFOLD="$OUT_DIR/perf.unfolded"
FOLDED="$OUT_DIR/perf.folded"

echo "Generating unfolded stacks -> $UNFOLD"
sudo perf script -i "$PERF_DATA" > "$UNFOLD"

echo "Collapsing stacks -> $FOLDED"
"$FLAME_DIR/stackcollapse-perf.pl" "$UNFOLD" > "$FOLDED"

echo "Splitting unfolded stacks by numeric TID into $OUT_DIR/by_tid"
# start fresh for by-tid outputs to avoid leftover files from previous runs
rm -rf "$OUT_DIR/by_tid" "$OUT_DIR/svgs"
mkdir -p "$OUT_DIR/by_tid"


# Split perf script unfolded output into per-TID unfolded files while capturing
# the original thread label. Header lines from 'perf script' start without
# leading whitespace. We extract both the label (first token) and the numeric
# TID (second token) then use them to form filenames like
# "CoroIoContext-0_tid-42497.unfolded".

awk -v outdir="$OUT_DIR/by_tid" '
  # We maintain a mapping best_label[tid] -> chosen label for that tid.
  # Heuristic: prefer the first non-generic label we see; if the first is
  # generic like "thread-..." we allow replacement by a later label.
  /^[^ \t]/ {
    n = split($0, a, /[[:space:]]+/)
    label = (n >= 1 ? a[1] : "unknown")
    tid = (n >= 2 ? a[2] : "unknown")
    # sanitize raw label/tid
    gsub(/[^A-Za-z0-9_.-]/, "_", label)
    gsub(/[^A-Za-z0-9_.-]/, "_", tid)
    if (best_label[tid] == "" || best_label[tid] ~ /^thread/ || best_label[tid] == "unknown") {
      best_label[tid] = label
    }
    fname = outdir "/" best_label[tid] "_tid-" tid ".unfolded"
    print $0 >> fname
    cur = fname
    next
  }
  { print $0 >> cur }
  END {
    # print mapping summary to stderr for debugging
    for (k in best_label) {
      print k " -> " best_label[k] > "/dev/stderr"
    }
  }
' "$UNFOLD"

echo "Collapsing and rendering flamegraphs per-TID into $OUT_DIR/svgs"
mkdir -p "$OUT_DIR/svgs"
for u in "$OUT_DIR"/by_tid/*.unfolded; do
  tidname=$(basename "${u%.unfolded}")
  folded="$OUT_DIR/by_tid/${tidname}.folded"
  echo "Collapsing $u -> $folded"
  "${FLAME_DIR}/stackcollapse-perf.pl" "$u" > "$folded"
  # Output file keeps the label + tid that is already in tidname
  out="$OUT_DIR/svgs/${tidname}.svg"
  echo "Rendering $folded -> $out"
  "${FLAME_DIR}/flamegraph.pl" "$folded" > "$out"
  echo "Wrote $out"
done

echo "Done. Open SVGs in $OUT_DIR/svgs"