//go:build windows

package local

import (
	"fmt"
	"os/exec"
	"syscall"
)

// configureProc creates the child in its own process group so a
// CTRL_BREAK_EVENT signals only it (and its descendants), not the
// controller. CREATE_NEW_PROCESS_GROUP = 0x00000200.
func configureProc(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{
		CreationFlags: 0x00000200, // CREATE_NEW_PROCESS_GROUP
	}
}

// terminateProc asks Windows to deliver CTRL_BREAK to the child's
// process group. tm-worker registers SIGINT/SIGTERM handlers via the
// usual Go-style signal package; CTRL_BREAK is what reaches a
// console-attached child started with CREATE_NEW_PROCESS_GROUP.
func terminateProc(cmd *exec.Cmd) error {
	if cmd.Process == nil {
		return nil
	}
	dll, err := syscall.LoadDLL("kernel32.dll")
	if err != nil {
		return err
	}
	defer dll.Release()
	proc, err := dll.FindProc("GenerateConsoleCtrlEvent")
	if err != nil {
		return err
	}
	const ctrlBreakEvent = 1
	r1, _, callErr := proc.Call(uintptr(ctrlBreakEvent), uintptr(cmd.Process.Pid))
	if r1 == 0 {
		return fmt.Errorf("GenerateConsoleCtrlEvent: %w", callErr)
	}
	return nil
}

// killProc terminates the child. exec.Cmd.Process.Kill maps to
// TerminateProcess on Windows, which is fine as a SIGKILL fallback.
// It does not kill descendants — Phase 2 will swap this for a Job
// Object that auto-terminates the whole tree.
func killProc(cmd *exec.Cmd) error {
	if cmd.Process == nil {
		return nil
	}
	return cmd.Process.Kill()
}
