// Package adopt scans on-disk worker sentinels left behind by previous
// controller runs (or by the shell-script driver) and classifies each
// one for the startup adoption pass.
//
// Classification is the whole point of this package; everything else
// (registration, supervision, kill) lives elsewhere. See
// `.github/prompts/worker-farm-controller-plan.md` ("Adoption side-channel")
// for the policy.
package adopt

import (
	"errors"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"strings"

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/backend"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/paths"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/sentinel"
)

// Class is the bucket a sentinel falls into after liveness + history
// checks.
type Class int

const (
	// ClassMine: sentinel's controller_id is in this install's history
	// AND the PID is alive. Auto-adopt at startup.
	ClassMine Class = iota
	// ClassTheirs: PID is alive but controller_id is NOT in history.
	// Surfaced in the quarantine list; operator decides.
	ClassTheirs
	// ClassStale: PID is dead. Treated as a normal exit with code=null
	// (we can't know what code the kernel reported) and folded into
	// the registry as an exited row so the operator's history is intact.
	ClassStale
)

// Candidate is one classified sentinel.
type Candidate struct {
	Class       Class
	Path        string // absolute path to worker-NN.adopt
	RunDir      string // parent dir
	Sentinel    *sentinel.Sentinel
	HistoryHit  bool   // controller_id was in our history
	Alive       bool   // liveness probe at scan time
	HistoryNote string // human-readable explanation, surfaced in logs/UI
}

// Key uniquely identifies a Candidate for HTTP routing of quarantine
// actions. Format: "<run-id>/<NN>". Parseable by SplitKey().
func (c *Candidate) Key() string {
	return filepath.Base(c.RunDir) + "/" + slotSuffix(c.Path)
}

// SplitKey reverses Candidate.Key. Returns ("","") on malformed input.
func SplitKey(key string) (runID, slot string) {
	parts := strings.SplitN(key, "/", 2)
	if len(parts) != 2 {
		return "", ""
	}
	return parts[0], parts[1]
}

// slotSuffix turns ".../worker-03.adopt" into "03".
func slotSuffix(p string) string {
	base := filepath.Base(p)
	base = strings.TrimSuffix(base, ".adopt")
	base = strings.TrimPrefix(base, "worker-")
	return base
}

// Scan walks every `runs/*/worker-*.adopt` under cacheDir, reads each
// sentinel, runs liveness + history-membership checks, and returns the
// classified set. Errors reading individual sentinels are logged and
// skipped — a single corrupt file should never fail the whole startup
// pass.
//
// `historyIDs` is the slice from identity.Load(); empty means
// "no history" and every alive sentinel is classified as theirs.
func Scan(cacheDir string, historyIDs []string) ([]Candidate, error) {
	known := make(map[string]struct{}, len(historyIDs))
	for _, id := range historyIDs {
		known[id] = struct{}{}
	}

	runsDir := paths.RunsDir(cacheDir)
	entries, err := os.ReadDir(runsDir)
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			return nil, nil // first launch ever
		}
		return nil, err
	}

	var out []Candidate
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		runDir := filepath.Join(runsDir, e.Name())
		matches, _ := filepath.Glob(filepath.Join(runDir, "worker-*.adopt"))
		for _, p := range matches {
			c, ok := classifyOne(p, runDir, known)
			if !ok {
				continue
			}
			out = append(out, c)
		}
	}
	return out, nil
}

func classifyOne(path, runDir string, known map[string]struct{}) (Candidate, bool) {
	s, err := sentinel.Read(path)
	if err != nil {
		log.Printf("adopt: skip unreadable sentinel %s: %v", path, err)
		return Candidate{}, false
	}
	if s.PID <= 0 {
		log.Printf("adopt: skip sentinel with invalid pid %d at %s", s.PID, path)
		return Candidate{}, false
	}
	alive := backend.IsAliveLocal(s.PID)
	_, hit := known[s.ControllerID]

	c := Candidate{
		Path:       path,
		RunDir:     runDir,
		Sentinel:   s,
		HistoryHit: hit,
		Alive:      alive,
	}
	switch {
	case !alive:
		c.Class = ClassStale
		c.HistoryNote = "PID is no longer running"
	case hit:
		c.Class = ClassMine
		c.HistoryNote = "controller_id matches identity history"
	default:
		c.Class = ClassTheirs
		c.HistoryNote = "controller_id not in identity history"
	}
	return c, true
}
