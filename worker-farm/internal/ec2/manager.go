// manager.go — EC2 lifecycle manager for the worker-farm remote backend.
//
// Spawn starts N workers on a managed EC2 instance via SSM, Stop sends
// SIGTERM+SIGKILL, and Run polls liveness at PollPeriod. Idle instances
// (no alive workers) are automatically stopped after IdleTimeout.
package ec2

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	_ "embed"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/service/ssm"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/inventory"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/registry"
)

//go:embed start_workers_local.sh
var spawnScript []byte

const (
	// remoteSpawnScriptPath is where start_workers_local.sh is uploaded on
	// each managed instance. /tmp survives a controller restart but is
	// wiped on instance reboot, so we re-upload when the hash cache misses.
	remoteSpawnScriptPath = "/tmp/tm-farm-spawn.sh"

	// Timeout for the SSM command that starts workers (cold-start can be
	// slow on a fresh instance).
	managerSpawnTimeout = 4 * time.Minute

	// Timeout for the SSM kill command.
	managerStopTimeout = 90 * time.Second

	// Timeout for log tail.
	managerLogTimeout = 30 * time.Second

	defaultManagerPollPeriod  = 5 * time.Second
	defaultManagerGracePeriod = 10 * time.Second
	defaultIdleTimeout        = 15 * time.Minute

	ec2RunDirBeginMarker = "===TM_FARM_RUN_DIR_BEGIN==="
	ec2RunDirEndMarker   = "===TM_FARM_RUN_DIR_END==="
	ec2ManifestBeginMark = "===TM_FARM_MANIFEST_BEGIN==="
	ec2ManifestEndMarker = "===TM_FARM_MANIFEST_END==="
)

// SpawnResult mirrors codespace.SpawnResult so the API can return a
// single uniform JSON shape regardless of backend.
type SpawnResult struct {
	ID    string `json:"id"`
	OK    bool   `json:"ok"`
	PID   int    `json:"pid,omitempty"`
	Error string `json:"error,omitempty"`
}

// ec2RunState is per-run bookkeeping kept in memory.
type ec2RunState struct {
	runID        string
	hostID       string
	instanceID   string
	ec2Cfg       inventory.EC2Cfg // needed for idle auto-stop
	remoteRunDir string
	workerIDs    []string
}

// Manager owns EC2-backed runs spawned by this controller. One
// instance serves every ec2 host in the inventory.
type Manager struct {
	reg          *registry.Registry
	inv          *inventory.Inventory
	cacheDir     string
	controllerID string
	gracePeriod  time.Duration
	pollPeriod   time.Duration
	idleTimeout  time.Duration

	mu             sync.Mutex
	runs           map[string]*ec2RunState // runID → state
	stopRequested  map[string]time.Time    // workerID → when stop was requested
	uploadedScript map[string]bool         // instanceID → start script uploaded this session
	idleSince      map[string]time.Time    // instanceID → time it became fully idle
}

// Options configures Manager.
type Options struct {
	Registry     *registry.Registry
	Inventory    *inventory.Inventory
	CacheDir     string
	ControllerID string
	// GracePeriod between SIGTERM and SIGKILL in Stop. Default 10 s.
	GracePeriod time.Duration
	// PollPeriod is the liveness poll cadence. Default 5 s.
	PollPeriod time.Duration
	// IdleTimeout is how long an instance with no live workers stays up
	// before the manager calls StopInstance. 0 → use default (15 min).
	IdleTimeout time.Duration
}

// New builds a Manager; does not start polling — call Run for that.
func New(opts Options) *Manager {
	gp := opts.GracePeriod
	if gp == 0 {
		gp = defaultManagerGracePeriod
	}
	pp := opts.PollPeriod
	if pp == 0 {
		pp = defaultManagerPollPeriod
	}
	it := opts.IdleTimeout
	if it == 0 {
		it = defaultIdleTimeout
	}
	return &Manager{
		reg:            opts.Registry,
		inv:            opts.Inventory,
		cacheDir:       opts.CacheDir,
		controllerID:   opts.ControllerID,
		gracePeriod:    gp,
		pollPeriod:     pp,
		idleTimeout:    it,
		runs:           map[string]*ec2RunState{},
		stopRequested:  map[string]time.Time{},
		uploadedScript: map[string]bool{},
		idleSince:      map[string]time.Time{},
	}
}

