//go:build windows

package liveness

import (
	"syscall"
)

// IsAlive checks whether the given PID is currently a running
// process. Uses OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) +
// GetExitCodeProcess: if the exit code is STILL_ACTIVE (259) the
// process is live; anything else means it has terminated. Falling
// back to "OpenProcess succeeded → alive" is wrong because the
// kernel keeps the process object around for a while after exit.
//
// PROCESS_QUERY_LIMITED_INFORMATION (0x1000) requires only that we
// be the same user (or have SeDebugPrivilege), which matches the
// adoption use case — we won't try to manage processes owned by
// another account.
func IsAlive(pid int) bool {
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
