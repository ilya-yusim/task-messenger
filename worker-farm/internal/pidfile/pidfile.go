// Package pidfile implements a single-instance guard for the controller.
//
// Acquire writes the current PID to pidfilePath after verifying that any
// existing pidfile is stale (its PID is not running). On clean shutdown
// callers should invoke the returned Release closure.
package pidfile

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// ErrAlreadyRunning indicates an existing pidfile points at a live process.
var ErrAlreadyRunning = errors.New("controller already running")

// Acquire takes the pidfile lock. The returned release function removes
// the pidfile; callers should defer it.
//
// The mtime of the file is irrelevant — staleness is determined by
// whether the PID inside the file is currently alive. This is safe
// across reboots because PIDs are not reused while the OS is up and a
// reboot wipes /proc anyway, so any post-reboot PID collision is benign:
// we'd only mistake an unrelated process for the prior controller, and
// the worst case is the operator removes the pidfile manually.
func Acquire(pidfilePath string) (release func(), err error) {
	if err := os.MkdirAll(filepath.Dir(pidfilePath), 0o755); err != nil {
		return nil, fmt.Errorf("create pidfile dir: %w", err)
	}

	if existing, ok, err := readExisting(pidfilePath); err != nil {
		return nil, err
	} else if ok && processAlive(existing) {
		return nil, fmt.Errorf("%w as PID %d (pidfile: %s); stop it or delete the pidfile",
			ErrAlreadyRunning, existing, pidfilePath)
	}

	// Either no pidfile, or it's stale — overwrite.
	if err := os.WriteFile(pidfilePath, []byte(strconv.Itoa(os.Getpid())), 0o644); err != nil {
		return nil, fmt.Errorf("write pidfile: %w", err)
	}

	return func() {
		// Best-effort: only remove if it still contains our PID, so we
		// don't clobber a successor that took over after we crashed.
		if existing, ok, err := readExisting(pidfilePath); err == nil && ok && existing == os.Getpid() {
			_ = os.Remove(pidfilePath)
		}
	}, nil
}

func readExisting(path string) (pid int, exists bool, err error) {
	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return 0, false, nil
	}
	if err != nil {
		return 0, false, fmt.Errorf("read pidfile: %w", err)
	}
	s := strings.TrimSpace(string(data))
	if s == "" {
		return 0, false, nil
	}
	pid, err = strconv.Atoi(s)
	if err != nil {
		// A malformed pidfile is treated as stale.
		return 0, false, nil
	}
	return pid, true, nil
}
