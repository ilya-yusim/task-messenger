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
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/logbuf"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/registry"
)

// Manager spawns and supervises workers tracked in a registry.
type Manager struct {
	reg          *registry.Registry
	cacheDir     string
	workerBin    string
	configPath   string
	controllerID string // exported as TM_WORKER_FARM_ID
	gracePeriod  time.Duration

	mu      sync.Mutex
	running map[string]*procEntry // workers actively supervised by this controller
}

type procEntry struct {
	cmd       *exec.Cmd
	logFile   *os.File
	startedAt time.Time
	stopOnce  sync.Once
	stopCh    chan struct{} // closed once Wait returns
	killed    bool          // set true when the grace timer fired
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
	return &Manager{
		reg:          opts.Registry,
		cacheDir:     opts.CacheDir,
		workerBin:    opts.WorkerBin,
		configPath:   opts.ConfigPath,
		controllerID: opts.ControllerID,
		gracePeriod:  g,
		running:      make(map[string]*procEntry),
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
func (m *Manager) Spawn(ctx context.Context, count int, extraArgs []string) []SpawnResult {
	if count <= 0 {
		return nil
	}
	results := make([]SpawnResult, count)
	var wg sync.WaitGroup
	wg.Add(count)
	for i := 0; i < count; i++ {
		go func(idx int) {
			defer wg.Done()
			results[idx] = m.spawnOne(ctx, extraArgs)
		}(i)
	}
	wg.Wait()
	return results
}

func (m *Manager) spawnOne(_ context.Context, extraArgs []string) SpawnResult {
	id := newWorkerID()
	logPath := filepath.Join(m.cacheDir, id+".log")
	args := []string{"-c", m.configPath, "--mode", "blocking", "--noui"}
	args = append(args, extraArgs...)

	logFile, err := logbuf.Open(logPath)
	if err != nil {
		m.recordFailure(id, args, logPath, fmt.Errorf("open log: %w", err))
		log.Printf("spawn %s: FAILED to open log %s: %v", id, logPath, err)
		return SpawnResult{ID: id, OK: false, Error: err.Error()}
	}

	cmd := exec.Command(m.workerBin, args...)
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	cmd.Env = append(os.Environ(), "TM_WORKER_FARM_ID="+m.controllerID)
	configureProc(cmd) // platform-specific: process group / job object

	if err := cmd.Start(); err != nil {
		_ = logFile.Close()
		m.recordFailure(id, args, logPath, fmt.Errorf("start: %w", err))
		log.Printf("spawn %s: FAILED to start: %v (cmd: %s %s)", id, err, m.workerBin, strings.Join(args, " "))
		return SpawnResult{ID: id, OK: false, Error: err.Error()}
	}

	startedAt := time.Now().UTC()
	w := &registry.Worker{
		ID:        id,
		PID:       cmd.Process.Pid,
		State:     registry.StateRunning,
		StartedAt: startedAt,
		Args:      args,
		LogPath:   logPath,
		Host:      "local",
	}
	m.reg.Add(w)

	entry := &procEntry{cmd: cmd, logFile: logFile, startedAt: startedAt, stopCh: make(chan struct{})}
	m.mu.Lock()
	m.running[id] = entry
	m.mu.Unlock()

	log.Printf("spawn %s: started pid=%d cmd=%s %s log=%s", id, cmd.Process.Pid, m.workerBin, strings.Join(args, " "), logPath)

	go m.supervise(id, entry)

	return SpawnResult{ID: id, OK: true, PID: cmd.Process.Pid}
}

func (m *Manager) recordFailure(id string, args []string, logPath string, err error) {
	now := time.Now().UTC()
	m.reg.Add(&registry.Worker{
		ID:        id,
		State:     registry.StateExited,
		StartedAt: now,
		StoppedAt: &now,
		Args:      args,
		LogPath:   logPath,
		LastError: err.Error(),
		Host:      "local",
	})
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
	})
	m.mu.Lock()
	delete(m.running, id)
	killed := entry.killed
	m.mu.Unlock()

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
	pid := 0
	if entry.cmd.Process != nil {
		pid = entry.cmd.Process.Pid
	}
	log.Printf("stop  %s: pid=%d grace=%s", id, pid, m.gracePeriod)
	m.reg.Update(id, func(w *registry.Worker) { w.State = registry.StateStopping })
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
	if err := terminateProc(entry.cmd); err != nil {
		log.Printf("stop  %s: terminate failed (will fall back to kill): %v", id, err)
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