// Run blocks until ctx is done, polling remote worker liveness at
// PollPeriod and stopping idle instances after IdleTimeout. Call as a
// goroutine; cancel ctx to stop.
func (m *Manager) Run(ctx context.Context) {
	t := time.NewTicker(m.pollPeriod)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			m.pollOnce(ctx)
		}
	}
}

// Spawn starts `count` workers on `host` via SSM and records each in
// the registry. Returned slice is in slot order.
func (m *Manager) Spawn(ctx context.Context, host inventory.Host, count int, extraArgs []string) []SpawnResult {
	if count <= 0 {
		return nil
	}
	results := make([]SpawnResult, count)
	failAll := func(msg string) []SpawnResult {
		for i := range results {
			results[i] = SpawnResult{ID: ec2NewWorkerID(), OK: false, Error: msg}
		}
		return results
	}

	if host.Backend != inventory.BackendEC2 || host.EC2 == nil {
		return failAll(fmt.Sprintf("host %q is not an ec2 backend", host.ID))
	}

	spawnCtx, cancelSpawn := context.WithTimeout(ctx, managerSpawnTimeout)
	defer cancelSpawn()

	inst, err := EnsureInstance(spawnCtx, *host.EC2, host.ID, m.controllerID)
	if err != nil {
		return failAll(fmt.Sprintf("ensure ec2 instance: %v", err))
	}

	cl, err := newClients(spawnCtx, inst.Region)
	if err != nil {
		return failAll(fmt.Sprintf("create aws clients: %v", err))
	}

	// Upload the spawn helper script once per instance per session.
	if err := m.ensureSpawnScriptUploaded(spawnCtx, cl.ssm, inst.InstanceID); err != nil {
		return failAll(fmt.Sprintf("upload spawn script: %v", err))
	}

	// Build the SSM command: source the script (so $run_dir is available
	// in the same shell), then emit sentinel-fenced output.
	workerBin := host.EC2.WorkerBin
	if strings.TrimSpace(workerBin) == "" {
		workerBin = "tm-worker"
	}
	cfgPath := host.EC2.Config
	if cfgPath == "" {
		cfgPath = "~/.config/task-messenger/tm-worker/config-worker.json"
	}

	var extraPart strings.Builder
	if len(extraArgs) > 0 {
		extraPart.WriteString(" --")
		for _, a := range extraArgs {
			extraPart.WriteByte(' ')
			extraPart.WriteString(shQuote(a))
		}
	}

	innerSpawnCmd := fmt.Sprintf(
		"set -e\n"+
			"source %s -n %d -b %s -c %s%s\n"+
			"if [ -z \"${run_dir:-}\" ]; then echo 'spawn helper did not set run_dir' >&2; exit 1; fi\n"+
			"echo '%s'\n"+
			"echo \"$run_dir\"\n"+
			"echo '%s'\n"+
			"echo '%s'\n"+
			"cat \"$run_dir/manifest.json\"\n"+
			"echo '%s'\n",
		shQuote(remoteSpawnScriptPath),
		count,
		shQuote(workerBin),
		shQuote(cfgPath),
		extraPart.String(),
		ec2RunDirBeginMarker,
		ec2RunDirEndMarker,
		ec2ManifestBeginMark,
		ec2ManifestEndMarker,
	)
	spawnCmd := "bash -lc " + shQuote(innerSpawnCmd)

	out, err := runSSMShellT(spawnCtx, cl.ssm, inst.InstanceID, spawnCmd, managerSpawnTimeout)
	if err != nil {
		log.Printf("ec2 spawn host=%s instance=%s count=%d: ssm failed: %v\n--- output ---\n%s",
			host.ID, inst.InstanceID, count, err, string(out))
		return failAll(fmt.Sprintf("ssm spawn: %v", err))
	}

	runDir, manifestRaw, perr := ec2ParseSpawnOutput([]byte(out))
	if perr != nil {
		log.Printf("ec2 spawn host=%s instance=%s: parse output: %v\n--- output ---\n%s",
			host.ID, inst.InstanceID, perr, out)
		return failAll(fmt.Sprintf("parse remote manifest: %v", perr))
	}

	var rm ec2RemoteManifest
	if err := json.Unmarshal(manifestRaw, &rm); err != nil {
		log.Printf("ec2 spawn host=%s instance=%s run_dir=%s: initial manifest decode failed: %v; attempting direct refetch", host.ID, inst.InstanceID, runDir, err)
		refetchCtx, cancelRefetch := context.WithTimeout(spawnCtx, 30*time.Second)
		refetched, ferr := runSSMShellT(refetchCtx, cl.ssm, inst.InstanceID,
			"cat "+shQuote(filepath.Join(runDir, "manifest.json")), 30*time.Second)
		cancelRefetch()
		if ferr != nil {
			return failAll(fmt.Sprintf("decode manifest: %v (refetch failed: %v)", err, ferr))
		}
		manifestRaw = []byte(strings.TrimSpace(refetched))
		if err2 := json.Unmarshal(manifestRaw, &rm); err2 != nil {
			return failAll(fmt.Sprintf("decode manifest: %v (refetched decode failed: %v)", err, err2))
		}
	}
	if len(rm.Workers) == 0 {
		return failAll("remote manifest reported zero workers")
	}

	m.mirrorManifest(host.ID, rm.RunID, manifestRaw)

	state := &ec2RunState{
		runID:        rm.RunID,
		hostID:       host.ID,
		instanceID:   inst.InstanceID,
		ec2Cfg:       *host.EC2,
		remoteRunDir: runDir,
		workerIDs:    make([]string, 0, len(rm.Workers)),
	}
	startedAt := time.Now().UTC()
	baseArgs := append([]string{}, rm.Args...)
	for i, rw := range rm.Workers {
		slot := i + 1
		id := ec2NewWorkerID()
		w := &registry.Worker{
			ID:        id,
			PID:       rw.PID,
			State:     registry.StateRunning,
			RunID:     rm.RunID,
			Slot:      slot,
			StartedAt: startedAt,
			Args:      append([]string{}, baseArgs...),
			LogPath:   rw.Log,
			Host:      host.ID,
		}
		m.reg.Add(w)
		state.workerIDs = append(state.workerIDs, id)
		results[i] = SpawnResult{ID: id, OK: true, PID: rw.PID}
	}

	m.mu.Lock()
	m.runs[rm.RunID] = state
	// Cancel any idle timer: instance is now in use again.
	delete(m.idleSince, inst.InstanceID)
	m.mu.Unlock()

	log.Printf("ec2 spawn host=%s instance=%s run=%s count=%d remote_dir=%s",
		host.ID, inst.InstanceID, rm.RunID, len(rm.Workers), runDir)
	return results
}

