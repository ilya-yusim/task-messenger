//go:build !windows

package local

import (
	"os/exec"
	"syscall"
)

// configureProc puts the child in its own process group so we can
// signal the whole subtree at once without taking the controller down.
func configureProc(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
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
