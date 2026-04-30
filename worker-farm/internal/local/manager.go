// Package local implements the Phase 1 backend: spawning tm-worker as a
// direct child of the controller, watching it, and stopping it
// gracefully (SIGTERM/taskkill, then SIGKILL after a grace window).
package local

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"log"
	"os"
	"os/exec"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/adopt"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/liveness"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/logbuf"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/manifest"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/paths"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/registry"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/sentinel"
)

// Manager spawns and supervises workers tracked in a registry.
type Manager struct {
	reg          *registry.Registry
	cacheDir     string
	hostname     string // cached os.Hostname()
	workerBin    string
	configPath   string
	controllerID string // exported as TM_WORKER_FARM_ID
	gracePeriod  time.Duration

	mu      sync.Mutex
	running map[string]*procEntry // workers actively supervised by this controller
	runs    map[string]*runState  // run-id → metadata + worker-id list, for manifest persistence

	// quarantine holds adoption candidates classified as ClassTheirs
	// at startup (or by a later rescan). The operator decides via the
	// quarantine API what to do with each. Keyed by Candidate.Key()
	// for stable URL routing.
	quarantine map[string]adopt.Candidate
}

type procEntry struct {
	cmd       *exec.Cmd // nil for adopted workers
	logFile   *os.File  // nil for adopted workers (we don't own the file handle)
	startedAt time.Time
	stopOnce  sync.Once
	stopCh    chan struct{} // closed once Wait/poll observes exit
	killed    bool          // set true when the grace timer fired

	// Adopted-worker fields. Set when this entry tracks a process the
	// controller did NOT fork (discovered via worker-NN.adopt at
	// startup). cmd is nil; lifecycle is observed via poll-based
	// liveness instead of cmd.Wait().
	adoptedPID int
}

// runState is the per-run bookkeeping the manager keeps so it can
// re-render manifest.json on every state change without scanning the
// whole registry.
type runState struct {
	runID     string
	runDir    string
	startedAt time.Time
	baseArgs  []string
	workerIDs []string   // registry IDs in slot order (1-based slots map to index+1)
	writeMu   sync.Mutex // serialises manifest.json writes for this run
}

// Options configures Manager.
type Options struct {
	Registry     *registry.Registry
	CacheDir     string
	WorkerBin    string
	ConfigPath   string
	ControllerID string
	// GracePeriod is the time between graceful stop and SIGKILL.
	// Defaults to 10 s if zero.
	GracePeriod time.Duration
}

// New constructs a Manager. The caller owns the registry; Manager only
// inserts/updates rows it spawned.
func New(opts Options) *Manager {
	g := opts.GracePeriod
	if g == 0 {
		g = 10 * time.Second
	}
	hostname, _ := os.Hostname()
	return &Manager{
		reg:          opts.Registry,
		cacheDir:     opts.CacheDir,
		hostname:     hostname,
		workerBin:    opts.WorkerBin,
		configPath:   opts.ConfigPath,
		controllerID: opts.ControllerID,
		gracePeriod:  g,
		running:      make(map[string]*procEntry),
		runs:         make(map[string]*runState),
		quarantine:   make(map[string]adopt.Candidate),
	}
}

// SpawnResult is one entry in the multi-status response from Spawn.
type SpawnResult struct {
	ID    string `json:"id"`
	OK    bool   `json:"ok"`
	PID   int    `json:"pid,omitempty"`
	Error string `json:"error,omitempty"`
}

