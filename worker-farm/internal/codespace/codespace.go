// Package codespace implements the Phase 3 remote backend for
// GitHub Codespaces. It mirrors what local.Manager does for forked
// children, but shells every lifecycle operation through
// `gh codespace ssh`. The remote side runs the same
// start_workers_local.sh / stop_workers_local.sh helpers that an
// operator would invoke by hand — embedded in this package and
// piped via stdin, so the codespace doesn't need a persistent copy
// of the scripts (only tm-worker itself, installed via
// /hosts/{id}/bootstrap).
//
// Liveness is observed by a single ssh per host every PollInterval:
// the controller batches `kill -0` for every alive PID it tracks on
// that host, parses the comma-separated alive list, and updates the
// registry. There is no persistent ssh / ControlMaster — each poll
// is a fresh `gh codespace ssh` exec. If polling latency hurts at
// scale, the plan documents a ControlMaster fallback.
package codespace

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	_ "embed"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/gh"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/inventory"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/registry"
)

// startScript / stopScript are the canonical local helpers from
// extras/scripts/, copied here so go:embed can pick them up. They
// are pure bash and behave identically on a codespace's Ubuntu as
// they do on a developer's Mac/Linux laptop.
//
//go:embed start_workers_local.sh
var startScript []byte

//go:embed stop_workers_local.sh
var stopScript []byte

// startEpilogue is appended to startScript before piping to remote
// bash. It surfaces the run dir + manifest the script just wrote,
// fenced by sentinel markers so the controller can pluck them out
// of the merged stdout/stderr without false positives from the
// script's own log output.
const startEpilogue = `
# === controller epilogue (added by worker-farm) ===
echo "===TM_FARM_RUN_DIR_BEGIN==="
echo "$run_dir"
echo "===TM_FARM_RUN_DIR_END==="
echo "===TM_FARM_MANIFEST_BEGIN==="
cat "$run_dir/manifest.json"
echo "===TM_FARM_MANIFEST_END==="
`

const (
	runDirBeginMarker  = "===TM_FARM_RUN_DIR_BEGIN==="
	runDirEndMarker    = "===TM_FARM_RUN_DIR_END==="
	manifestBeginMark  = "===TM_FARM_MANIFEST_BEGIN==="
	manifestEndMarker  = "===TM_FARM_MANIFEST_END==="
	defaultPollPeriod  = 5 * time.Second
	defaultGracePeriod = 10 * time.Second
	// Spawn / Stop are over a network and a remote codespace; give
	// the operator-facing call enough time to ride out a cold start.
	spawnTimeout = 4 * time.Minute
	stopTimeout  = 90 * time.Second
)

// SpawnResult mirrors local.SpawnResult so the API can return one
// uniform JSON shape regardless of the host backend.
type SpawnResult struct {
	ID    string `json:"id"`
	OK    bool   `json:"ok"`
	PID   int    `json:"pid,omitempty"`
	Error string `json:"error,omitempty"`
}

// remoteManifest is the subset of the on-codespace manifest.json the
// controller cares about. The bash helper writes more fields (e.g.
// hostname, os) which we mirror through verbatim — see captureManifest.
type remoteManifest struct {
	RunID     string                 `json:"run_id"`
	StartedAt string                 `json:"started_at"`
	Host      string                 `json:"host"`
	Hostname  string                 `json:"hostname"`
	OS        string                 `json:"os"`
	BaseDir   string                 `json:"base_dir"`
	WorkerBin string                 `json:"worker_bin"`
	Config    string                 `json:"config"`
	Args      []string               `json:"args"`
	Workers   []remoteManifestWorker `json:"workers"`
}

type remoteManifestWorker struct {
	ID      string `json:"id"`
	PID     int    `json:"pid"`
	Log     string `json:"log"`
	Pidfile string `json:"pidfile"`
}