// Stop sends SIGTERM to the worker's remote PID and schedules a
// background SIGKILL after gracePeriod. Returns when the SSM command
// exits (well before the grace timer fires on the remote).
func (m *Manager) Stop(ctx context.Context, workerID string) error {
	w, ok := m.reg.Get(workerID)
	if !ok {
		return fmt.Errorf("unknown worker %q", workerID)
	}
	if w.State == registry.StateExited {
		return nil
	}
	st := m.ec2RunForWorker(workerID)
	if st == nil {
		return fmt.Errorf("worker %s: no ec2 run state (controller restart? not supported for ec2 backend)", workerID)
	}

	m.reg.Update(workerID, func(w *registry.Worker) {
		w.State = registry.StateStopping
	})
	m.mu.Lock()
	m.stopRequested[workerID] = time.Now().UTC()
	m.mu.Unlock()

	grace := int(m.gracePeriod.Seconds())
	if grace < 1 {
		grace = 1
	}

	stopScript := fmt.Sprintf(`
set -u
PID=%d
GRACE=%d
kill -TERM "$PID" 2>/dev/null || true
nohup bash -c '
  for i in $(seq 1 %d); do
    sleep 1
    kill -0 '"$PID"' 2>/dev/null || exit 0
  done
  kill -KILL '"$PID"' 2>/dev/null || true
' >/dev/null 2>&1 &
disown 2>/dev/null || true
echo "stop submitted pid=$PID grace=${GRACE}s"
`, w.PID, grace, grace)

	cl, err := newClients(ctx, st.ec2Cfg.Region)
	if err != nil {
		return fmt.Errorf("ec2 stop: create clients: %w", err)
	}

	stopCtx, cancel := context.WithTimeout(ctx, managerStopTimeout)
	defer cancel()
	if _, err := runSSMShellT(stopCtx, cl.ssm, st.instanceID, stopScript, managerStopTimeout); err != nil {
		return fmt.Errorf("ssm stop: %w", err)
	}
	return nil
}

