// Package api wires the controller's HTTP surface.
package api

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"log"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/adopt"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/bootstrap"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/codespace"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/gh"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/inventory"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/local"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/logbuf"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/recent"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/registry"
)

// Server is the HTTP handler set for the controller.
type Server struct {
	mux      *http.ServeMux
	webFS    fs.FS
	noCache  bool
	reg      *registry.Registry
	mgr      *local.Manager
	csmgr    *codespace.Manager
	rec      *recent.Log
	cfgPath  string
	binPath  string
	cacheDir string
	inv      *inventory.Inventory
}

// Options configures Server.
type Options struct {
	WebFS      fs.FS
	Registry   *registry.Registry
	Manager    *local.Manager
	Codespace  *codespace.Manager
	Recent     *recent.Log
	ConfigPath string
	WorkerBin  string
	CacheDir   string
	Inventory  *inventory.Inventory
}

// New constructs a Server.
func New(opts Options) *Server {
	s := &Server{
		mux:      http.NewServeMux(),
		webFS:    opts.WebFS,
		noCache:  true, // Tactical Decision #8
		reg:      opts.Registry,
		mgr:      opts.Manager,
		csmgr:    opts.Codespace,
		rec:      opts.Recent,
		cfgPath:  opts.ConfigPath,
		binPath:  opts.WorkerBin,
		cacheDir: opts.CacheDir,
		inv:      opts.Inventory,
	}
	s.routes()
	return s
}

// Handler exposes the underlying mux.
func (s *Server) Handler() http.Handler { return s.mux }

