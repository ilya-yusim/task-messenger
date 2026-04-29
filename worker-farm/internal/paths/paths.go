// Package paths centralises the OS-specific directory and file paths used
// by the worker-farm controller. Keeping every "where do we put things"
// decision in one file makes Phase 2 persistence and cross-platform
// behaviour easier to audit.
package paths

import (
	"os"
	"path/filepath"
	"runtime"
)

// CacheDir returns the directory the controller uses for runtime state:
// per-worker logs, the controller pidfile, the recent-runs file. The
// directory is NOT created here; callers should mkdir-p when needed.
//
//	POSIX:   $XDG_CACHE_HOME/tm-worker-controller  or  ~/.cache/tm-worker-controller
//	Windows: %LOCALAPPDATA%\tm-worker-controller
func CacheDir() (string, error) {
	if runtime.GOOS == "windows" {
		base := os.Getenv("LOCALAPPDATA")
		if base == "" {
			home, err := os.UserHomeDir()
			if err != nil {
				return "", err
			}
			base = filepath.Join(home, "AppData", "Local")
		}
		return filepath.Join(base, "tm-worker-controller"), nil
	}
	if base := os.Getenv("XDG_CACHE_HOME"); base != "" {
		return filepath.Join(base, "tm-worker-controller"), nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".cache", "tm-worker-controller"), nil
}

// DefaultWorkerConfig returns the path that the makeself .run installer
// writes config-worker.json to. The controller defaults to passing this
// to every spawned worker via -c <path>.
//
//	POSIX:   ~/.config/task-messenger/tm-worker/config-worker.json
//	Windows: %APPDATA%\task-messenger\tm-worker\config-worker.json
func DefaultWorkerConfig() (string, error) {
	if runtime.GOOS == "windows" {
		base := os.Getenv("APPDATA")
		if base == "" {
			home, err := os.UserHomeDir()
			if err != nil {
				return "", err
			}
			base = filepath.Join(home, "AppData", "Roaming")
		}
		return filepath.Join(base, "task-messenger", "tm-worker", "config-worker.json"), nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".config", "task-messenger", "tm-worker", "config-worker.json"), nil
}

// DefaultWorkerBinFallbacks returns the platform-specific final fallback
// locations checked by worker.Resolve when neither --worker-bin nor $PATH
// produced a result. Order matters: first match wins.
func DefaultWorkerBinFallbacks() []string {
	if runtime.GOOS == "windows" {
		base := os.Getenv("LOCALAPPDATA")
		if base == "" {
			home, _ := os.UserHomeDir()
			base = filepath.Join(home, "AppData", "Local")
		}
		return []string{
			filepath.Join(base, "Programs", "tm-worker", "bin", "tm-worker.exe"),
		}
	}
	home, _ := os.UserHomeDir()
	return []string{
		filepath.Join(home, ".local", "bin", "tm-worker"),
	}
}

// PidfilePath is the controller's single-instance guard file.
func PidfilePath(cacheDir string) string {
	return filepath.Join(cacheDir, "controller.pid")
}

// WorkerLogPath is the destination for a single worker's combined
// stdout+stderr capture.
func WorkerLogPath(cacheDir, workerID string) string {
	return filepath.Join(cacheDir, workerID+".log")
}

// RecentRunsPath is the JSONL file the controller appends to on every
// spawn, used by --restart-last (Phase 1) and the future "recent runs"
// UI list (Phase 2).
func RecentRunsPath(cacheDir string) string {
	return filepath.Join(cacheDir, "recent.json")
}

// ControllerLogPath is the default --log-file destination: a single
// rolling file in the cache dir capturing the controller's stderr
// trail (spawn / stop / exit access log + startup banner).
func ControllerLogPath(cacheDir string) string {
	return filepath.Join(cacheDir, "controller.log")
}