// runState is per-run bookkeeping. Lives only in memory; the
// authoritative copy of the manifest is on the codespace under
// $XDG_CACHE_HOME/tm-worker-farm/runs/<run-id>/. We mirror it
// locally for offline inspection.
type runState struct {
	runID        string
	hostID       string
	csName       string // resolved codespace name (never empty post-spawn)
	remoteRunDir string
	workerIDs    []string
}

// Manager owns codespace runs the controller spawned. One instance
// services every codespace host in the inventory; per-host state is
// keyed off Worker.Host (the inventory id).
type Manager struct {
	reg          *registry.Registry
	inv          *inventory.Inventory
	cacheDir     string
	controllerID string
	gracePeriod  time.Duration
	pollPeriod   time.Duration

	mu            sync.Mutex
	runs          map[string]*runState // runID -> state
	stopRequested map[string]time.Time // workerID -> when the operator clicked Stop
}

// Options configures Manager.
type Options struct {
	Registry     *registry.Registry
	Inventory    *inventory.Inventory
	CacheDir     string
	ControllerID string
	// GracePeriod is honoured by per-worker Stop (remote
	// SIGTERM → sleep → SIGKILL). Default 10s.
	GracePeriod time.Duration
	// PollPeriod is the liveness poll cadence. Default 5s.
	PollPeriod time.Duration
}

// New builds a Manager. It does not start polling — call Run for
// that.
func New(opts Options) *Manager {
	gp := opts.GracePeriod
	if gp == 0 {
		gp = defaultGracePeriod
	}
	pp := opts.PollPeriod
	if pp == 0 {
		pp = defaultPollPeriod
	}
	return &Manager{
		reg:           opts.Registry,
		inv:           opts.Inventory,
		cacheDir:      opts.CacheDir,
		controllerID:  opts.ControllerID,
		gracePeriod:   gp,
		pollPeriod:    pp,
		runs:          map[string]*runState{},
		stopRequested: map[string]time.Time{},
	}
}

// Run blocks until ctx is done, polling remote workers for liveness
// at PollPeriod. Safe to invoke as a goroutine; cancel ctx to stop.
func (m *Manager) Run(ctx context.Context) {
	t := time.NewTicker(m.pollPeriod)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			m.pollOnce(ctx)
		}
	}
}

