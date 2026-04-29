// Package recent keeps an append-only log of spawn events at
// ~/.cache/tm-worker-controller/recent.json (JSONL, one record per
// line). Phase 1 tactical decision #9: ~20 LoC, saves a retro-fit.
package recent

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// Entry is one spawn event.
type Entry struct {
	Timestamp  time.Time `json:"timestamp"`
	Count      int       `json:"count"`
	Args       []string  `json:"args"`
	ConfigPath string    `json:"config_path"`
	WorkerBin  string    `json:"worker_bin"`
}

// Log appends entries to a JSONL file. Safe for concurrent use.
type Log struct {
	path string
	mu   sync.Mutex
}

// New constructs a Log writing to the given path.
func New(path string) *Log { return &Log{path: path} }

// Append writes one record. Errors are returned but most callers should
// just log them — losing a recent-runs entry is never fatal.
func (l *Log) Append(e Entry) error {
	l.mu.Lock()
	defer l.mu.Unlock()
	if err := os.MkdirAll(filepath.Dir(l.path), 0o755); err != nil {
		return err
	}
	f, err := os.OpenFile(l.path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	return json.NewEncoder(f).Encode(e)
}

// Latest reads the file and returns the most recent entry, or
// (Entry{}, false, nil) if no entries exist. Used by --restart-last.
func (l *Log) Latest() (Entry, bool, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	data, err := os.ReadFile(l.path)
	if err != nil {
		if os.IsNotExist(err) {
			return Entry{}, false, nil
		}
		return Entry{}, false, err
	}
	dec := json.NewDecoder(bytes.NewReader(data))
	var last Entry
	found := false
	for {
		var e Entry
		if err := dec.Decode(&e); err != nil {
			if errors.Is(err, io.EOF) {
				break
			}
			return Entry{}, false, err
		}
		last = e
		found = true
	}
	return last, found, nil
}