// StopAll stops every EC2-backed worker the manager knows about.
// Errors are logged and not propagated (matching local.Manager semantics).
func (m *Manager) StopAll(ctx context.Context) {
	ids := m.ec2RunningWorkerIDs()
	if len(ids) == 0 {
		return
	}
	log.Printf("ec2 stop-all: %d worker(s)", len(ids))
	var wg sync.WaitGroup
	for _, id := range ids {
		wg.Add(1)
		go func(id string) {
			defer wg.Done()
			if err := m.Stop(ctx, id); err != nil {
				log.Printf("ec2 stop %s: %v", id, err)
			}
		}(id)
	}
	wg.Wait()
}

// Purge removes an exited EC2 worker from the registry and drops any
// per-run bookkeeping once no live siblings remain. Refuses to purge a
// still-running worker — caller must Stop first. The local manifest
// mirror is left in place as an audit trail.
func (m *Manager) Purge(id string) error {
	w, ok := m.reg.Get(id)
	if !ok {
		return fmt.Errorf("purge: no worker %q", id)
	}
	if w.State == registry.StateRunning || w.State == registry.StateStarting || w.State == registry.StateStopping {
		return fmt.Errorf("purge: worker %s is %s; stop it first", id, w.State)
	}
	m.reg.Remove(id)
	m.mu.Lock()
	delete(m.stopRequested, id)
	if w.RunID != "" {
		stillReferenced := false
		for _, sib := range m.reg.List() {
			if sib.RunID == w.RunID {
				stillReferenced = true
				break
			}
		}
		if !stillReferenced {
			delete(m.runs, w.RunID)
		}
	}
	m.mu.Unlock()
	log.Printf("ec2 purge %s: run=%s slot=%02d", id, w.RunID, w.Slot)
	return nil
}

// TailLog returns the last `lines` lines of the worker's remote log
// via SSM. lines <= 0 means the entire file.
func (m *Manager) TailLog(ctx context.Context, workerID string, lines int) ([]byte, error) {
	w, ok := m.reg.Get(workerID)
	if !ok {
		return nil, fmt.Errorf("unknown worker %q", workerID)
	}
	st := m.ec2RunForWorker(workerID)
	if st == nil {
		return nil, fmt.Errorf("worker %s: no run state", workerID)
	}
	if w.LogPath == "" {
		return nil, fmt.Errorf("worker %s has no log path", workerID)
	}
	tailArg := "-n +1"
	if lines > 0 {
		tailArg = fmt.Sprintf("-n %d", lines)
	}
	cmd := fmt.Sprintf("tail %s -- %s", tailArg, shQuote(w.LogPath))

	cl, err := newClients(ctx, st.ec2Cfg.Region)
	if err != nil {
		return nil, fmt.Errorf("ec2 tail-log: create clients: %w", err)
	}

	logCtx, cancel := context.WithTimeout(ctx, managerLogTimeout)
	defer cancel()
	out, err := runSSMShellT(logCtx, cl.ssm, st.instanceID, cmd, managerLogTimeout)
	if err != nil {
		return []byte(out), err
	}
	return []byte(out), nil
}

// IsEC2Worker reports whether this manager owns the given worker.
// Used by the API to dispatch Stop/Log calls to the right backend.
func (m *Manager) IsEC2Worker(id string) bool {
	return m.ec2RunForWorker(id) != nil
}