// Spawn starts `count` workers on `host` via gh codespace ssh and
// records each in the registry. Returned slice is in slot order.
func (m *Manager) Spawn(ctx context.Context, host inventory.Host, count int, extraArgs []string) []SpawnResult {
	if count <= 0 {
		return nil
	}
	results := make([]SpawnResult, count)
	failAll := func(msg string) []SpawnResult {
		for i := range results {
			results[i] = SpawnResult{ID: newWorkerID(), OK: false, Error: msg}
		}
		return results
	}

	if host.Backend != inventory.BackendCodespace || host.Codespace == nil {
		return failAll(fmt.Sprintf("host %q is not a codespace backend", host.ID))
	}

	// Resolve the codespace name. Empty name ⇒ pick first available.
	resolveCtx, cancelResolve := context.WithTimeout(ctx, 30*time.Second)
	cs, err := gh.Resolve(resolveCtx, host.Codespace.Name)
	cancelResolve()
	if err != nil {
		return failAll(fmt.Sprintf("resolve codespace: %v", err))
	}

	// Build the bash invocation. `bash -s -- -n COUNT [-b BIN] [-c CFG] -- ARG1 ARG2 ...`
	args := []string{"-n", strconv.Itoa(count)}
	// Worker binary: the script's repo-relative default
	// (`<repo>/builddir/worker/tm-worker`) is meaningless on a codespace
	// where we install via release, so we always pass an explicit `-b`.
	// `tm-worker` (bare command) resolves through PATH, hitting
	// `~/.local/bin/tm-worker` from install_tm_worker_release.sh.
	workerBin := host.Codespace.WorkerBin
	if workerBin == "" {
		workerBin = "tm-worker"
	}
	args = append(args, "-b", workerBin)
	// Same story for the config: the script's repo-relative default
	// won't exist on the codespace. The .run installer drops the
	// canonical config at `~/.config/task-messenger/tm-worker/config-worker.json`
	// (see extras/scripts/start_workers_codespace.ps1 for the same
	// default in the legacy driver).
	cfgPath := host.Codespace.Config
	if cfgPath == "" {
		cfgPath = "~/.config/task-messenger/tm-worker/config-worker.json"
	}
	args = append(args, "-c", cfgPath)
	if len(extraArgs) > 0 {
		args = append(args, "--")
		args = append(args, extraArgs...)
	}

	script := string(startScript) + startEpilogue
	sshCtx, cancelSSH := context.WithTimeout(ctx, spawnTimeout)
	defer cancelSSH()
	out, err := gh.SSH(sshCtx, cs.Name, script, args...)
	if err != nil {
		log.Printf("codespace spawn host=%s cs=%s count=%d: ssh failed: %v\n--- output ---\n%s",
			host.ID, cs.Name, count, err, string(out))
		return failAll(fmt.Sprintf("ssh: %v", err))
	}

	runDir, manifestRaw, perr := parseSpawnOutput(out)
	if perr != nil {
		log.Printf("codespace spawn host=%s cs=%s: parse output: %v\n--- output ---\n%s",
			host.ID, cs.Name, perr, string(out))
		return failAll(fmt.Sprintf("parse remote manifest: %v", perr))
	}
	var rm remoteManifest
	if err := json.Unmarshal(manifestRaw, &rm); err != nil {
		return failAll(fmt.Sprintf("decode manifest: %v", err))
	}
	if len(rm.Workers) == 0 {
		return failAll("remote manifest reported zero workers")
	}

	// Mirror the manifest locally for offline inspection. Best
	// effort — a write failure here doesn't undo the spawn.
	m.mirrorManifest(host.ID, rm.RunID, manifestRaw)

	// Register one row per worker. Slot ordering matches the order
	// in the remote manifest, which start_workers_local.sh emits in
	// 1..count order.
	state := &runState{
		runID:        rm.RunID,
		hostID:       host.ID,
		csName:       cs.Name,
		remoteRunDir: runDir,
		workerIDs:    make([]string, 0, len(rm.Workers)),
	}
	startedAt := time.Now().UTC()
	baseArgs := append([]string{}, rm.Args...)
	for i, rw := range rm.Workers {
		slot := i + 1
		id := newWorkerID()
		w := &registry.Worker{
			ID:        id,
			PID:       rw.PID,
			State:     registry.StateRunning,
			RunID:     rm.RunID,
			Slot:      slot,
			StartedAt: startedAt,
			Args:      append([]string{}, baseArgs...),
			LogPath:   rw.Log,
			Host:      host.ID,
		}
		m.reg.Add(w)
		state.workerIDs = append(state.workerIDs, id)
		results[i] = SpawnResult{ID: id, OK: true, PID: rw.PID}
	}
	m.mu.Lock()
	m.runs[rm.RunID] = state
	m.mu.Unlock()

	log.Printf("codespace spawn host=%s cs=%s run=%s count=%d remote_dir=%s",
		host.ID, cs.Name, rm.RunID, len(rm.Workers), runDir)
	return results
}

