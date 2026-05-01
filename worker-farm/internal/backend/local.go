// LocalBackend implementation: starts/stops worker processes on the
// same machine the controller runs on. This is the only backend Phase
// 2 ships; Phase 3 will add a CodespaceBackend alongside.
//
// Lifecycle expectations:
//   - Start invokes os/exec, redirects Stdout/Stderr to the
//     caller-supplied sinks, sets Env via append-to-os.Environ, and
//     applies platform-specific process-control flags via
//     configureProc (Setsid on POSIX, DETACHED_PROCESS|
//     CREATE_NEW_PROCESS_GROUP on Windows). The detachment is
//     deliberate: workers must outlive the controller so the next
//     controller launch can adopt them.
//   - Adopt wraps a foreign PID. Wait on adopted handles polls
//     IsAlive at adoptedPollInterval; exit codes are reported as nil.
//   - Terminate / Kill route through the platform-specific helpers
//     (terminateProc/killProc for forked children, terminatePID/
//     killPID for adopted PIDs). On Windows both routes converge on
//     TerminateProcess since detached children have no console for
//     CTRL_BREAK.

package backend

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"time"
)

// LocalBackend is the default backend; spawns workers on the local
// machine.
type LocalBackend struct {
	// AdoptedPollInterval controls how often Wait polls IsAlive for
	// adopted handles. Zero means use the default (2 s).
	AdoptedPollInterval time.Duration
}

// NewLocal constructs a LocalBackend with sensible defaults.
func NewLocal() *LocalBackend {
	return &LocalBackend{AdoptedPollInterval: 2 * time.Second}
}

// Compile-time check that LocalBackend satisfies Backend.
var _ Backend = (*LocalBackend)(nil)

// Start forks a child process per spec.
func (b *LocalBackend) Start(_ context.Context, spec Spec) (*Handle, error) {
	if spec.Bin == "" {
		return nil, fmt.Errorf("backend.Local.Start: empty bin path")
	}
	cmd := exec.Command(spec.Bin, spec.Args...)
	cmd.Stdout = spec.Stdout
	cmd.Stderr = spec.Stderr
	if len(spec.Env) > 0 {
		cmd.Env = append(os.Environ(), spec.Env...)
	}
	configureProc(cmd) // platform-specific: Setsid / DETACHED_PROCESS

	if err := cmd.Start(); err != nil {
		return nil, err
	}
	return &Handle{
		PID:  cmd.Process.Pid,
		impl: cmd,
	}, nil
}

// Adopt produces a Handle for a foreign PID.
func (b *LocalBackend) Adopt(pid int) *Handle {
	return &Handle{PID: pid, Adopted: true}
}

// IsAlive returns true if the worker's PID is still a running
// process. Implementation lives in the platform-specific files.
func (b *LocalBackend) IsAlive(h *Handle) bool {
	if h == nil {
		return false
	}
	return isAlive(h.PID)
}

// IsAliveLocal exposes the local-host liveness probe at package
// scope so adoption-scan code (which inspects on-disk sentinels for
// the local machine only) can check PIDs without holding a Backend
// instance. Backends for remote hosts have their own equivalents.
func IsAliveLocal(pid int) bool {
	return isAlive(pid)
}

// Terminate sends a graceful stop. For forked children this is a
// signal to the process group; for adopted PIDs it's a single-process
// signal.
func (b *LocalBackend) Terminate(h *Handle) error {
	if h == nil {
		return nil
	}
	if cmd, ok := h.impl.(*exec.Cmd); ok && cmd != nil {
		return terminateProc(cmd)
	}
	return terminatePID(h.PID)
}

// Kill is the SIGKILL / TerminateProcess fallback.
func (b *LocalBackend) Kill(h *Handle) error {
	if h == nil {
		return nil
	}
	if cmd, ok := h.impl.(*exec.Cmd); ok && cmd != nil {
		return killProc(cmd)
	}
	return killPID(h.PID)
}

// Wait blocks until the worker exits.
//
// Forked-child path: cmd.Wait() collects the exit status.
// Adopted path: poll IsAlive at AdoptedPollInterval; exit code is
// returned as nil since the kernel only reports it to the parent.
func (b *LocalBackend) Wait(h *Handle) ExitInfo {
	if h == nil {
		return ExitInfo{Err: fmt.Errorf("backend.Local.Wait: nil handle")}
	}
	if cmd, ok := h.impl.(*exec.Cmd); ok && cmd != nil {
		err := cmd.Wait()
		var code *int
		if cmd.ProcessState != nil {
			c := cmd.ProcessState.ExitCode()
			code = &c
		}
		// cmd.Wait() returns *exec.ExitError for non-zero exits; that
		// is not a transport error, just a worker exit. We only
		// surface true Err for unexpected failures.
		if _, isExit := err.(*exec.ExitError); err != nil && !isExit {
			return ExitInfo{Code: code, Err: err}
		}
		return ExitInfo{Code: code}
	}
	// Adopted: poll.
	interval := b.AdoptedPollInterval
	if interval <= 0 {
		interval = 2 * time.Second
	}
	t := time.NewTicker(interval)
	defer t.Stop()
	for range t.C {
		if !isAlive(h.PID) {
			return ExitInfo{}
		}
	}
	return ExitInfo{} // unreachable
}