// pollOnce groups every running EC2 worker by instance, runs a single
// SSM kill-0 per instance to detect exited workers, and auto-stops
// instances that have been idle longer than IdleTimeout.
func (m *Manager) pollOnce(ctx context.Context) {
	type watch struct {
		id    string
		pid   int
		state registry.State
	}
	type instInfo struct {
		watches []watch
		ec2Cfg  inventory.EC2Cfg
	}
	byInst := map[string]*instInfo{} // instanceID → info

	for _, w := range m.reg.List() {
		if w.State != registry.StateRunning && w.State != registry.StateStopping {
			continue
		}
		st := m.ec2RunForWorker(w.ID)
		if st == nil {
			continue
		}
		ii := byInst[st.instanceID]
		if ii == nil {
			ii = &instInfo{ec2Cfg: st.ec2Cfg}
			byInst[st.instanceID] = ii
		}
		ii.watches = append(ii.watches, watch{id: w.ID, pid: w.PID, state: w.State})
	}

	stoppingDeadline := m.gracePeriod + 10*time.Second
	now := time.Now().UTC()

	for instanceID, info := range byInst {
		pids := make([]string, 0, len(info.watches))
		for _, w := range info.watches {
			pids = append(pids, strconv.Itoa(w.pid))
		}
		script := fmt.Sprintf(`for pid in %s; do kill -0 "$pid" 2>/dev/null && echo "$pid"; done`,
			strings.Join(pids, " "))

		cl, clErr := newClients(ctx, info.ec2Cfg.Region)
		var out string
		var ssmErr error
		if clErr != nil {
			ssmErr = clErr
		} else {
			pollCtx, cancel := context.WithTimeout(ctx, 30*time.Second)
			out, ssmErr = runSSMShellT(pollCtx, cl.ssm, instanceID, script, 30*time.Second)
			cancel()
		}

		if ssmErr != nil {
			log.Printf("ec2 poll instance=%s: %v", instanceID, ssmErr)
			// Don't mark running workers dead on a single failed poll.
			// Do unstick anything past the stopping deadline.
			for _, w := range info.watches {
				if w.state == registry.StateStopping && m.ec2StoppingFor(w.id, now) > stoppingDeadline {
					m.ec2MarkExited(w.id, "ssm-unreachable-after-stop")
				}
			}
			continue
		}

		alive := map[int]bool{}
		sc := bufio.NewScanner(bytes.NewReader([]byte(out)))
		for sc.Scan() {
			line := strings.TrimSpace(sc.Text())
			if line == "" {
				continue
			}
			if pid, err := strconv.Atoi(line); err == nil {
				alive[pid] = true
			}
		}
		for _, w := range info.watches {
			if alive[w.pid] {
				if w.state == registry.StateStopping && m.ec2StoppingFor(w.id, now) > stoppingDeadline {
					m.ec2MarkExited(w.id, "stopping-deadline-exceeded")
				}
				continue
			}
			m.ec2MarkExited(w.id, fmt.Sprintf("poll: pid %d not alive", w.pid))
		}
	}

	// Idle auto-stop: after all workers on an instance exit, stop it
	// after IdleTimeout to avoid unnecessary AWS charges.
	activeInsts := make(map[string]bool, len(byInst))
	for k := range byInst {
		activeInsts[k] = true
	}
	m.handleIdleInstances(ctx, activeInsts)
}

