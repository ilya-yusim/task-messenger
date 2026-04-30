// Package api wires the controller's HTTP surface.
package api

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/adopt"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/local"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/logbuf"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/recent"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/registry"
)

// Server is the HTTP handler set for the controller.
type Server struct {
	mux     *http.ServeMux
	webFS   fs.FS
	noCache bool
	reg     *registry.Registry
	mgr     *local.Manager
	rec     *recent.Log
	cfgPath string
	binPath string
}

// Options configures Server.
type Options struct {
	WebFS      fs.FS
	Registry   *registry.Registry
	Manager    *local.Manager
	Recent     *recent.Log
	ConfigPath string
	WorkerBin  string
}

// New constructs a Server.
func New(opts Options) *Server {
	s := &Server{
		mux:     http.NewServeMux(),
		webFS:   opts.WebFS,
		noCache: true, // Tactical Decision #8
		reg:     opts.Registry,
		mgr:     opts.Manager,
		rec:     opts.Recent,
		cfgPath: opts.ConfigPath,
		binPath: opts.WorkerBin,
	}
	s.routes()
	return s
}

// Handler exposes the underlying mux.
func (s *Server) Handler() http.Handler { return s.mux }

func (s *Server) routes() {
	s.mux.HandleFunc("/healthz", s.handleHealthz)
	s.mux.HandleFunc("/workers", s.handleWorkers)
	s.mux.HandleFunc("/workers/", s.handleWorkerByID)
	s.mux.HandleFunc("/quarantine", s.handleQuarantineList)
	s.mux.HandleFunc("/quarantine/", s.handleQuarantineAct)
	if s.webFS != nil {
		s.mux.Handle("/", s.staticHandler())
	}
}

func (s *Server) handleHealthz(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write([]byte("ok\n"))
}

func (s *Server) staticHandler() http.Handler {
	fileServer := http.FileServer(http.FS(s.webFS))
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if s.noCache {
			w.Header().Set("Cache-Control", "no-store")
		}
		fileServer.ServeHTTP(w, r)
	})
}

// /workers — POST = spawn, GET = list.
func (s *Server) handleWorkers(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		writeJSON(w, http.StatusOK, s.reg.List())
	case http.MethodPost:
		s.handleSpawn(w, r)
	default:
		w.Header().Set("Allow", "GET, POST")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

type spawnRequest struct {
	Count int      `json:"count"`
	Args  []string `json:"args"`
}

type spawnResponse struct {
	Workers []local.SpawnResult `json:"workers"`
}

func (s *Server) handleSpawn(w http.ResponseWriter, r *http.Request) {
	var req spawnRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid JSON: "+err.Error(), http.StatusBadRequest)
		return
	}
	if req.Count <= 0 {
		http.Error(w, "count must be > 0", http.StatusBadRequest)
		return
	}
	if req.Count > 256 {
		http.Error(w, "count must be <= 256", http.StatusBadRequest)
		return
	}

	results := s.mgr.Spawn(r.Context(), req.Count, req.Args)

	if s.rec != nil {
		_ = s.rec.Append(recent.Entry{
			Timestamp:  time.Now().UTC(),
			Count:      req.Count,
			Args:       req.Args,
			ConfigPath: s.cfgPath,
			WorkerBin:  s.binPath,
		})
	}

	status := http.StatusOK
	for _, r := range results {
		if !r.OK {
			status = http.StatusMultiStatus
			break
		}
	}
	writeJSON(w, status, spawnResponse{Workers: results})
}

