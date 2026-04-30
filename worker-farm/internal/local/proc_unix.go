//go:build !windows

package local

import (
	"os/exec"
	"syscall"
)

// configureProc puts the child in its own session (and therefore its
// own process group, with no controlling terminal) so it survives the
// controller exiting or its terminal closing. Setsid implies a new
// process-group leader whose pgid == pid, which is what
// terminateProc/killProc rely on when they send signals to -pid.
func configureProc(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}
}

// terminateProc sends SIGTERM to the child's process group.
func terminateProc(cmd *exec.Cmd) error {
	if cmd.Process == nil {
		return nil
	}
	return syscall.Kill(-cmd.Process.Pid, syscall.SIGTERM)
}

// killProc sends SIGKILL to the child's process group.
func killProc(cmd *exec.Cmd) error {
	if cmd.Process == nil {
		return nil
	}
	return syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
}

// terminatePID sends SIGTERM to a single PID we did NOT fork (an
// adopted orphan). We can't signal the whole process group because we
// don't own it; tm-worker handles SIGTERM cleanly so this is enough.
func terminatePID(pid int) error {
	if pid <= 0 {
		return nil
	}
	return syscall.Kill(pid, syscall.SIGTERM)
}

// killPID is the SIGKILL counterpart of terminatePID for adopted workers.
func killPID(pid int) error {
	if pid <= 0 {
		return nil
	}
	return syscall.Kill(pid, syscall.SIGKILL)
}
