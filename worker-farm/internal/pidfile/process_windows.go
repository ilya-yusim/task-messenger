//go:build windows

package pidfile

import (
	"syscall"
)

// processAlive opens the process with PROCESS_QUERY_LIMITED_INFORMATION
// and checks its exit code. Any "still active" answer means alive.
// Failure to open is treated as not-alive (most commonly an invalid PID),
// except for ACCESS_DENIED which we conservatively treat as alive.
func processAlive(pid int) bool {
	if pid <= 0 {
		return false
	}
	const processQueryLimitedInformation = 0x1000
	h, err := syscall.OpenProcess(processQueryLimitedInformation, false, uint32(pid))
	if err != nil {
		if errno, ok := err.(syscall.Errno); ok && errno == syscall.ERROR_ACCESS_DENIED {
			return true
		}
		return false
	}
	defer syscall.CloseHandle(h)

	var code uint32
	if err := syscall.GetExitCodeProcess(h, &code); err != nil {
		return true
	}
	const stillActive = 259 // STILL_ACTIVE
	return code == stillActive
}