func (s *Server) routes() {
	s.mux.HandleFunc("/healthz", s.handleHealthz)
	s.mux.HandleFunc("/hosts", s.handleHosts)
	s.mux.HandleFunc("/hosts/", s.handleHostByID)
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

// /hosts — list hosts from the inventory. Read-only; editing is via
// hosts.json + restart.
func (s *Server) handleHosts(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	type hostView struct {
		ID        string `json:"id"`
		Backend   string `json:"backend"`
		Supported bool   `json:"supported"` // false ⇒ UI grays out the dropdown entry
	}
	var out []hostView
	if s.inv != nil {
		out = make([]hostView, 0, len(s.inv.Hosts))
		for _, h := range s.inv.Hosts {
			supported := h.Backend == inventory.BackendLocal ||
				(h.Backend == inventory.BackendCodespace && s.csmgr != nil)
			out = append(out, hostView{
				ID:        h.ID,
				Backend:   string(h.Backend),
				Supported: supported,
			})
		}
	}
	writeJSON(w, http.StatusOK, out)
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

// /hosts/{id}/status — backend-aware reachability + auth check.
//
// Status codes (all returned in the JSON body's "status" field):
//
//	ok                       — host is fully reachable
//	gh-missing               — `gh` not on PATH (codespace only)
//	not-logged-in            — `gh auth status` reports no session
//	needs-codespace-scope    — token missing the `codespace` scope
//	codespace-not-found      — `gh codespace list` doesn't list it
//	codespace-not-available  — codespace exists but isn't running
//	error                    — anything else (`detail` populated)
func (s *Server) handleHostByID(w http.ResponseWriter, r *http.Request) {
	rest := strings.TrimPrefix(r.URL.Path, "/hosts/")
	parts := strings.Split(rest, "/")
	id := parts[0]
	if id == "" {
		http.NotFound(w, r)
		return
	}
	if len(parts) < 2 {
		http.NotFound(w, r)
		return
	}

	if s.inv == nil {
		http.Error(w, "inventory not configured", http.StatusInternalServerError)
		return
	}
	host, ok := s.inv.Get(id)
	if !ok {
		http.Error(w, fmt.Sprintf("unknown host_id %q", id), http.StatusNotFound)
		return
	}

	switch parts[1] {
	case "status":
		if r.Method != http.MethodGet {
			w.Header().Set("Allow", "GET")
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		s.handleHostStatus(w, r, host)
		return
	case "bootstrap":
		if r.Method != http.MethodPost {
			w.Header().Set("Allow", "POST")
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		s.handleHostBootstrap(w, r, host)
		return
	default:
		http.NotFound(w, r)
		return
	}
}

// handleHostStatus is the GET /hosts/{id}/status implementation.
func (s *Server) handleHostStatus(w http.ResponseWriter, r *http.Request, host inventory.Host) {
	type statusResp struct {
		ID        string             `json:"id"`
		Backend   string             `json:"backend"`
		Status    string             `json:"status"`
		Detail    string             `json:"detail,omitempty"`
		Hint      string             `json:"hint,omitempty"`
		Auth      *gh.AuthStatusInfo `json:"auth,omitempty"`
		Codespace *gh.Codespace      `json:"codespace,omitempty"`
	}

	resp := statusResp{ID: host.ID, Backend: string(host.Backend)}

	switch host.Backend {
	case inventory.BackendLocal:
		resp.Status = "ok"
		writeJSON(w, http.StatusOK, resp)
		return
	case inventory.BackendCodespace:
		ctx, cancel := context.WithTimeout(r.Context(), 15*time.Second)
		defer cancel()

		auth, err := gh.AuthStatus(ctx)
		resp.Auth = auth
		if err != nil {
			var miss *gh.MissingBinaryError
			var noScope *gh.NeedsCodespaceScopeError
			var notLogin *gh.NotLoggedInError
			switch {
			case errors.As(err, &miss):
				resp.Status = "gh-missing"
				resp.Detail = err.Error()
				writeJSON(w, http.StatusOK, resp)
				return
			case errors.As(err, &notLogin):
				resp.Status = "not-logged-in"
				resp.Detail = err.Error()
				resp.Hint = "gh auth login"
				writeJSON(w, http.StatusOK, resp)
				return
			case errors.As(err, &noScope):
				resp.Status = "needs-codespace-scope"
				resp.Detail = err.Error()
				resp.Hint = "gh auth refresh -h github.com -s codespace"
				writeJSON(w, http.StatusOK, resp)
				return
			default:
				resp.Status = "error"
				resp.Detail = err.Error()
				writeJSON(w, http.StatusOK, resp)
				return
			}
		}

		var name, label string
		if host.Codespace != nil {
			name = host.Codespace.Name
			label = host.Codespace.Label
		}
		var cs *gh.Codespace
		if label != "" {
			cs, err = gh.ResolveByLabel(ctx, label)
		} else {
			cs, err = gh.Resolve(ctx, name)
		}
		if err != nil {
			var nf *gh.NotFoundError
			var lnf *gh.LabelNotFoundError
			if errors.As(err, &nf) || errors.As(err, &lnf) {
				resp.Status = "codespace-not-found"
				resp.Detail = "Bootstrap will create a new codespace"
				writeJSON(w, http.StatusOK, resp)
				return
			}
			resp.Status = "error"
			resp.Detail = err.Error()
			writeJSON(w, http.StatusOK, resp)
			return
		}
		resp.Codespace = cs
		if !strings.EqualFold(cs.State, "Available") {
			resp.Status = "codespace-not-available"
			resp.Detail = fmt.Sprintf("codespace state is %q; start it in the GitHub UI", cs.State)
			writeJSON(w, http.StatusOK, resp)
			return
		}
		resp.Status = "ok"
		writeJSON(w, http.StatusOK, resp)
		return
	default:
		// ssh / gcp-iap reserved by inventory but no transport landed
		// yet. Surface a clean "not implemented" instead of a vague
		// error so the UI can hide the badge.
		resp.Status = "error"
		resp.Detail = fmt.Sprintf("backend %q has no status probe yet", host.Backend)
		writeJSON(w, http.StatusOK, resp)
		return
	}
}

// handleHostBootstrap is the POST /hosts/{id}/bootstrap implementation.
// Body (all optional): {"repo":"OWNER/REPO","tag":"v0.4.2"}.
func (s *Server) handleHostBootstrap(w http.ResponseWriter, r *http.Request, host inventory.Host) {
	if host.Backend != inventory.BackendCodespace {
		http.Error(w, fmt.Sprintf("bootstrap is only supported for backend=codespace (host %q is %s)", host.ID, host.Backend), http.StatusBadRequest)
		return
	}
	if host.Codespace == nil {
		http.Error(w, fmt.Sprintf("host %q: codespace config is required for bootstrap", host.ID), http.StatusBadRequest)
		return
	}
	if host.Codespace.Name == "" && host.Codespace.Label == "" {
		http.Error(w, fmt.Sprintf("host %q: either codespace.name or codespace.label is required for bootstrap", host.ID), http.StatusBadRequest)
		return
	}

	var body struct {
		Repo string `json:"repo"`
		Tag  string `json:"tag"`
	}
	if r.ContentLength > 0 {
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			http.Error(w, "invalid JSON: "+err.Error(), http.StatusBadRequest)
			return
		}
	}

	// Bootstrap can take minutes (download + upload + run installer).
	// Cap at 10 min so a hung gh process doesn't pin a goroutine
	// forever.
	ctx, cancel := context.WithTimeout(r.Context(), 10*time.Minute)
	defer cancel()

	res, err := bootstrap.Bootstrap(ctx, bootstrap.Request{
		HostID:         host.ID,
		CodespaceName:  host.Codespace.Name,
		CodespaceLabel: host.Codespace.Label,
		CodespaceRepo:  host.Codespace.Repo,
		Repo:           body.Repo,
		Tag:            body.Tag,
		CacheDir:       s.cacheDir,
	})
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	writeJSON(w, http.StatusOK, res)
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
	Count  int      `json:"count"`
	Args   []string `json:"args"`
	HostID string   `json:"host_id,omitempty"`
}

type spawnResponse struct {
	Workers []spawnEntry `json:"workers"`
}

// spawnEntry is the per-worker shape the API returns. Local and
// codespace backends produce structurally-identical results — keeping
// one shape lets the UI render uniformly without sniffing the host.
type spawnEntry struct {
	ID    string `json:"id"`
	OK    bool   `json:"ok"`
	PID   int    `json:"pid,omitempty"`
	Error string `json:"error,omitempty"`
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

	hostID := req.HostID
	if hostID == "" {
		hostID = "local"
	}

	var host inventory.Host
	var haveHost bool
	if s.inv != nil {
		h, ok := s.inv.Get(hostID)
		if !ok {
			http.Error(w, fmt.Sprintf("unknown host_id %q", hostID), http.StatusBadRequest)
			return
		}
		host = h
		haveHost = true
	}

	var entries []spawnEntry
	switch {
	case !haveHost || host.Backend == inventory.BackendLocal:
		results := s.mgr.Spawn(r.Context(), req.Count, req.Args)
		entries = make([]spawnEntry, len(results))
		for i, r := range results {
			entries[i] = spawnEntry{ID: r.ID, OK: r.OK, PID: r.PID, Error: r.Error}
		}
	case host.Backend == inventory.BackendCodespace:
		if s.csmgr == nil {
			http.Error(w, "codespace backend not configured", http.StatusInternalServerError)
			return
		}
		results := s.csmgr.Spawn(r.Context(), host, req.Count, req.Args)
		entries = make([]spawnEntry, len(results))
		for i, r := range results {
			entries[i] = spawnEntry{ID: r.ID, OK: r.OK, PID: r.PID, Error: r.Error}
		}
	default:
		http.Error(w, fmt.Sprintf("host %q backend=%s is not yet supported", hostID, host.Backend), http.StatusNotImplemented)
		return
	}

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
	for _, e := range entries {
		if !e.OK {
			status = http.StatusMultiStatus
			break
		}
	}
	writeJSON(w, status, spawnResponse{Workers: entries})
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
		ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
		defer cancel()
		s.mgr.StopAll(ctx)
		if s.csmgr != nil {
			s.csmgr.StopAll(ctx)
		}
		w.WriteHeader(http.StatusNoContent)
		return
	}

	if rest == "purge-all" {
		s.handleWorkersPurgeAll(w, r)
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
	ctx, cancel := context.WithTimeout(r.Context(), 90*time.Second)
	defer cancel()
	// Dispatch by ownership: codespace manager owns its workers'
	// remote PIDs; everything else is local.
	var err error
	if s.csmgr != nil && s.csmgr.IsCodespaceWorker(id) {
		err = s.csmgr.Stop(ctx, id)
	} else {
		err = s.mgr.Stop(ctx, id)
	}
	if err != nil {
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
	var err error
	if s.csmgr != nil && s.csmgr.IsCodespaceWorker(id) {
		err = s.csmgr.Purge(id)
	} else {
		err = s.mgr.Purge(id)
	}
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

// handleWorkersPurgeAll is the POST /workers/purge-all implementation.
// Iterates the registry once and purges every worker in StateExited,
// dispatching to the right backend. Running/starting/stopping workers
// are silently skipped (operator must Stop them first). Always
// succeeds (204) — per-row failures are logged. Returns a small JSON
// summary in the response body so the UI can show "purged N row(s)".
func (s *Server) handleWorkersPurgeAll(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var purged, skipped, failed int
	for _, worker := range s.reg.List() {
		if worker.State != registry.StateExited {
			skipped++
			continue
		}
		var err error
		if s.csmgr != nil && s.csmgr.IsCodespaceWorker(worker.ID) {
			err = s.csmgr.Purge(worker.ID)
		} else {
			err = s.mgr.Purge(worker.ID)
		}
		if err != nil {
			log.Printf("purge-all: %s: %v", worker.ID, err)
			failed++
			continue
		}
		purged++
	}
	writeJSON(w, http.StatusOK, map[string]int{
		"purged":  purged,
		"skipped": skipped,
		"failed":  failed,
	})
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
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	// Codespace logs live on the remote; tail via ssh. We treat
	// `tail` as a line count rather than a byte count for remote
	// workers so the operator gets sensible output without us having
	// to byte-seek over ssh.
	if s.csmgr != nil && s.csmgr.IsCodespaceWorker(id) {
		lines := int(tail)
		if lines == 0 {
			lines = 200 // sane default; SSH for the entire log on demand only
		}
		ctx, cancel := context.WithTimeout(r.Context(), 45*time.Second)
		defer cancel()
		data, err := s.csmgr.TailLog(ctx, id, lines)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadGateway)
			return
		}
		_, _ = w.Write(data)
		return
	}
	data, err := logbuf.Tail(worker.LogPath, tail)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	_, _ = w.Write(data)
}

func (s *Server) handleWorkerLogStream(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	// SSE log streaming is not supported for codespace workers.
	// The UI falls back to /log?tail=N polling.
	if s.csmgr != nil && s.csmgr.IsCodespaceWorker(id) {
		http.Error(w, "log streaming not supported for codespace workers; use /log?tail=N", http.StatusNotImplemented)
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