// Spawn launches `count` workers concurrently, each receiving extraArgs
// appended after the controller-supplied flags. Returns a per-worker
// outcome slice. Per Tactical Decision #1, all spawns happen in
// parallel and the call returns once every spawn has either started or
// reported a start error.
//
// One Spawn call corresponds to one *run*: a single `runs/<run-id>/`
// directory holding all the spawned workers' logs (and, in Slice 2, a
// shared `manifest.json`). Workers within a run are numbered 1..count
// (the `slot`) and their logs land at `worker-NN.log`. Slot ordering is
// stable in the result slice.
func (m *Manager) Spawn(ctx context.Context, count int, extraArgs []string) []SpawnResult {
	if count <= 0 {
		return nil
	}
	runID, runDir, err := m.allocateRunDir()
	if err != nil {
		// Allocation failure is rare and fatal for the whole batch —
		// fabricate an error result per requested worker so the API
		// shape stays consistent.
		log.Printf("spawn batch: allocate run dir failed: %v", err)
		results := make([]SpawnResult, count)
		for i := range results {
			results[i] = SpawnResult{ID: newWorkerID(), OK: false, Error: fmt.Sprintf("allocate run dir: %v", err)}
		}
		return results
	}
	log.Printf("spawn batch: run=%s count=%d dir=%s", runID, count, runDir)

	// Pre-compute the controller-supplied base args so the manifest
	// records exactly what every worker in the run got prepended.
	baseArgs := append([]string{"-c", m.configPath, "--mode", "blocking", "--noui"}, extraArgs...)

	// Track the run for persistence. Initial manifest is written
	// pre-spawn so a crash between Mkdir and the first cmd.Start
	// still leaves a valid (empty-workers) manifest behind.
	run := &runState{
		runID:     runID,
		runDir:    runDir,
		startedAt: time.Now().UTC(),
		baseArgs:  baseArgs,
		workerIDs: make([]string, count),
	}
	m.mu.Lock()
	m.runs[runID] = run
	m.mu.Unlock()
	m.persistRun(runID)
	m.updateLatestPointer(runID)

	results := make([]SpawnResult, count)
	var wg sync.WaitGroup
	wg.Add(count)
	for i := 0; i < count; i++ {
		slot := i + 1 // 1-based to match worker-NN.log
		go func(idx, slot int) {
			defer wg.Done()
			results[idx] = m.spawnOne(ctx, runID, runDir, slot, extraArgs)
		}(i, slot)
	}
	wg.Wait()
	return results
}

// allocateRunDir picks a run-id of the form `YYYYMMDD-HHMMSS` (with a
// `-N` suffix on collision) and creates the run directory. Returns the
// run-id and the absolute directory path. Uses os.Mkdir's atomic
// creation as the collision arbiter so two concurrent Spawn calls in
// the same wall-clock second can't both pick the same id.
func (m *Manager) allocateRunDir() (string, string, error) {
	base := time.Now().UTC().Format("20060102-150405")
	runsDir := paths.RunsDir(m.cacheDir)
	if err := os.MkdirAll(runsDir, 0o755); err != nil {
		return "", "", fmt.Errorf("create runs dir %s: %w", runsDir, err)
	}
	for n := 1; n <= 100; n++ {
		candidate := base
		if n > 1 {
			candidate = fmt.Sprintf("%s-%d", base, n)
		}
		dir := paths.RunDir(m.cacheDir, candidate)
		if err := os.Mkdir(dir, 0o755); err == nil {
			return candidate, dir, nil
		} else if !errors.Is(err, os.ErrExist) {
			return "", "", fmt.Errorf("create run dir %s: %w", dir, err)
		}
	}
	return "", "", fmt.Errorf("could not allocate unique run dir under %s after 100 attempts", runsDir)
}

