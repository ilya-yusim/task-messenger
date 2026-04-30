// Package manifest renders and atomically persists a per-run
// manifest.json. Schema is a superset of what
// `extras/scripts/start_workers_local.{ps1,sh}` write so the
// controller and the shell scripts can read each other's runs.
//
// Key/value naming follows the script for the shared subset
// (`run_id`, `started_at`, `host`, `hostname`, `os`, `base_dir`,
// `worker_bin`, `config`, `args`, `workers[]{id,pid,log,pidfile}`).
// Controller-specific extensions (`controller_id`, per-worker
// `worker_id`/`state`/`stopped_at`/`exit_code`/`args`/`last_error`)
// are added without breaking the script's reader.
package manifest

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// Manifest is the JSON-serialisable per-run state.
type Manifest struct {
	RunID        string    `json:"run_id"`
	StartedAt    time.Time `json:"started_at"`
	Host         string    `json:"host"`     // "local" — matches the script's literal
	Hostname     string    `json:"hostname"` // os.Hostname()
	OS           string    `json:"os"`       // "linux" / "darwin" / "windows"
	BaseDir      string    `json:"base_dir"`
	WorkerBin    string    `json:"worker_bin"`
	Config       string    `json:"config"`
	Args         []string  `json:"args"`
	Workers      []Worker  `json:"workers"`
	ControllerID string    `json:"controller_id"`
}

// Worker is the per-worker entry in `workers[]`. The first four
// fields (`id`, `pid`, `log`, `pidfile`) match the shell-script
// schema verbatim; the rest are controller-only superset fields.
type Worker struct {
	ID        string     `json:"id"` // zero-padded slot number, e.g. "01"
	PID       int        `json:"pid"`
	Log       string     `json:"log"`
	Pidfile   string     `json:"pidfile"`
	WorkerID  string     `json:"worker_id"`
	State     string     `json:"state"`
	StartedAt time.Time  `json:"started_at"`
	StoppedAt *time.Time `json:"stopped_at,omitempty"`
	ExitCode  *int       `json:"exit_code,omitempty"`
	Args      []string   `json:"args"`
	LastError string     `json:"last_error,omitempty"`
}

// Write atomically replaces path with a JSON-encoded `m`. Uses
// `os.Rename` after a sibling `.tmp` write so a crash mid-write
// can't leave a half-encoded manifest behind.
func Write(path string, m *Manifest) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		return fmt.Errorf("rename %s -> %s: %w", tmp, path, err)
	}
	return nil
}