// Stop sends SIGTERM to the worker's remote PID and schedules a
// background SIGKILL after gracePeriod, all in one ssh call. Returns
// when the ssh exits (which is well before the grace timer
// completes; the stub is `nohup`'d on the remote so it survives our
// session ending).
func (m *Manager) Stop(ctx context.Context, workerID string) error {
	w, ok := m.reg.Get(workerID)
	if !ok {
		return fmt.Errorf("unknown worker %q", workerID)
	}
	if w.State == registry.StateExited {
		return nil
	}
	st := m.runForWorker(workerID)
	if st == nil {
		return fmt.Errorf("worker %s: no codespace run state (controller restart? not yet supported for codespace)", workerID)
	}

	m.reg.Update(workerID, func(w *registry.Worker) {
		w.State = registry.StateStopping
	})
	m.mu.Lock()
	m.stopRequested[workerID] = time.Now().UTC()
	m.mu.Unlock()

	// Single-shot kill+timer on the remote so the grace fallback
	// survives our ssh tearing down.
	grace := int(m.gracePeriod.Seconds())
	if grace < 1 {
		grace = 1
	}
	script := fmt.Sprintf(`
set -u
PID=%d
GRACE=%d
kill -TERM "$PID" 2>/dev/null || true
nohup bash -c '
  for i in $(seq 1 %d); do
    sleep 1
    kill -0 '"$PID"' 2>/dev/null || exit 0
  done
  kill -KILL '"$PID"' 2>/dev/null || true
' >/dev/null 2>&1 &
disown 2>/dev/null || true
echo "stop submitted pid=$PID grace=${GRACE}s"
`, w.PID, grace, grace)

	sshCtx, cancel := context.WithTimeout(ctx, stopTimeout)
	defer cancel()
	if _, err := gh.SSH(sshCtx, st.csName, script); err != nil {
		return fmt.Errorf("ssh stop: %w", err)
	}
	return nil
}

// StopAll stops every codespace-backed worker the manager knows
// about. Errors are logged; never propagated, matching local.Manager
// semantics.
func (m *Manager) StopAll(ctx context.Context) {
	ids := m.runningWorkerIDs()
	if len(ids) == 0 {
		return
	}
	log.Printf("codespace stop-all: %d worker(s)", len(ids))
	var wg sync.WaitGroup
	for _, id := range ids {
		wg.Add(1)
		go func(id string) {
			defer wg.Done()
			if err := m.Stop(ctx, id); err != nil {
				log.Printf("codespace stop %s: %v", id, err)
			}
		}(id)
	}
	wg.Wait()
}

// TailLog returns the last `lines` lines of the worker's remote log
// file via `tail -n N`. lines<=0 ⇒ entire file.
func (m *Manager) TailLog(ctx context.Context, workerID string, lines int) ([]byte, error) {
	w, ok := m.reg.Get(workerID)
	if !ok {
		return nil, fmt.Errorf("unknown worker %q", workerID)
	}
	st := m.runForWorker(workerID)
	if st == nil {
		return nil, fmt.Errorf("worker %s: no run state", workerID)
	}
	if w.LogPath == "" {
		return nil, fmt.Errorf("worker %s has no log path", workerID)
	}
	tailArg := "-n +1"
	if lines > 0 {
		tailArg = fmt.Sprintf("-n %d", lines)
	}
	// Quote the log path defensively; codespace home paths are
	// well-behaved but tail's argument should still be opaque to the
	// shell.
	script := fmt.Sprintf("tail %s -- %q", tailArg, w.LogPath)
	sshCtx, cancel := context.WithTimeout(ctx, 30*time.Second)
	defer cancel()
	out, err := gh.SSH(sshCtx, st.csName, script)
	if err != nil {
		return out, err
	}
	return out, nil
}