func (m *Manager) spawnOne(_ context.Context, runID, runDir string, slot int, extraArgs []string) SpawnResult {
	id := newWorkerID()
	logPath := paths.WorkerSlotLogPath(runDir, slot)
	args := []string{"-c", m.configPath, "--mode", "blocking", "--noui"}
	args = append(args, extraArgs...)

	logFile, err := logbuf.Open(logPath)
	if err != nil {
		m.recordFailure(id, runID, slot, args, logPath, fmt.Errorf("open log: %w", err))
		log.Printf("spawn %s [run=%s slot=%02d]: FAILED to open log %s: %v", id, runID, slot, logPath, err)
		return SpawnResult{ID: id, OK: false, Error: err.Error()}
	}

	cmd := exec.Command(m.workerBin, args...)
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	cmd.Env = append(os.Environ(), "TM_WORKER_FARM_ID="+m.controllerID)
	configureProc(cmd) // platform-specific: process group / job object

	if err := cmd.Start(); err != nil {
		_ = logFile.Close()
		m.recordFailure(id, runID, slot, args, logPath, fmt.Errorf("start: %w", err))
		log.Printf("spawn %s [run=%s slot=%02d]: FAILED to start: %v (cmd: %s %s)", id, runID, slot, err, m.workerBin, strings.Join(args, " "))
		return SpawnResult{ID: id, OK: false, Error: err.Error()}
	}

	startedAt := time.Now().UTC()
	w := &registry.Worker{
		ID:        id,
		PID:       cmd.Process.Pid,
		State:     registry.StateRunning,
		RunID:     runID,
		Slot:      slot,
		StartedAt: startedAt,
		Args:      args,
		LogPath:   logPath,
		Host:      "local",
	}
	m.reg.Add(w)

	entry := &procEntry{cmd: cmd, logFile: logFile, startedAt: startedAt, stopCh: make(chan struct{})}
	m.mu.Lock()
	m.running[id] = entry
	if run, ok := m.runs[runID]; ok && slot >= 1 && slot <= len(run.workerIDs) {
		run.workerIDs[slot-1] = id
	}
	m.mu.Unlock()

	// Drop the adoption sentinel and the script-compatible pidfile.
	// Best-effort: a sentinel-write failure is logged but doesn't
	// fail the spawn — the worker is already running.
	pidfilePath := paths.WorkerSlotPidPath(runDir, slot)
	if werr := os.WriteFile(pidfilePath, []byte(fmt.Sprintf("%d\n", cmd.Process.Pid)), 0o644); werr != nil {
		log.Printf("spawn %s [run=%s slot=%02d]: warning: write pidfile %s: %v", id, runID, slot, pidfilePath, werr)
	}
	sentinelPath := paths.WorkerSlotSentinelPath(runDir, slot)
	if serr := sentinel.Write(sentinelPath, &sentinel.Sentinel{
		ControllerID:  m.controllerID,
		StartedAtUnix: startedAt.Unix(),
		PID:           cmd.Process.Pid,
		WorkerBin:     m.workerBin,
		Args:          args,
		LogPath:       logPath,
		ConfigPath:    m.configPath,
		RunID:         runID,
		Slot:          slot,
	}); serr != nil {
		log.Printf("spawn %s [run=%s slot=%02d]: warning: write sentinel %s: %v", id, runID, slot, sentinelPath, serr)
	}

	m.persistRun(runID)

	log.Printf("spawn %s [run=%s slot=%02d]: started pid=%d cmd=%s %s log=%s", id, runID, slot, cmd.Process.Pid, m.workerBin, strings.Join(args, " "), logPath)

	go m.supervise(id, entry)

	return SpawnResult{ID: id, OK: true, PID: cmd.Process.Pid}
}

func (m *Manager) recordFailure(id, runID string, slot int, args []string, logPath string, err error) {
	now := time.Now().UTC()
	m.reg.Add(&registry.Worker{
		ID:        id,
		State:     registry.StateExited,
		RunID:     runID,
		Slot:      slot,
		StartedAt: now,
		StoppedAt: &now,
		Args:      args,
		LogPath:   logPath,
		LastError: err.Error(),
		Host:      "local",
	})
	m.mu.Lock()
	if run, ok := m.runs[runID]; ok && slot >= 1 && slot <= len(run.workerIDs) {
		run.workerIDs[slot-1] = id
	}
	m.mu.Unlock()
	m.persistRun(runID)
}

