// Package backend defines the host-abstraction layer the controller
// uses to start, observe, and stop worker processes. Phase 2 ships a
// single LocalBackend; Phase 3 will add a CodespaceBackend (and
// eventually generic SshBackend / GcpIapBackend) that satisfies the
// same interface so the rest of the controller — registry,
// adoption, manifest persistence, HTTP/UI — stays backend-agnostic.
//
// The interface is deliberately PID-flavoured: a Handle carries an
// integer PID that uniquely identifies the worker on its host. For
// remote backends, "PID" is the worker's PID on the remote VM, paired
// with whatever ssh/transport state the backend needs to interact
// with it. Local and remote callers never see the difference.
package backend

import (
	"context"
	"io"
)

// Spec describes a worker the caller wants to start. The backend
// chooses how to actually launch it (direct fork, ssh exec, etc.).
type Spec struct {
	// Bin is the path to the worker binary on the target host. For
	// LocalBackend this is a local path; for remote backends it is a
	// path on the remote machine.
	Bin string

	// Args are the worker's CLI arguments, in the order they should be
	// passed. Backend implementations MAY append additional args
	// (e.g. for remote backends to redirect logs into a specific
	// path on the remote machine).
	Args []string

	// Env is the environment for the worker. Local backend appends to
	// the controller's os.Environ(); remote backends interpret it as
	// the literal environment to set on the remote side.
	Env []string

	// Stdout / Stderr are sinks for the worker's output. For local
	// workers the backend wires these into the OS process directly.
	// Remote backends stream output to the sinks asynchronously.
	// nil means "discard" (or, for remote, "do not stream").
	Stdout io.Writer
	Stderr io.Writer
}

// Handle is an opaque reference to a started (or adopted) worker.
// Backends own the contents; callers treat it as a token to pass back
// into other Backend methods.
type Handle struct {
	// PID is the worker's process ID on its host. For LocalBackend
	// this is the OS PID; for remote backends it's the PID inside the
	// remote VM. Stable for the lifetime of the worker; reused by
	// adoption logic to reconnect to a worker across controller
	// restarts.
	PID int

	// Adopted is true iff the backend did NOT start this process —
	// it discovered an already-running PID via the adoption scan and
	// only has a PID to work with. Affects signaling semantics and
	// rules out exit-code reporting.
	Adopted bool

	// impl is backend-specific state (e.g. *exec.Cmd for LocalBackend).
	// Opaque to callers.
	impl any
}

// ExitInfo carries the result of a Wait. Code is nil for adopted
// workers (the kernel doesn't surface exit codes for processes we
// didn't fork). Err is non-nil only for transport-level failures —
// a worker that exited cleanly with a non-zero status returns Err=nil.
type ExitInfo struct {
	Code *int
	Err  error
}

// Backend abstracts the host on which workers run. Implementations
// must be safe for concurrent use by multiple goroutines.
type Backend interface {
	// Start launches a worker according to spec and returns a Handle
	// that callers use for subsequent lifecycle operations.
	Start(ctx context.Context, spec Spec) (*Handle, error)

	// Adopt produces a Handle for an already-running worker the
	// controller did NOT start (discovered via the adoption sentinel
	// scan). The returned Handle has Adopted=true; Wait on it polls
	// IsAlive instead of attaching to a child reaper.
	Adopt(pid int) *Handle

	// IsAlive reports whether the underlying process is still
	// running. Cheap; safe to call from polling loops.
	IsAlive(h *Handle) bool

	// Terminate asks the worker to shut down gracefully (POSIX SIGTERM
	// to the worker's process group; Windows: TerminateProcess, since
	// detached processes have no console for CTRL_BREAK). The call
	// returns immediately; observe completion via Wait or IsAlive.
	Terminate(h *Handle) error

	// Kill forces termination. Used as the grace-timer fallback.
	Kill(h *Handle) error

	// Wait blocks until the worker exits and returns its exit info.
	// For forked children this maps to cmd.Wait(); for adopted
	// handles it polls IsAlive at the implementation's discretion.
	// Wait MUST be safe to call exactly once per Handle.
	Wait(h *Handle) ExitInfo
}