// pollOnce groups every running codespace worker by host and runs a
// single ssh per host that emits the alive PIDs. Workers whose PIDs
// don't appear are marked exited.
func (m *Manager) pollOnce(ctx context.Context) {
	type watch struct {
		id    string
		pid   int
		state registry.State
	}
	byHost := map[string][]watch{}  // hostID -> watches
	csByHost := map[string]string{} // hostID -> last-known cs name
	for _, w := range m.reg.List() {
		if w.State != registry.StateRunning && w.State != registry.StateStopping {
			continue
		}
		st := m.runForWorker(w.ID)
		if st == nil {
			continue // not a codespace-backed worker we manage
		}
		byHost[w.Host] = append(byHost[w.Host], watch{id: w.ID, pid: w.PID, state: w.State})
		csByHost[w.Host] = st.csName
	}
	if len(byHost) == 0 {
		return
	}

	// Safety net: a worker that's been in `stopping` state longer
	// than (grace + slack) is force-marked exited even if SSH says
	// otherwise. Covers two real failure modes seen in practice:
	//   1. The codespace went idle after the stop request, so every
	//      subsequent poll's SSH returns an error.
	//   2. The remote `kill -0` race: the worker is in the middle of
	//      teardown but the kernel hasn't reaped it yet across two
	//      consecutive polls. Rare, but it would otherwise leave the
	//      UI permanently stuck on "stopping".
	stoppingDeadline := m.gracePeriod + 10*time.Second
	now := time.Now().UTC()

	for hostID, ws := range byHost {
		csName := csByHost[hostID]
		pids := make([]string, 0, len(ws))
		for _, w := range ws {
			pids = append(pids, strconv.Itoa(w.pid))
		}
		// One-liner: print each alive pid on its own line.
		script := fmt.Sprintf(`for pid in %s; do kill -0 "$pid" 2>/dev/null && echo "$pid"; done`,
			strings.Join(pids, " "))
		pollCtx, cancel := context.WithTimeout(ctx, 30*time.Second)
		out, err := gh.SSH(pollCtx, csName, script)
		cancel()
		if err != nil {
			log.Printf("codespace poll host=%s: %v (output: %s)", hostID, err, strings.TrimSpace(string(out)))
			// SSH failed (codespace paused, gh hiccup, etc.). Don't
			// declare running workers dead from a single failed
			// poll, but do unstick anything past the stopping
			// deadline — those we already asked to exit, and the
			// UI shouldn't show "stopping" forever.
			for _, w := range ws {
				if w.state == registry.StateStopping && m.stoppingFor(w.id, now) > stoppingDeadline {
					m.markExited(w.id, "ssh-unreachable-after-stop")
				}
			}
			continue
		}
		alive := map[int]bool{}
		sc := bufio.NewScanner(bytes.NewReader(out))
		for sc.Scan() {
			line := strings.TrimSpace(sc.Text())
			if line == "" {
				continue
			}
			if pid, err := strconv.Atoi(line); err == nil {
				alive[pid] = true
			}
		}
		for _, w := range ws {
			if alive[w.pid] {
				// Belt-and-braces: a worker we asked to stop ages
				// ago that SSH still claims is alive is most likely
				// a different process that recycled the PID. Force
				// the transition.
				if w.state == registry.StateStopping && m.stoppingFor(w.id, now) > stoppingDeadline {
					m.markExited(w.id, "stopping-deadline-exceeded")
				}
				continue
			}
			m.markExited(w.id, fmt.Sprintf("poll: pid %d not alive", w.pid))
		}
	}
}

// stoppingFor returns how long the operator has been waiting for the
// given worker to exit, or 0 if no stop request was ever recorded
// (e.g. the worker exited on its own).
func (m *Manager) stoppingFor(id string, now time.Time) time.Duration {
	m.mu.Lock()
	defer m.mu.Unlock()
	t, ok := m.stopRequested[id]
	if !ok {
		return 0
	}
	return now.Sub(t)
}

// markExited transitions a worker to StateExited if it isn't already.
// Centralises the bookkeeping so poll-success and safety-net branches
// behave identically.
func (m *Manager) markExited(id, reason string) {
	now := time.Now().UTC()
	transitioned := false
	m.reg.Update(id, func(rw *registry.Worker) {
		if rw.State == registry.StateExited {
			return
		}
		rw.State = registry.StateExited
		rw.StoppedAt = &now
		// Exit code is unknown remotely (the worker's parent on the
		// codespace is the bash session that launched it, not us).
		// Match local-Adopted semantics: leave ExitCode nil.
		transitioned = true
	})
	if transitioned {
		log.Printf("codespace exit %s: %s", id, reason)
		m.mu.Lock()
		delete(m.stopRequested, id)
		m.mu.Unlock()
	}
}