// supervise blocks on cmd.Wait, then updates the registry.
func (m *Manager) supervise(id string, entry *procEntry) {
	err := entry.cmd.Wait()
	_ = entry.logFile.Close()
	now := time.Now().UTC()
	exitCode := 0
	if err != nil {
		var ee *exec.ExitError
		if errors.As(err, &ee) {
			exitCode = ee.ExitCode()
		} else {
			exitCode = -1
		}
	}
	var runID string
	m.reg.Update(id, func(w *registry.Worker) {
		w.State = registry.StateExited
		w.StoppedAt = &now
		ec := exitCode
		w.ExitCode = &ec
		if err != nil && w.LastError == "" {
			// Keep stop-induced exit codes informative without
			// drowning the operator in noise.
			if exitCode != 0 && w.State == registry.StateExited {
				w.LastError = err.Error()
			}
		}
		runID = w.RunID
	})
	m.mu.Lock()
	delete(m.running, id)
	killed := entry.killed
	m.mu.Unlock()
	if runID != "" {
		m.persistRun(runID)
	}

	pid := 0
	if entry.cmd.Process != nil {
		pid = entry.cmd.Process.Pid
	}
	duration := now.Sub(entry.startedAt).Round(time.Millisecond)
	cause := "natural"
	if killed {
		cause = "killed-after-grace"
	}
	log.Printf("exit  %s: pid=%d code=%d cause=%s uptime=%s", id, pid, exitCode, cause, duration)

	close(entry.stopCh)
}

