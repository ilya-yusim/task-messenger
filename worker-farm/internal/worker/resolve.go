// Package worker locates the tm-worker binary at controller startup,
// using the same precedence as the shell scripts in extras/scripts/.
package worker

import (
	"errors"
	"fmt"
	"os/exec"
	"runtime"
	"strings"

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/paths"
)

// ErrNotFound is returned by Resolve when no candidate is an executable file.
var ErrNotFound = errors.New("tm-worker binary not found")

// Resolve returns the absolute path to a tm-worker executable using the
// precedence documented in the plan (Tactical Decision #6):
//
//  1. explicit override (-worker-bin)
//  2. $PATH lookup
//  3. platform fallback(s) under the user's home
//
// The returned error wraps ErrNotFound and includes every candidate that
// was checked, so the operator gets an actionable failure message.
func Resolve(override string) (string, error) {
	var checked []string

	if override != "" {
		path, err := exec.LookPath(override)
		if err == nil {
			return path, nil
		}
		checked = append(checked, fmt.Sprintf("--worker-bin %s (%v)", override, err))
	}

	name := "tm-worker"
	if runtime.GOOS == "windows" {
		name = "tm-worker.exe"
	}
	if path, err := exec.LookPath(name); err == nil {
		return path, nil
	}
	checked = append(checked, fmt.Sprintf("$PATH lookup for %s", name))

	for _, candidate := range paths.DefaultWorkerBinFallbacks() {
		if _, err := exec.LookPath(candidate); err == nil {
			return candidate, nil
		}
		checked = append(checked, candidate)
	}

	return "", fmt.Errorf("%w; checked: %s", ErrNotFound, strings.Join(checked, "; "))
}