// /workers/{id}            -> GET worker
// /workers/{id}/stop       -> POST stop
// /workers/stop-all        -> POST stop all
// /workers/{id}/log        -> GET tail
// /workers/{id}/log/stream -> GET SSE
func (s *Server) handleWorkerByID(w http.ResponseWriter, r *http.Request) {
	rest := strings.TrimPrefix(r.URL.Path, "/workers/")

	if rest == "stop-all" {
		if r.Method != http.MethodPost {
			w.Header().Set("Allow", "POST")
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
		defer cancel()
		s.mgr.StopAll(ctx)
		w.WriteHeader(http.StatusNoContent)
		return
	}

	parts := strings.Split(rest, "/")
	id := parts[0]
	if id == "" {
		http.NotFound(w, r)
		return
	}
	if _, ok := s.reg.Get(id); !ok {
		http.NotFound(w, r)
		return
	}

	switch {
	case len(parts) == 1:
		s.handleWorkerGet(w, r, id)
	case len(parts) == 2 && parts[1] == "stop":
		s.handleWorkerStop(w, r, id)
	case len(parts) == 2 && parts[1] == "purge":
		s.handleWorkerPurge(w, r, id)
	case len(parts) == 2 && parts[1] == "log":
		s.handleWorkerLog(w, r, id)
	case len(parts) == 3 && parts[1] == "log" && parts[2] == "stream":
		s.handleWorkerLogStream(w, r, id)
	default:
		http.NotFound(w, r)
	}
}

func (s *Server) handleWorkerGet(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	worker, ok := s.reg.Get(id)
	if !ok {
		http.NotFound(w, r)
		return
	}
	writeJSON(w, http.StatusOK, worker)
}

func (s *Server) handleWorkerStop(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()
	if err := s.mgr.Stop(ctx, id); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

// handleWorkerPurge deletes the on-disk trace of an exited worker
// (log, pidfile, sentinel) and removes the row from the registry.
// Refuses to purge a still-running worker.
func (s *Server) handleWorkerPurge(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if err := s.mgr.Purge(id); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (s *Server) handleWorkerLog(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	worker, _ := s.reg.Get(id)
	tail := int64(0)
	if v := r.URL.Query().Get("tail"); v != "" {
		n, err := strconv.ParseInt(v, 10, 64)
		if err != nil || n < 0 {
			http.Error(w, "invalid tail", http.StatusBadRequest)
			return
		}
		tail = n
	}
	data, err := logbuf.Tail(worker.LogPath, tail)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	_, _ = w.Write(data)
}

func (s *Server) handleWorkerLogStream(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	worker, _ := s.reg.Get(id)
	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Connection", "keep-alive")
	w.WriteHeader(http.StatusOK)
	flusher.Flush()

	sw := &sseWriter{w: w, flusher: flusher}
	if err := logbuf.Stream(r.Context(), worker.LogPath, sw); err != nil && !errors.Is(err, context.Canceled) {
		_, _ = fmt.Fprintf(w, "event: error\ndata: %s\n\n", err.Error())
		flusher.Flush()
	}
}

// sseWriter wraps log lines as SSE "data:" frames.
type sseWriter struct {
	w       http.ResponseWriter
	flusher http.Flusher
}

func (s *sseWriter) Write(p []byte) (int, error) {
	body := strings.TrimRight(string(p), "\n")
	if _, err := fmt.Fprintf(s.w, "data: %s\n\n", body); err != nil {
		return 0, err
	}
	s.flusher.Flush()
	return len(p), nil
}

// Flush implements the optional interface logbuf.Stream checks for.
func (s *sseWriter) Flush() error { s.flusher.Flush(); return nil }

func writeJSON(w http.ResponseWriter, status int, v interface{}) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

// quarantineEntry is the trimmed-down JSON shape exposed to the UI.
// Mirrors enough of adopt.Candidate to render a useful row, no more.
type quarantineEntry struct {
	Key          string   `json:"key"`
	RunID        string   `json:"run_id"`
	Slot         int      `json:"slot"`
	PID          int      `json:"pid"`
	ControllerID string   `json:"controller_id"`
	StartedAt    int64    `json:"started_at_unix"`
	WorkerBin    string   `json:"worker_bin"`
	Args         []string `json:"args"`
	LogPath      string   `json:"log_path"`
	Alive        bool     `json:"alive"`
	HistoryHit   bool     `json:"history_hit"`
	Note         string   `json:"note"`
}

func (s *Server) handleQuarantineList(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	cands := s.mgr.Quarantine()
	out := make([]quarantineEntry, 0, len(cands))
	for _, c := range cands {
		out = append(out, toEntry(c))
	}
	writeJSON(w, http.StatusOK, out)
}

func (s *Server) handleQuarantineAct(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	// URL: /quarantine/<run-id>/<slot>/<action>
	rest := strings.TrimPrefix(r.URL.Path, "/quarantine/")
	parts := strings.Split(rest, "/")
	if len(parts) != 3 {
		http.Error(w, "expected /quarantine/<run-id>/<slot>/<action>", http.StatusBadRequest)
		return
	}
	key := parts[0] + "/" + parts[1]
	action := parts[2]
	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()
	if err := s.mgr.QuarantineAct(ctx, key, action); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func toEntry(c adopt.Candidate) quarantineEntry {
	e := quarantineEntry{
		Key:        c.Key(),
		Alive:      c.Alive,
		HistoryHit: c.HistoryHit,
		Note:       c.HistoryNote,
	}
	if c.Sentinel != nil {
		e.RunID = c.Sentinel.RunID
		e.Slot = c.Sentinel.Slot
		e.PID = c.Sentinel.PID
		e.ControllerID = c.Sentinel.ControllerID
		e.StartedAt = c.Sentinel.StartedAtUnix
		e.WorkerBin = c.Sentinel.WorkerBin
		e.Args = c.Sentinel.Args
		e.LogPath = c.Sentinel.LogPath
	}
	return e
}
