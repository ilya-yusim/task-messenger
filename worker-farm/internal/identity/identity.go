// Package identity manages the controller's persistent ID and the
// rolling history of IDs this install has ever used. The current ID
// is generated once on first launch and reused across restarts; the
// history list lets a future startup recognise its own past workers
// (auto-adopt) versus workers another install left running on the
// same machine (quarantine).
package identity

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// File is the on-disk schema. Kept tiny on purpose; future fields go
// here rather than in a sidecar so a single load returns everything.
//
// `controller_id` is the *current* identity (the one written into new
// worker sentinels). `previous_ids` is every ID this install has ever
// used, including the current one — duplicated so adoption checks are
// a single set lookup. The redundancy costs ~16 bytes; the simpler
// invariant is worth it.
type File struct {
	ControllerID string    `json:"controller_id"`
	CreatedAt    time.Time `json:"created_at"`
	PreviousIDs  []string  `json:"previous_ids,omitempty"`
}

// LoadOrCreate returns the controller ID at path, generating and
// persisting a new one if the file is missing or unreadable. A second
// return value reports whether the ID was freshly minted (useful for
// startup logging).
//
// Side effect: if the on-disk file lacks the current ID in
// `previous_ids`, this call rewrites the file with it appended. This
// keeps the invariant ("current is always in history") true for
// installs that pre-date Slice 3 without making callers do the upgrade.
func LoadOrCreate(path string) (string, bool, error) {
	if data, err := os.ReadFile(path); err == nil {
		var f File
		if jerr := json.Unmarshal(data, &f); jerr == nil && f.ControllerID != "" {
			if !contains(f.PreviousIDs, f.ControllerID) {
				f.PreviousIDs = append(f.PreviousIDs, f.ControllerID)
				if werr := writeAtomic(path, f); werr != nil {
					return "", false, fmt.Errorf("upgrade %s: %w", path, werr)
				}
			}
			return f.ControllerID, false, nil
		}
		// Corrupt or empty file: regenerate. Keep going rather than
		// failing the controller startup over a single rogue file.
	} else if !errors.Is(err, os.ErrNotExist) {
		return "", false, fmt.Errorf("read %s: %w", path, err)
	}

	id, err := mintID()
	if err != nil {
		return "", false, err
	}
	f := File{ControllerID: id, CreatedAt: time.Now().UTC(), PreviousIDs: []string{id}}
	if err := writeAtomic(path, f); err != nil {
		return "", false, fmt.Errorf("persist %s: %w", path, err)
	}
	return id, true, nil
}

// Load returns the current ID and the full history (current included).
// `ok` is false if the file is missing or empty — callers should treat
// that as "no history yet" and behave conservatively (no auto-adopt).
func Load(path string) (current string, history []string, ok bool, err error) {
	data, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return "", nil, false, nil
		}
		return "", nil, false, fmt.Errorf("read %s: %w", path, err)
	}
	var f File
	if jerr := json.Unmarshal(data, &f); jerr != nil {
		return "", nil, false, fmt.Errorf("parse %s: %w", path, jerr)
	}
	if f.ControllerID == "" {
		return "", nil, false, nil
	}
	hist := f.PreviousIDs
	if !contains(hist, f.ControllerID) {
		hist = append(hist, f.ControllerID)
	}
	return f.ControllerID, hist, true, nil
}

func mintID() (string, error) {
	var b [8]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", fmt.Errorf("crypto/rand: %w", err)
	}
	return "ctl-" + hex.EncodeToString(b[:]), nil
}

func contains(list []string, s string) bool {
	for _, e := range list {
		if e == s {
			return true
		}
	}
	return false
}

func writeAtomic(path string, f File) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(f, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}