// Stop initiates a graceful stop on a single worker. Returns nil if the
// worker is already exited or not tracked by this manager.
func (m *Manager) Stop(ctx context.Context, id string) error {
	m.mu.Lock()
	entry, ok := m.running[id]
	m.mu.Unlock()
	if !ok {
		return nil
	}
	pid := entry.adoptedPID
	if entry.cmd != nil && entry.cmd.Process != nil {
		pid = entry.cmd.Process.Pid
	}
	adoptedTag := ""
	if entry.adoptedPID != 0 {
		adoptedTag = " (adopted)"
	}
	log.Printf("stop  %s%s: pid=%d grace=%s", id, adoptedTag, pid, m.gracePeriod)
	var runID string
	m.reg.Update(id, func(w *registry.Worker) {
		w.State = registry.StateStopping
		runID = w.RunID
	})
	if runID != "" {
		m.persistRun(runID)
	}
	entry.stopOnce.Do(func() {
		go m.gracefulKill(id, entry)
	})
	select {
	case <-entry.stopCh:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}

// StopAll initiates a graceful stop on every running worker.
func (m *Manager) StopAll(ctx context.Context) {
	m.mu.Lock()
	ids := make([]string, 0, len(m.running))
	for id := range m.running {
		ids = append(ids, id)
	}
	m.mu.Unlock()
	if len(ids) == 0 {
		return
	}
	log.Printf("stop-all: %d worker(s)", len(ids))
	var wg sync.WaitGroup
	for _, id := range ids {
		wg.Add(1)
		go func(id string) {
			defer wg.Done()
			_ = m.Stop(ctx, id)
		}(id)
	}
	wg.Wait()
}

func (m *Manager) gracefulKill(id string, entry *procEntry) {
	// Branch on adopted vs forked: adopted workers are not in our
	// process group on POSIX (so we can't signal the group) and can't
	// be sent a console event on Windows (TerminateProcess is the
	// only available verb). Both cases route through the PID-only
	// helpers; the grace timer still runs, mostly for log clarity and
	// for POSIX where SIGTERM is honoured by tm-worker.
	if entry.adoptedPID != 0 {
		if err := terminatePID(entry.adoptedPID); err != nil {
			log.Printf("stop  %s: terminate(adopted) failed (will fall back to kill): %v", id, err)
		}
	} else {
		if err := terminateProc(entry.cmd); err != nil {
			log.Printf("stop  %s: terminate failed (will fall back to kill): %v", id, err)
		}
	}
	timer := time.NewTimer(m.gracePeriod)
	defer timer.Stop()
	select {
	case <-entry.stopCh:
		return
	case <-timer.C:
		m.mu.Lock()
		entry.killed = true
		m.mu.Unlock()
		if entry.adoptedPID != 0 {
			log.Printf("stop  %s: grace expired, sending KILL pid=%d (adopted)", id, entry.adoptedPID)
			_ = killPID(entry.adoptedPID)
			return
		}
		pid := 0
		if entry.cmd.Process != nil {
			pid = entry.cmd.Process.Pid
		}
		log.Printf("stop  %s: grace expired, sending KILL pid=%d", id, pid)
		_ = killProc(entry.cmd)
	}
}

func newWorkerID() string {
	var b [6]byte
	if _, err := rand.Read(b[:]); err != nil {
		// Fall back to time-based; only happens if crypto/rand fails,
		// which it doesn't in practice.
		return fmt.Sprintf("w-%x", time.Now().UnixNano())
	}
	return "w-" + hex.EncodeToString(b[:])
}

// persistRun re-renders manifest.json for one run and writes it
// atomically. Called on every observable state change so the on-disk
// view never lags the registry by more than one syscall. A failure
// here is logged but never bubbled up: the controller's authoritative
// state is the in-memory registry; the manifest is a side-channel for
// future restarts and the shell-script driver.
func (m *Manager) persistRun(runID string) {
	m.mu.Lock()
	run, ok := m.runs[runID]
	if !ok {
		m.mu.Unlock()
		return
	}
	// Snapshot worker IDs while we hold the run map lock; release
	// before fetching from the registry to avoid lock ordering
	// surprises.
	ids := append([]string(nil), run.workerIDs...)
	rmRunDir := run.runDir
	rmStart := run.startedAt
	rmBaseArgs := append([]string(nil), run.baseArgs...)
	m.mu.Unlock()

	// Serialise file writes for this run so concurrent state changes
	// (parallel spawns, stop-all) don't race on manifest.json.tmp.
	run.writeMu.Lock()
	defer run.writeMu.Unlock()

	workers := make([]manifest.Worker, 0, len(ids))
	for slot, id := range ids {
		if id == "" {
			continue // spawn for this slot hasn't completed yet
		}
		w, ok := m.reg.Get(id)
		if !ok || w == nil {
			continue
		}
		entry := manifest.Worker{
			ID:        fmt.Sprintf("%02d", slot+1),
			PID:       w.PID,
			Log:       w.LogPath,
			Pidfile:   paths.WorkerSlotPidPath(rmRunDir, slot+1),
			WorkerID:  w.ID,
			State:     string(w.State),
			StartedAt: w.StartedAt,
			StoppedAt: w.StoppedAt,
			ExitCode:  w.ExitCode,
			Args:      w.Args,
			LastError: w.LastError,
		}
		workers = append(workers, entry)
	}
	// Stable order: by slot (workers slice is already in slot order
	// because ids is indexed by slot).
	sort.SliceStable(workers, func(i, j int) bool { return workers[i].ID < workers[j].ID })

	man := &manifest.Manifest{
		RunID:        runID,
		StartedAt:    rmStart,
		Host:         "local",
		Hostname:     m.hostname,
		OS:           osLabel(),
		BaseDir:      rmRunDir,
		WorkerBin:    m.workerBin,
		Config:       m.configPath,
		Args:         rmBaseArgs,
		Workers:      workers,
		ControllerID: m.controllerID,
	}
	path := paths.RunManifestPath(rmRunDir)
	if err := manifest.Write(path, man); err != nil {
		log.Printf("persist run=%s: write manifest %s: %v", runID, path, err)
	}
}

// updateLatestPointer writes <runs>/latest.txt so a CLI/script driver
// (and future controllers) can find the most recent run without
// scanning. Mirrors `start_workers_local.sh`'s behaviour.
func (m *Manager) updateLatestPointer(runID string) {
	path := paths.LatestRunPath(m.cacheDir)
	if err := os.WriteFile(path, []byte(runID+"\n"), 0o644); err != nil {
		log.Printf("write latest pointer %s: %v", path, err)
	}
}

// osLabel returns the lowercase OS string the shell scripts emit
// (`uname -s | tr '[:upper:]' '[:lower:]'`). On Windows we follow the
// same convention with the literal "windows".
func osLabel() string {
	switch runtime.GOOS {
	case "darwin":
		return "darwin"
	case "linux":
		return "linux"
	case "windows":
		return "windows"
	default:
		return runtime.GOOS
	}
}

// adoptedPollInterval controls the cadence of liveness checks for
// adopted workers. The plan calls for 2 s; cheap on every OS we care
// about and gives the UI a snappy "exited (orphan)" transition.
const adoptedPollInterval = 2 * time.Second

// Adopt registers a previously-discovered live worker (ClassMine
// candidate) into the registry as a running, supervised entry. The
// supervise loop polls liveness rather than waiting on a child handle
// since this controller did not fork the process.
//
// Returns the registry ID assigned to the adopted worker. Safe to call
// multiple times for the same sentinel — the second call is a no-op
// because the registry rejects duplicate IDs (we derive a stable ID
// from run-id + slot).
func (m *Manager) Adopt(c adopt.Candidate) string {
	if c.Sentinel == nil || c.Sentinel.PID <= 0 {
		return ""
	}
	id := adoptedID(c)
	startedAt := time.Unix(c.Sentinel.StartedAtUnix, 0).UTC()

	w := &registry.Worker{
		ID:        id,
		PID:       c.Sentinel.PID,
		State:     registry.StateRunning,
		RunID:     c.Sentinel.RunID,
		Slot:      c.Sentinel.Slot,
		StartedAt: startedAt,
		Args:      c.Sentinel.Args,
		LogPath:   c.Sentinel.LogPath,
		Host:      "local",
		Adopted:   true,
	}
	if !m.reg.Add(w) {
		return id // already adopted in a previous pass
	}

	entry := &procEntry{
		startedAt:  startedAt,
		stopCh:     make(chan struct{}),
		adoptedPID: c.Sentinel.PID,
	}
	m.mu.Lock()
	m.running[id] = entry
	m.mu.Unlock()

	log.Printf("adopt %s: run=%s slot=%02d pid=%d log=%s", id, c.Sentinel.RunID, c.Sentinel.Slot, c.Sentinel.PID, c.Sentinel.LogPath)
	go m.superviseAdopted(id, entry)
	return id
}

// RegisterStale folds a ClassStale candidate into the registry as an
// already-exited row. Exit code is null (we did not parent the
// process and the kernel doesn't tell anyone else).
func (m *Manager) RegisterStale(c adopt.Candidate) string {
	if c.Sentinel == nil {
		return ""
	}
	id := adoptedID(c)
	startedAt := time.Unix(c.Sentinel.StartedAtUnix, 0).UTC()
	now := time.Now().UTC()
	w := &registry.Worker{
		ID:        id,
		PID:       c.Sentinel.PID,
		State:     registry.StateExited,
		RunID:     c.Sentinel.RunID,
		Slot:      c.Sentinel.Slot,
		StartedAt: startedAt,
		StoppedAt: &now,
		Args:      c.Sentinel.Args,
		LogPath:   c.Sentinel.LogPath,
		Host:      "local",
		Adopted:   true,
	}
	if !m.reg.Add(w) {
		return id
	}
	log.Printf("stale %s: run=%s slot=%02d pid=%d (process gone, recorded as exited)", id, c.Sentinel.RunID, c.Sentinel.Slot, c.Sentinel.PID)
	return id
}

// adoptedID derives a stable registry ID for an adopted candidate so
// repeated adoption passes don't create duplicate rows.
func adoptedID(c adopt.Candidate) string {
	return fmt.Sprintf("adopted-%s-%02d", c.Sentinel.RunID, c.Sentinel.Slot)
}

// superviseAdopted polls liveness on the adopted PID until it
// disappears, then marks the registry row exited. Exit code stays
// nil (we are not the parent; the kernel won't report it to us).
func (m *Manager) superviseAdopted(id string, entry *procEntry) {
	tick := time.NewTicker(adoptedPollInterval)
	defer tick.Stop()
	for range tick.C {
		if !liveness.IsAlive(entry.adoptedPID) {
			break
		}
	}

	now := time.Now().UTC()
	m.reg.Update(id, func(w *registry.Worker) {
		w.State = registry.StateExited
		w.StoppedAt = &now
		// Exit code intentionally left nil: we did not parent this
		// process. The UI key for "orphan exit" is Adopted=true +
		// State=exited + ExitCode==nil.
	})
	m.mu.Lock()
	delete(m.running, id)
	killed := entry.killed
	m.mu.Unlock()

	cause := "natural"
	if killed {
		cause = "killed-after-grace"
	}
	duration := now.Sub(entry.startedAt).Round(time.Millisecond)
	log.Printf("exit  %s (adopted): pid=%d cause=%s uptime=%s", id, entry.adoptedPID, cause, duration)

	close(entry.stopCh)
}

// SetQuarantine replaces the in-memory quarantine set with the given
// candidates. Called once at startup after the adopt.Scan pass; can
// be called again later if a rescan endpoint is added.
func (m *Manager) SetQuarantine(cands []adopt.Candidate) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.quarantine = make(map[string]adopt.Candidate, len(cands))
	for _, c := range cands {
		m.quarantine[c.Key()] = c
	}
}

