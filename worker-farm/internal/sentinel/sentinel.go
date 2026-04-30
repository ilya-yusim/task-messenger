// Package sentinel writes the per-worker `worker-NN.adopt` JSON file
// next to each spawned worker's log. Slice 3 of Phase 2 will read these
// at controller startup to classify orphans into auto-adopt vs
// quarantine; Slice 2 just emits them.
package sentinel

import (
	"encoding/json"
	"os"
	"path/filepath"
)

// Sentinel is the on-disk schema. Kept small and fully self-describing
// so the file alone tells the next controller everything it needs to
// decide whether the PID is "ours" and how to attach.
type Sentinel struct {
	ControllerID  string   `json:"controller_id"`
	StartedAtUnix int64    `json:"started_at_unix"`
	PID           int      `json:"pid"`
	WorkerBin     string   `json:"worker_bin"`
	Args          []string `json:"args"`
	LogPath       string   `json:"log_path"`
	ConfigPath    string   `json:"config_path"`
	RunID         string   `json:"run_id"`
	Slot          int      `json:"slot"`
}

// Write atomically writes s to path.
func Write(path string, s *Sentinel) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// Read parses the sentinel JSON at path. Errors are non-fatal at the
// call site (Slice 3 startup adoption logs and skips bad files).
func Read(path string) (*Sentinel, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var s Sentinel
	if err := json.Unmarshal(data, &s); err != nil {
		return nil, err
	}
	return &s, nil
}
