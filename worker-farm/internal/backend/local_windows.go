//go:build windows

package backend

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
// pipe or shared event), which is a future enhancement.
//
//	DETACHED_PROCESS         = 0x00000008
//	CREATE_NEW_PROCESS_GROUP = 0x00000200 (kept defensively; harmless
//	                           under DETACHED_PROCESS).
func configureProc(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{
		CreationFlags: 0x00000008 | 0x00000200,
	}
}

// terminateProc on Windows is equivalent to killProc: the worker has
// no console attached, so console-control events can't reach it.
// Kept as a separate symbol so the backend's Terminate flow stays
// platform-neutral.
func terminateProc(cmd *exec.Cmd) error {
	return killProc(cmd)
}

// killProc terminates the child. exec.Cmd.Process.Kill maps to
// TerminateProcess on Windows, which is the right SIGKILL fallback.
// Does not kill descendants — a future enhancement could swap this
// for a Job Object that auto-terminates the whole tree alongside
// resource limits.
func killProc(cmd *exec.Cmd) error {
	if cmd.Process == nil {
		return nil
	}
	return cmd.Process.Kill()
}

// terminatePID for an adopted worker on Windows is necessarily a hard
// terminate: we did NOT fork the child, so it is not in our console
// process group, so GenerateConsoleCtrlEvent can't reach it.
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

// isAlive checks whether the given PID is currently a running process.
// Uses OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) +
// GetExitCodeProcess: alive iff exit code is STILL_ACTIVE (259).
// "OpenProcess succeeded → alive" is wrong because the kernel keeps
// the process object around for a while after exit.
func isAlive(pid int) bool {
	if pid <= 0 {
		return false
	}
	const (
		processQueryLimitedInformation = 0x1000
		stillActive                    = 259
	)
	h, err := syscall.OpenProcess(processQueryLimitedInformation, false, uint32(pid))
	if err != nil {
		return false
	}
	defer syscall.CloseHandle(h)
	var code uint32
	if err := syscall.GetExitCodeProcess(h, &code); err != nil {
		return false
	}
	return code == stillActive
}