// Quarantine returns a snapshot of the current quarantine set,
// re-checking liveness on each entry so the UI doesn't display stale
// "alive" badges.
func (m *Manager) Quarantine() []adopt.Candidate {
	m.mu.Lock()
	cands := make([]adopt.Candidate, 0, len(m.quarantine))
	for _, c := range m.quarantine {
		cands = append(cands, c)
	}
	m.mu.Unlock()
	for i := range cands {
		cands[i].Alive = liveness.IsAlive(cands[i].Sentinel.PID)
	}
	return cands
}

// QuarantineAct performs the operator's chosen action on a quarantine
// entry. action ∈ {"adopt","kill","ignore"}.
//
//   - adopt:  promote the candidate into the running registry; sentinel
//     stays on disk (its controller_id still belongs to the
//     original install).
//   - kill:   gracefully terminate the foreign PID and remove the
//     sentinel + pidfile so the next scan doesn't re-list it.
//   - ignore: leave the process alone but remove the sentinel so the
//     next scan stops surfacing it.
func (m *Manager) QuarantineAct(ctx context.Context, key, action string) error {
	m.mu.Lock()
	c, ok := m.quarantine[key]
	if !ok {
		m.mu.Unlock()
		return fmt.Errorf("quarantine: no entry %q", key)
	}
	delete(m.quarantine, key)
	m.mu.Unlock()

	switch action {
	case "adopt":
		// Re-check liveness; an operator that took their time may be
		// adopting a now-dead PID, in which case fold it as stale.
		if !liveness.IsAlive(c.Sentinel.PID) {
			m.RegisterStale(c)
			return nil
		}
		m.Adopt(c)
		return nil
	case "kill":
		return m.killForeign(ctx, c)
	case "ignore":
		removeSentinelArtifacts(c)
		return nil
	default:
		// Unknown action: put it back so the operator can retry.
		m.mu.Lock()
		m.quarantine[key] = c
		m.mu.Unlock()
		return fmt.Errorf("quarantine: unknown action %q", action)
	}
}