// handleIdleInstances checks whether any instances tracked by the manager
// have become fully idle (no running/stopping workers), updates
// m.idleSince, and calls StopInstance on instances idle longer than
// m.idleTimeout.
func (m *Manager) handleIdleInstances(ctx context.Context, active map[string]bool) {
	known := map[string]inventory.EC2Cfg{}
	m.mu.Lock()
	for _, st := range m.runs {
		known[st.instanceID] = st.ec2Cfg
	}
	m.mu.Unlock()

	now := time.Now().UTC()
	for instanceID, cfg := range known {
		if active[instanceID] {
			// At least one alive/stopping worker: clear idle timer.
			m.mu.Lock()
			delete(m.idleSince, instanceID)
			m.mu.Unlock()
			continue
		}
		// No active workers on this instance.
		m.mu.Lock()
		t, alreadyIdle := m.idleSince[instanceID]
		if !alreadyIdle {
			m.idleSince[instanceID] = now
			m.mu.Unlock()
			continue
		}
		idleDur := now.Sub(t)
		m.mu.Unlock()

		if idleDur >= m.idleTimeout {
			log.Printf("ec2 idle-stop: instance %s idle for %s (>= %s); stopping",
				instanceID, idleDur.Round(time.Second), m.idleTimeout)
			stopCtx, cancel := context.WithTimeout(ctx, 60*time.Second)
			if err := StopInstance(stopCtx, cfg, instanceID); err != nil {
				log.Printf("ec2 idle-stop: StopInstance %s: %v", instanceID, err)
			} else {
				m.mu.Lock()
				delete(m.idleSince, instanceID)
				m.mu.Unlock()
			}
			cancel()
		}
	}
}

// ensureSpawnScriptUploaded uploads start_workers_local.sh to
// remoteSpawnScriptPath if it hasn't been done yet this session.
func (m *Manager) ensureSpawnScriptUploaded(ctx context.Context, cli *ssm.Client, instanceID string) error {
	m.mu.Lock()
	already := m.uploadedScript[instanceID]
	m.mu.Unlock()
	if already {
		return nil
	}

	// Ensure the directory exists.
	if _, err := runSSMShell(ctx, cli, instanceID, "mkdir -p /tmp"); err != nil {
		return fmt.Errorf("mkdir /tmp: %w", err)
	}
	if err := writeRemoteFileFromBytes(ctx, cli, instanceID, spawnScript, remoteSpawnScriptPath); err != nil {
		return fmt.Errorf("write spawn script: %w", err)
	}
	if _, err := runSSMShell(ctx, cli, instanceID, "chmod +x "+shQuote(remoteSpawnScriptPath)); err != nil {
		return fmt.Errorf("chmod spawn script: %w", err)
	}
	m.mu.Lock()
	m.uploadedScript[instanceID] = true
	m.mu.Unlock()
	return nil
}

// runSSMShellT is like runSSMShell but uses an explicit timeout instead
// of the package-wide ssmPerCommandTimeout. Use this for operations that
// can take longer than 30 s (e.g. starting multiple workers).
func runSSMShellT(ctx context.Context, cli *ssm.Client, instanceID, command string, timeout time.Duration) (string, error) {
	ctxOne, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	out, err := cli.SendCommand(ctxOne, &ssm.SendCommandInput{
		DocumentName: aws.String("AWS-RunShellScript"),
		InstanceIds:  []string{instanceID},
		Parameters:   map[string][]string{"commands": {command}},
	})
	if err != nil {
		return "", fmt.Errorf("ssm send command: %w", err)
	}
	if out.Command == nil || out.Command.CommandId == nil {
		return "", errors.New("ssm send command: missing command id")
	}
	return waitCommandInvocation(ctxOne, cli, aws.ToString(out.Command.CommandId), instanceID)
}

// ec2StoppingFor returns how long the manager has been waiting for the
// worker to exit, or 0 if no stop was ever requested.
func (m *Manager) ec2StoppingFor(id string, now time.Time) time.Duration {
	m.mu.Lock()
	defer m.mu.Unlock()
	t, ok := m.stopRequested[id]
	if !ok {
		return 0
	}
	return now.Sub(t)
}

// ec2MarkExited transitions a worker to StateExited.
func (m *Manager) ec2MarkExited(id, reason string) {
	now := time.Now().UTC()
	transitioned := false
	m.reg.Update(id, func(rw *registry.Worker) {
		if rw.State == registry.StateExited {
			return
		}
		rw.State = registry.StateExited
		rw.StoppedAt = &now
		transitioned = true
	})
	if transitioned {
		log.Printf("ec2 exit %s: %s", id, reason)
		m.mu.Lock()
		delete(m.stopRequested, id)
		m.mu.Unlock()
	}
}

