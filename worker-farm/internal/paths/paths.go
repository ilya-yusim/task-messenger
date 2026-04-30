// Package paths centralises the OS-specific directory and file paths used
// by the worker-farm controller. Keeping every "where do we put things"
// decision in one file makes Phase 2 persistence and cross-platform
// behaviour easier to audit.
package paths

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
)

// CacheDir returns the directory the controller uses for runtime state:
// per-run worker logs and manifests, the controller pidfile, the
// recent-runs file, and the controller log. The directory is NOT
// created here; callers should mkdir-p when needed.
//
//	POSIX:   $XDG_CACHE_HOME/tm-worker-farm  or  ~/.cache/tm-worker-farm
//	Windows: %LOCALAPPDATA%\tm-worker-farm
func CacheDir() (string, error) {
	return cacheDirNamed("tm-worker-farm")
}

// LegacyCacheDir returns the pre-Phase-2 location
// (`tm-worker-controller`). The controller checks for its existence at
// startup and warns the operator if it still has data. There is no
// automatic migration.
func LegacyCacheDir() (string, error) {
	return cacheDirNamed("tm-worker-controller")
}

func cacheDirNamed(leaf string) (string, error) {
	if runtime.GOOS == "windows" {
		base := os.Getenv("LOCALAPPDATA")
		if base == "" {
			home, err := os.UserHomeDir()
			if err != nil {
				return "", err
			}
			base = filepath.Join(home, "AppData", "Local")
		}
		return filepath.Join(base, leaf), nil
	}
	if base := os.Getenv("XDG_CACHE_HOME"); base != "" {
		return filepath.Join(base, leaf), nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".cache", leaf), nil
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

// IdentityPath is the persistent controller-id file. The id stored
// here survives controller restarts so orphan adoption can decide
// whether a `worker-NN.adopt` sentinel was written by *this* logical
// controller (and is therefore safe to auto-adopt) or by another
// install (quarantine).
func IdentityPath(cacheDir string) string {
	return filepath.Join(cacheDir, "identity.json")
}

// RunsDir is the parent directory under which each run's per-run
// directory lives. Mirrors the layout used by
// `extras/scripts/start_workers_local.{ps1,sh}`.
func RunsDir(cacheDir string) string {
	return filepath.Join(cacheDir, "runs")
}

// LatestRunPath is the cross-driver "what was the most recent run"
// pointer that the shell scripts also write. Plain text, single line:
// the run-id.
func LatestRunPath(cacheDir string) string {
	return filepath.Join(RunsDir(cacheDir), "latest.txt")
}

// RunDir returns the directory for a single run's state
// (`manifest.json`, per-worker logs, adoption sentinels).
func RunDir(cacheDir, runID string) string {
	return filepath.Join(RunsDir(cacheDir), runID)
}

// RunManifestPath is the per-run write-through state file the
// controller maintains. Schema is a superset of what
// `start_workers_local.{ps1,sh}` write so the two drivers can read
// each other's runs.
func RunManifestPath(runDir string) string {
	return filepath.Join(runDir, "manifest.json")
}

// WorkerSlotLogPath returns the log destination for the Nth worker in a
// run, named `worker-NN.log` (1-based, zero-padded to two digits to
// match the shell scripts).
func WorkerSlotLogPath(runDir string, slot int) string {
	return filepath.Join(runDir, fmt.Sprintf("worker-%02d.log", slot))
}

// WorkerSlotPidPath is the per-worker pidfile, written for
// compatibility with `stop_workers_local.{ps1,sh} -r <run-id>`.
func WorkerSlotPidPath(runDir string, slot int) string {
	return filepath.Join(runDir, fmt.Sprintf("worker-%02d.pid", slot))
}

// WorkerSlotSentinelPath is the per-worker adoption sentinel
// (`worker-NN.adopt`). See internal/sentinel for the schema.
func WorkerSlotSentinelPath(runDir string, slot int) string {
	return filepath.Join(runDir, fmt.Sprintf("worker-%02d.adopt", slot))
}

// RecentRunsPath is the JSONL file the controller appends to on every
// spawn, used by --restart-last and the future "recent runs" UI list.
func RecentRunsPath(cacheDir string) string {
	return filepath.Join(cacheDir, "recent.json")
}

// ControllerLogPath is the default --log-file destination: a single
// rolling file in the cache dir capturing the controller's stderr
// trail (spawn / stop / exit access log + startup banner).
func ControllerLogPath(cacheDir string) string {
	return filepath.Join(cacheDir, "controller.log")
}
