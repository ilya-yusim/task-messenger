//go:build windows

package local

import (
	"fmt"
	"os/exec"
	"syscall"
)

// configureProc detaches the child from the controller's console so
// closing the controller window does NOT propagate CTRL_CLOSE_EVENT to
// the worker. Workers are designed to outlive the controller; the next
// controller launch picks them up via the adoption sentinel scan.
//
// Trade-off: a detached process has no console, so
// GenerateConsoleCtrlEvent / CTRL_BREAK cannot reach it. Stop is
// therefore TerminateProcess on Windows for both forked and adopted
// workers — same path either way. Graceful in-process shutdown on
// Windows would require a Job Object + a side-channel signal (named
// pipe or shared event), which is Phase 4 territory.
//
//	DETACHED_PROCESS         = 0x00000008
//	CREATE_NEW_PROCESS_GROUP = 0x00000200 (kept defensively; harmless
//	                           under DETACHED_PROCESS).
func configureProc(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{
		CreationFlags: 0x00000008 | 0x00000200,
	}
}

// terminateProc on Windows is now equivalent to killProc: the worker
// has no console attached, so console-control events can't reach it.
// Kept as a separate symbol so the manager's gracefulKill flow stays
// platform-neutral.
func terminateProc(cmd *exec.Cmd) error {
	return killProc(cmd)
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

// terminatePID for an adopted worker on Windows is necessarily a hard
// terminate: we did NOT fork the child, so it is not in our console
// process group, so GenerateConsoleCtrlEvent can't reach it. Plan
// trade-off (see worker-farm-controller-plan.md → Adopted-worker
// semantics): Stop on Windows-adopted is effectively SIGKILL.
func terminatePID(pid int) error {
	return killPID(pid)
}

// killPID opens the process and TerminateProcess's it. Mirrors what
// `cmd.Process.Kill()` does internally.
func killPID(pid int) error {
	if pid <= 0 {
		return nil
	}
	const processTerminate = 0x0001
	h, err := syscall.OpenProcess(processTerminate, false, uint32(pid))
	if err != nil {
		return fmt.Errorf("OpenProcess(%d): %w", pid, err)
	}
	defer syscall.CloseHandle(h)
	if err := syscall.TerminateProcess(h, 1); err != nil {
		return fmt.Errorf("TerminateProcess(%d): %w", pid, err)
	}
	return nil
}