// killForeign sends SIGTERM/TerminateProcess to a foreign PID, waits up
// to gracePeriod for it to exit, then SIGKILL/TerminateProcess if it
// hasn't. Removes the sentinel + pidfile so the next scan doesn't
// resurrect the entry.
func (m *Manager) killForeign(ctx context.Context, c adopt.Candidate) error {
	pid := c.Sentinel.PID
	log.Printf("quarantine kill: run=%s slot=%02d pid=%d", c.Sentinel.RunID, c.Sentinel.Slot, pid)
	if err := terminatePID(pid); err != nil {
		log.Printf("quarantine kill: terminate(%d): %v", pid, err)
	}
	deadline := time.NewTimer(m.gracePeriod)
	defer deadline.Stop()
	tick := time.NewTicker(250 * time.Millisecond)
	defer tick.Stop()
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-deadline.C:
			log.Printf("quarantine kill: grace expired, KILL pid=%d", pid)
			_ = killPID(pid)
			removeSentinelArtifacts(c)
			return nil
		case <-tick.C:
			if !liveness.IsAlive(pid) {
				removeSentinelArtifacts(c)
				return nil
			}
		}
	}
}

// removeSentinelArtifacts deletes the .adopt sentinel and the
// .pid file (if present) for a quarantine entry. Best-effort: failure
// just means the next scan will see them again, which is annoying but
// not dangerous.
func removeSentinelArtifacts(c adopt.Candidate) {
	if err := os.Remove(c.Path); err != nil && !errors.Is(err, os.ErrNotExist) {
		log.Printf("remove sentinel %s: %v", c.Path, err)
	}
	pidPath := paths.WorkerSlotPidPath(c.RunDir, c.Sentinel.Slot)
	if err := os.Remove(pidPath); err != nil && !errors.Is(err, os.ErrNotExist) {
		log.Printf("remove pidfile %s: %v", pidPath, err)
	}
}