// runForWorker returns the runState the worker belongs to, or nil if
// this manager doesn't own that worker (e.g. it's local).
func (m *Manager) runForWorker(id string) *runState {
	w, ok := m.reg.Get(id)
	if !ok {
		return nil
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.runs[w.RunID]
}

// runningWorkerIDs returns IDs of every codespace-managed worker
// currently in Running or Stopping state.
func (m *Manager) runningWorkerIDs() []string {
	m.mu.Lock()
	known := make(map[string]bool, len(m.runs))
	for runID := range m.runs {
		known[runID] = true
	}
	m.mu.Unlock()
	var out []string
	for _, w := range m.reg.List() {
		if !known[w.RunID] {
			continue
		}
		if w.State == registry.StateRunning || w.State == registry.StateStopping {
			out = append(out, w.ID)
		}
	}
	return out
}

// IsCodespaceWorker reports whether this manager owns the given
// worker (used by the API to dispatch Stop/Log calls to the right
// backend without comparing host strings everywhere).
func (m *Manager) IsCodespaceWorker(id string) bool {
	return m.runForWorker(id) != nil
}

// mirrorManifest writes the remote manifest verbatim under
// <cacheDir>/runs/codespace-<host>/<run-id>/manifest.json so the
// operator can inspect it locally even if the codespace is paused.
func (m *Manager) mirrorManifest(hostID, runID string, raw []byte) {
	if m.cacheDir == "" {
		return
	}
	dir := filepath.Join(m.cacheDir, "runs", "codespace-"+hostID, runID)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		log.Printf("codespace: mirror manifest mkdir: %v", err)
		return
	}
	dst := filepath.Join(dir, "manifest.json")
	tmp := dst + ".tmp"
	if err := os.WriteFile(tmp, raw, 0o644); err != nil {
		log.Printf("codespace: mirror manifest write: %v", err)
		return
	}
	if err := os.Rename(tmp, dst); err != nil {
		log.Printf("codespace: mirror manifest rename: %v", err)
	}
}

// parseSpawnOutput plucks the run dir + manifest from the merged
// stdout/stderr fenced by sentinel markers. Returns a friendly
// error if the markers are missing (typically means the start
// script bailed before the epilogue ran).
func parseSpawnOutput(out []byte) (runDir string, manifest []byte, err error) {
	runDir, ok := extractBlock(out, []byte(runDirBeginMarker), []byte(runDirEndMarker))
	if !ok {
		return "", nil, errors.New("run-dir sentinel not found in remote output")
	}
	runDir = strings.TrimSpace(runDir)
	mraw, ok := extractBlock(out, []byte(manifestBeginMark), []byte(manifestEndMarker))
	if !ok {
		return "", nil, errors.New("manifest sentinel not found in remote output")
	}
	return runDir, []byte(strings.TrimSpace(mraw)), nil
}

// extractBlock returns the substring between (begin\n ... \nend),
// exclusive of the marker lines themselves.
func extractBlock(buf, begin, end []byte) (string, bool) {
	bi := bytes.Index(buf, begin)
	if bi < 0 {
		return "", false
	}
	// advance past begin marker line
	rest := buf[bi+len(begin):]
	// skip to next newline after begin marker
	if nl := bytes.IndexByte(rest, '\n'); nl >= 0 {
		rest = rest[nl+1:]
	}
	ei := bytes.Index(rest, end)
	if ei < 0 {
		return "", false
	}
	return string(rest[:ei]), true
}

// newWorkerID returns a fresh w-XXXXXXXXXXXX identifier matching
// local.Manager's format so the API can't tell who minted an id.
func newWorkerID() string {
	var b [6]byte
	if _, err := rand.Read(b[:]); err != nil {
		return fmt.Sprintf("w-%x", time.Now().UnixNano())
	}
	return "w-" + hex.EncodeToString(b[:])
}