// ec2RunForWorker returns the run state for the given worker, or nil if
// this manager doesn't own that worker.
func (m *Manager) ec2RunForWorker(id string) *ec2RunState {
	w, ok := m.reg.Get(id)
	if !ok {
		return nil
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.runs[w.RunID]
}

// ec2RunningWorkerIDs returns IDs of every EC2-managed worker currently
// in Running or Stopping state.
func (m *Manager) ec2RunningWorkerIDs() []string {
	m.mu.Lock()
	known := make(map[string]bool, len(m.runs))
	for runID := range m.runs {
		known[runID] = true
	}
	m.mu.Unlock()
	var out []string
	for _, w := range m.reg.List() {
		if !known[w.RunID] {
			continue
		}
		if w.State == registry.StateRunning || w.State == registry.StateStopping {
			out = append(out, w.ID)
		}
	}
	return out
}

// mirrorManifest writes the remote manifest verbatim under
// <cacheDir>/runs/ec2-<host>/<run-id>/manifest.json for offline
// inspection.
func (m *Manager) mirrorManifest(hostID, runID string, raw []byte) {
	if m.cacheDir == "" {
		return
	}
	dir := filepath.Join(m.cacheDir, "runs", "ec2-"+hostID, runID)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		log.Printf("ec2: mirror manifest mkdir: %v", err)
		return
	}
	dst := filepath.Join(dir, "manifest.json")
	tmp := dst + ".tmp"
	if err := os.WriteFile(tmp, raw, 0o644); err != nil {
		log.Printf("ec2: mirror manifest write: %v", err)
		return
	}
	if err := os.Rename(tmp, dst); err != nil {
		log.Printf("ec2: mirror manifest rename: %v", err)
	}
}

// ec2RemoteManifest is the subset of the on-instance manifest.json the
// controller cares about. The bash helper writes additional fields
// (hostname, os, …) which we mirror verbatim via mirrorManifest.
type ec2RemoteManifest struct {
	RunID     string                    `json:"run_id"`
	StartedAt string                    `json:"started_at"`
	Host      string                    `json:"host"`
	Hostname  string                    `json:"hostname"`
	OS        string                    `json:"os"`
	BaseDir   string                    `json:"base_dir"`
	WorkerBin string                    `json:"worker_bin"`
	Config    string                    `json:"config"`
	Args      []string                  `json:"args"`
	Workers   []ec2RemoteManifestWorker `json:"workers"`
}

type ec2RemoteManifestWorker struct {
	ID      string `json:"id"`
	PID     int    `json:"pid"`
	Log     string `json:"log"`
	Pidfile string `json:"pidfile"`
}

// ec2ParseSpawnOutput plucks the run dir + manifest from the SSM
// command output fenced by sentinel markers.
func ec2ParseSpawnOutput(out []byte) (runDir string, manifest []byte, err error) {
	rd, ok := ec2ExtractBlock(out, []byte(ec2RunDirBeginMarker), []byte(ec2RunDirEndMarker))
	if !ok {
		return "", nil, errors.New("run-dir sentinel not found in remote output")
	}
	rd = strings.TrimSpace(rd)
	mraw, ok := ec2ExtractBlock(out, []byte(ec2ManifestBeginMark), []byte(ec2ManifestEndMarker))
	if !ok {
		return "", nil, errors.New("manifest sentinel not found in remote output")
	}
	return rd, []byte(strings.TrimSpace(mraw)), nil
}

// ec2ExtractBlock returns the content between begin and end markers.
func ec2ExtractBlock(buf, begin, end []byte) (string, bool) {
	bi := bytes.Index(buf, begin)
	if bi < 0 {
		return "", false
	}
	rest := buf[bi+len(begin):]
	if nl := bytes.IndexByte(rest, '\n'); nl >= 0 {
		rest = rest[nl+1:]
	}
	ei := bytes.Index(rest, end)
	if ei < 0 {
		return "", false
	}
	return string(rest[:ei]), true
}

// ec2NewWorkerID returns a fresh w-XXXXXXXXXXXX worker identifier.
func ec2NewWorkerID() string {
	var b [6]byte
	if _, err := rand.Read(b[:]); err != nil {
		return fmt.Sprintf("w-%x", time.Now().UnixNano())
	}
	return "w-" + hex.EncodeToString(b[:])
}
