//go:build !windows

package liveness

import (
	"errors"
	"os"
	"syscall"
)

// IsAlive returns true if a process with the given PID exists and is
// reachable by this user. It does NOT distinguish "running" from
// "zombie" — that distinction doesn't matter for adoption decisions
// (a zombie still occupies the PID and will eventually be reaped).
//
// Implementation: os.FindProcess always succeeds on POSIX, so we
// follow up with signal 0, which performs the kernel's permission
// check without delivering a signal.
func IsAlive(pid int) bool {
	if pid <= 0 {
		return false
	}
	p, err := os.FindProcess(pid)
	if err != nil {
		return false
	}
	if err := p.Signal(syscall.Signal(0)); err != nil {
		// ESRCH = no such process. EPERM = exists but we can't signal
		// it (different user); count that as alive — refusing to
		// adopt is the right move, but it isn't "stale".
		if errors.Is(err, syscall.ESRCH) {
			return false
		}
		return true
	}
	return true
}
