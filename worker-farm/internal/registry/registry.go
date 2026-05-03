// Package registry holds the in-memory worker table.
package registry

import (
	"encoding/json"
	"sort"
	"sync"
	"time"
)

// State enumerates the lifecycle stages a worker can be in.
type State string

const (
	StateStarting State = "starting"
	StateRunning  State = "running"
	StateStopping State = "stopping"
	StateExited   State = "exited"
)

// Worker is the public, JSON-serialisable view of a tracked worker.
//
// Fields that change after creation are guarded by Registry.mu — never
// mutate a Worker directly; go through Registry.Update.
type Worker struct {
	ID    string `json:"id"`
	PID   int    `json:"pid"`
	State State  `json:"state"`
	// RunID groups workers that were spawned together by one
	// `POST /workers` (or `--restart-last`) call. Format is
	// `YYYYMMDD-HHMMSS` with a `-N` collision suffix; matches the
	// convention in `extras/scripts/start_workers_local.{ps1,sh}`.
	RunID string `json:"run_id"`
	// Slot is the 1-based ordinal of this worker within its run. Used
	// for the on-disk `worker-NN.log` filename and for the future
	// manifest-driven CLI/script driver.
	Slot      int        `json:"slot"`
	StartedAt time.Time  `json:"started_at"`
	StoppedAt *time.Time `json:"stopped_at,omitempty"`
	ExitCode  *int       `json:"exit_code,omitempty"`
	Args      []string   `json:"args"`
	LogPath   string     `json:"log_path"`
	LastError string     `json:"last_error,omitempty"`
	Host      string     `json:"host"`
	// Adopted is true if this worker was discovered at startup via a
	// `worker-NN.adopt` sentinel rather than fork-spawned by this
	// controller. Adopted workers have no parent/child relationship
	// with the controller: their lifecycle is observed via liveness
	// polling and their exit codes are unobtainable (kernel only
	// reports those to the parent). The UI uses this to render
	// "exited (orphan)" once the poll detects exit.
	Adopted bool `json:"adopted,omitempty"`
}

// Registry is a concurrent map of worker ID → Worker. All mutations
// happen through methods so callers can't forget to hold the lock.
type Registry struct {
	mu      sync.RWMutex
	workers map[string]*Worker
}

// New returns an empty registry.
func New() *Registry {
	return &Registry{workers: make(map[string]*Worker)}
}

// Add inserts a worker. Returns false if the ID already exists.
func (r *Registry) Add(w *Worker) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, exists := r.workers[w.ID]; exists {
		return false
	}
	r.workers[w.ID] = w
	return true
}

// Get returns a snapshot copy of the worker, or (nil, false).
func (r *Registry) Get(id string) (*Worker, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	w, ok := r.workers[id]
	if !ok {
		return nil, false
	}
	cp := *w
	return &cp, true
}

// Update applies fn to the live worker under the write lock. Returns
// false if the worker doesn't exist. fn must not block.
func (r *Registry) Update(id string, fn func(*Worker)) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	w, ok := r.workers[id]
	if !ok {
		return false
	}
	fn(w)
	return true
}

// Remove deletes a worker from the registry. Returns false if it
// didn't exist. Used by the "purge" action to forget exited workers.
func (r *Registry) Remove(id string) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, ok := r.workers[id]; !ok {
		return false
	}
	delete(r.workers, id)
	return true
}

// List returns a snapshot slice ordered by StartedAt ascending. Stable
// ordering keeps the UI table from jumping around between polls.
func (r *Registry) List() []Worker {
	r.mu.RLock()
	defer r.mu.RUnlock()
	out := make([]Worker, 0, len(r.workers))
	for _, w := range r.workers {
		out = append(out, *w)
	}
	sort.Slice(out, func(i, j int) bool {
		return out[i].StartedAt.Before(out[j].StartedAt)
	})
	return out
}

// MarshalJSON makes Registry directly encodable as a JSON array.
func (r *Registry) MarshalJSON() ([]byte, error) {
	return json.Marshal(r.List())
}
