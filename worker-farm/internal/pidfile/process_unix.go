//go:build !windows

package pidfile

import (
	"errors"
	"os"
	"syscall"
)

// processAlive returns true if a process with the given PID exists and
// is reachable by signal 0 (POSIX semantics). syscall.ESRCH means
// "no such process". EPERM means the process exists but we lack
// permission to signal it — treat that as alive (better safe).
func processAlive(pid int) bool {
	if pid <= 0 {
		return false
	}
	proc, err := os.FindProcess(pid)
	if err != nil {
		return false
	}
	err = proc.Signal(syscall.Signal(0))
	if err == nil {
		return true
	}
	return !errors.Is(err, syscall.ESRCH) && !errors.Is(err, os.ErrProcessDone)
}