// Purge removes an exited worker's row from the registry and deletes
// its on-disk trace (log file, pidfile, .adopt sentinel). Refuses to
// purge a worker that is still running — operator must Stop first.
//
// The slot's run-manifest entry is rewritten on the way out so the
// disappeared worker no longer shows up under runs/<run-id>/manifest.json.
// The run directory itself is left alone; multi-slot runs may still
// have other workers persisting state there.
func (m *Manager) Purge(id string) error {
	w, ok := m.reg.Get(id)
	if !ok {
		return fmt.Errorf("purge: no worker %q", id)
	}
	if w.State == registry.StateRunning || w.State == registry.StateStarting || w.State == registry.StateStopping {
		return fmt.Errorf("purge: worker %s is %s; stop it first", id, w.State)
	}

	// Resolve the on-disk paths from the worker record. Slot+RunID
	// are always populated, even for adopted/stale entries.
	runDir := paths.RunDir(m.cacheDir, w.RunID)
	logPath := w.LogPath
	pidPath := paths.WorkerSlotPidPath(runDir, w.Slot)
	sentinelPath := paths.WorkerSlotSentinelPath(runDir, w.Slot)

	for _, p := range []string{logPath, pidPath, sentinelPath} {
		if p == "" {
			continue
		}
		if err := os.Remove(p); err != nil && !errors.Is(err, os.ErrNotExist) {
			log.Printf("purge %s: remove %s: %v", id, p, err)
		}
	}

	// Drop from registry and clear the slot mapping so persistRun
	// stops emitting a manifest entry for this worker.
	m.reg.Remove(id)
	m.mu.Lock()
	if run, ok := m.runs[w.RunID]; ok && w.Slot >= 1 && w.Slot <= len(run.workerIDs) {
		if run.workerIDs[w.Slot-1] == id {
			run.workerIDs[w.Slot-1] = ""
		}
	}
	m.mu.Unlock()

	if w.RunID != "" {
		m.persistRun(w.RunID)
	}
	log.Printf("purge %s: run=%s slot=%02d log=%s", id, w.RunID, w.Slot, logPath)
	return nil
}
