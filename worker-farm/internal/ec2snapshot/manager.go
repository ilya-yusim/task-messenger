package ec2snapshot

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
	"sort"
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
	remoteSpawnScriptPath     = "/tmp/tm-farm-spawn.sh"
	managerSpawnTimeout       = 4 * time.Minute
	managerStopTimeout        = 90 * time.Second
	managerLogTimeout         = 30 * time.Second
	defaultManagerPollPeriod  = 5 * time.Second
	defaultManagerGracePeriod = 10 * time.Second
	defaultIdleTimeout        = 1 * time.Minute
	ec2RunDirBeginMarker      = "===TM_FARM_RUN_DIR_BEGIN==="
	ec2RunDirEndMarker        = "===TM_FARM_RUN_DIR_END==="
	ec2ManifestBeginMark      = "===TM_FARM_MANIFEST_BEGIN==="
	ec2ManifestEndMarker      = "===TM_FARM_MANIFEST_END==="
)

type ec2SnapshotRunState struct {
	runID        string
	hostID       string
	instanceID   string
	ec2Cfg       inventory.EC2SnapshotCfg
	remoteRunDir string
	workerIDs    []string
}

type Manager struct {
	reg            *registry.Registry
	inv            *inventory.Inventory
	cacheDir       string
	controllerID   string
	gracePeriod    time.Duration
	pollPeriod     time.Duration
	idleTimeout    time.Duration
	mu             sync.Mutex
	runs           map[string]*ec2SnapshotRunState
	stopRequested  map[string]time.Time
	uploadedScript map[string]bool
	idleSince      map[string]time.Time
}

type Options struct {
	Registry     *registry.Registry
	Inventory    *inventory.Inventory
	CacheDir     string
	ControllerID string
	GracePeriod  time.Duration
	PollPeriod   time.Duration
	IdleTimeout  time.Duration
}

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
		runs:           map[string]*ec2SnapshotRunState{},
		stopRequested:  map[string]time.Time{},
		uploadedScript: map[string]bool{},
		idleSince:      map[string]time.Time{},
	}
}

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
	if host.Backend != inventory.BackendEC2Snapshot || host.EC2Snapshot == nil {
		return failAll(fmt.Sprintf("host %q is not an ec2-snapshot backend", host.ID))
	}
	spawnCtx, cancelSpawn := context.WithTimeout(ctx, managerSpawnTimeout)
	defer cancelSpawn()
	inst, err := EnsureInstance(spawnCtx, *host.EC2Snapshot, host.ID, m.controllerID)
	if err != nil {
		return failAll(fmt.Sprintf("ensure ec2 snapshot instance: %v", err))
	}
	cl, err := newClients(spawnCtx, inst.Region)
	if err != nil {
		return failAll(fmt.Sprintf("create aws clients: %v", err))
	}
	if err := m.ensureSpawnScriptUploaded(spawnCtx, cl.ssm, inst.InstanceID); err != nil {
		return failAll(fmt.Sprintf("upload spawn script: %v", err))
	}
	workerBin := host.EC2Snapshot.WorkerBin
	if strings.TrimSpace(workerBin) == "" {
		workerBin = "tm-worker"
	}
	cfgPath := host.EC2Snapshot.Config
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
	innerSpawnCmd := fmt.Sprintf("set -e\nsource %s -n %d -b %s -c %s%s\nif [ -z \"${run_dir:-}\" ]; then echo 'spawn helper did not set run_dir' >&2; exit 1; fi\necho '%s'\necho \"$run_dir\"\necho '%s'\necho '%s'\ncat \"$run_dir/manifest.json\"\necho '%s'\n",
		shQuote(remoteSpawnScriptPath), count, shQuote(workerBin), shQuote(cfgPath), extraPart.String(), ec2RunDirBeginMarker, ec2RunDirEndMarker, ec2ManifestBeginMark, ec2ManifestEndMarker)
	spawnCmd := "bash -lc " + shQuote(innerSpawnCmd)
	out, err := runSSMShell(spawnCtx, cl.ssm, inst.InstanceID, spawnCmd)
	if err != nil {
		log.Printf("ec2snapshot spawn host=%s instance=%s count=%d: ssm failed: %v\n--- output ---\n%s", host.ID, inst.InstanceID, count, err, string(out))
		return failAll(fmt.Sprintf("ssm spawn: %v", err))
	}
	runDir, manifestRaw, perr := ec2ParseSpawnOutput([]byte(out))
	if perr != nil {
		log.Printf("ec2snapshot spawn host=%s instance=%s: parse output: %v\n--- output ---\n%s", host.ID, inst.InstanceID, perr, out)
		return failAll(fmt.Sprintf("parse remote manifest: %v", perr))
	}
	var rm ec2RemoteManifest
	if err := json.Unmarshal(manifestRaw, &rm); err != nil {
		log.Printf("ec2snapshot spawn host=%s instance=%s run_dir=%s: initial manifest decode failed: %v; attempting direct refetch", host.ID, inst.InstanceID, runDir, err)
		refetchCtx, cancelRefetch := context.WithTimeout(spawnCtx, 30*time.Second)
		refetched, ferr := runSSMShell(refetchCtx, cl.ssm, inst.InstanceID, "cat "+shQuote(filepath.Join(runDir, "manifest.json")))
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
	state := &ec2SnapshotRunState{runID: rm.RunID, hostID: host.ID, instanceID: inst.InstanceID, ec2Cfg: *host.EC2Snapshot, remoteRunDir: runDir, workerIDs: make([]string, 0, len(rm.Workers))}
	startedAt := time.Now().UTC()
	baseArgs := append([]string{}, rm.Args...)
	for i, rw := range rm.Workers {
		slot := i + 1
		id := ec2NewWorkerID()
		w := &registry.Worker{ID: id, PID: rw.PID, State: registry.StateRunning, RunID: rm.RunID, Slot: slot, StartedAt: startedAt, Args: append([]string{}, baseArgs...), LogPath: rw.Log, Host: host.ID}
		m.reg.Add(w)
		state.workerIDs = append(state.workerIDs, id)
		results[i] = SpawnResult{ID: id, OK: true, PID: rw.PID}
	}
	m.mu.Lock()
	m.runs[rm.RunID] = state
	delete(m.idleSince, inst.InstanceID)
	m.mu.Unlock()
	log.Printf("ec2snapshot spawn host=%s instance=%s run=%s count=%d remote_dir=%s", host.ID, inst.InstanceID, rm.RunID, len(rm.Workers), runDir)
	return results
}

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
		return fmt.Errorf("worker %s: no ec2 run state (controller restart? not supported for ec2-snapshot backend)", workerID)
	}
	m.reg.Update(workerID, func(w *registry.Worker) { w.State = registry.StateStopping })
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
		return fmt.Errorf("ec2-snapshot stop: create clients: %w", err)
	}
	stopCtx, cancel := context.WithTimeout(ctx, managerStopTimeout)
	defer cancel()
	if _, err := runSSMShell(stopCtx, cl.ssm, st.instanceID, stopScript); err != nil {
		return fmt.Errorf("ssm stop: %w", err)
	}
	return nil
}

func (m *Manager) StopAll(ctx context.Context) {
	ids := m.ec2RunningWorkerIDs()
	if len(ids) == 0 {
		return
	}
	log.Printf("ec2snapshot stop-all: %d worker(s)", len(ids))
	var wg sync.WaitGroup
	for _, id := range ids {
		wg.Add(1)
		go func(id string) {
			defer wg.Done()
			if err := m.Stop(ctx, id); err != nil {
				log.Printf("ec2snapshot stop %s: %v", id, err)
			}
		}(id)
	}
	wg.Wait()
}

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
	// Keep runs in m.runs to allow idle termination to complete.
	// Runs will be cleaned up when the instance terminates.
	m.mu.Unlock()
	log.Printf("ec2snapshot purge %s: run=%s slot=%02d", id, w.RunID, w.Slot)
	return nil
}

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
		cl, cerr := newClients(ctx, st.ec2Cfg.Region)
		if cerr != nil {
			return nil, fmt.Errorf("worker %s has no log path", workerID)
		}
		if resolved, rerr := resolveRemoteWorkerLogPath(ctx, cl.ssm, st.instanceID, w.PID); rerr == nil && resolved != "" {
			w.LogPath = resolved
			m.reg.Update(workerID, func(rw *registry.Worker) {
				rw.LogPath = resolved
			})
		} else {
			return nil, fmt.Errorf("worker %s has no log path", workerID)
		}
	}
	tailArg := "-n +1"
	if lines > 0 {
		tailArg = fmt.Sprintf("-n %d", lines)
	}
	cmd := fmt.Sprintf("tail %s -- %s", tailArg, shQuote(w.LogPath))
	cl, err := newClients(ctx, st.ec2Cfg.Region)
	if err != nil {
		return nil, fmt.Errorf("ec2-snapshot tail-log: create clients: %w", err)
	}
	logCtx, cancel := context.WithTimeout(ctx, managerLogTimeout)
	defer cancel()
	out, err := runSSMShell(logCtx, cl.ssm, st.instanceID, cmd)
	if err != nil {
		return []byte(out), err
	}
	return []byte(out), nil
}

func (m *Manager) IsEC2SnapshotWorker(id string) bool { return m.ec2RunForWorker(id) != nil }

// Adopt registers a previously-discovered live worker (from a remote
// manifest) into the registry as a running, supervised entry. For EC2-snapshot,
// liveness is checked by the polling loop.
//
// Returns the registry ID assigned to the adopted worker. Safe to call
// multiple times for the same worker — the registry rejects duplicates.
func (m *Manager) Adopt(hostID string, instanceID string, ec2Cfg inventory.EC2SnapshotCfg, runID string, slot int, pid int, logPath string, args []string) string {
	if pid <= 0 {
		return ""
	}
	// Derive stable, compact ID for adopted remote workers.
	id := remoteAdoptedID(hostIndexFromInventory(m.inv, hostID), runID, slot, pid)

	w := &registry.Worker{
		ID:        id,
		PID:       pid,
		State:     registry.StateRunning,
		RunID:     runID,
		Slot:      slot,
		StartedAt: time.Now().UTC(),
		Args:      args,
		LogPath:   logPath,
		Host:      hostID,
		Adopted:   true,
	}
	if !m.reg.Add(w) {
		return id // already adopted in a previous pass
	}

	m.mu.Lock()
	st := m.runs[runID]
	if st == nil {
		st = &ec2SnapshotRunState{
			runID:      runID,
			hostID:     hostID,
			instanceID: instanceID,
			ec2Cfg:     ec2Cfg,
			workerIDs:  make([]string, 0, 1),
		}
		m.runs[runID] = st
	}
	st.workerIDs = append(st.workerIDs, id)
	m.mu.Unlock()

	log.Printf("adopt %s (adopted=true): run=%s slot=%02d pid=%d log=%s", id, runID, slot, pid, logPath)
	return id
}

// RegisterStale folds a dead worker from a remote manifest into the
// registry as an already-exited row. Exit code is null (we did not
// parent the process and cannot retrieve its exit code remotely).
func (m *Manager) RegisterStale(hostID string, runID string, slot int, pid int, logPath string, args []string) string {
	id := remoteAdoptedID(hostIndexFromInventory(m.inv, hostID), runID, slot, pid)
	now := time.Now().UTC()
	w := &registry.Worker{
		ID:        id,
		PID:       pid,
		State:     registry.StateExited,
		RunID:     runID,
		Slot:      slot,
		StartedAt: now,
		StoppedAt: &now,
		Args:      args,
		LogPath:   logPath,
		Host:      hostID,
		Adopted:   true,
	}
	if !m.reg.Add(w) {
		return id
	}
	log.Printf("stale %s (adopted=true): run=%s slot=%02d pid=%d (process gone, recorded as exited)", id, runID, slot, pid)
	return id
}

// AdoptOrphanedWorkers queries each EC2-snapshot instance for orphaned workers
// from previous runs and adopts them into the registry. Called once at
// startup.
func (m *Manager) AdoptOrphanedWorkers(ctx context.Context) (adopted, stale int) {
	var hostsTotal, hostsTried int
	log.Printf("ec2-snapshot adoption: begin scan")

	// For each EC2-snapshot host in inventory, query its instance for latest run.
	for _, host := range m.inv.Hosts {
		if host.Backend != inventory.BackendEC2Snapshot || host.EC2Snapshot == nil {
			continue
		}
		hostsTotal++
		hostsTried++
		log.Printf("ec2-snapshot adoption: host=%s region=%s: checking managed instance", host.ID, host.EC2Snapshot.Region)

		cl, err := newClients(ctx, host.EC2Snapshot.Region)
		if err != nil {
			log.Printf("ec2-snapshot adoption: host=%s: create clients: %v", host.ID, err)
			continue
		}

		// Find the managed instance for this host (if it exists).
		inst, err := findManagedInstance(ctx, cl.ec2, host.ID, m.controllerID)
		if err != nil {
			log.Printf("ec2-snapshot adoption: host=%s: find instance: %v", host.ID, err)
			continue
		}
		if inst == nil {
			// No instance for this host yet; nothing to adopt.
			log.Printf("ec2-snapshot adoption: host=%s: no managed instance found", host.ID)
			continue
		}

		instanceID := aws.ToString(inst.InstanceId)
		log.Printf("ec2-snapshot adoption: host=%s instance=%s: probing latest run", host.ID, instanceID)

		// Query instance for latest run ID.
		latestCmd := "cache_root=\"${XDG_CACHE_HOME:-$HOME/.cache}/tm-worker-farm/runs\"; cat \"$cache_root/latest.txt\" 2>/dev/null || true"
		latestOut, err := runSSMShell(ctx, cl.ssm, instanceID, latestCmd)
		if err != nil {
			log.Printf("ec2-snapshot adoption: host=%s instance=%s: latest.txt probe failed: %v", host.ID, instanceID, err)
			continue
		}
		if strings.TrimSpace(latestOut) == "" {
			log.Printf("ec2-snapshot adoption: host=%s instance=%s: latest.txt empty or missing", host.ID, instanceID)
			fallbackCmd := "cache_root=\"${XDG_CACHE_HOME:-$HOME/.cache}/tm-worker-farm/runs\"; if [ -d \"$cache_root\" ]; then ls -1 \"$cache_root\" 2>/dev/null | sort -r | while read -r d; do [ -f \"$cache_root/$d/manifest.json\" ] && { echo \"$d\"; break; }; done; fi"
			fallbackOut, fbErr := runSSMShell(ctx, cl.ssm, instanceID, fallbackCmd)
			if fbErr != nil {
				log.Printf("ec2-snapshot adoption: host=%s instance=%s: fallback run scan failed: %v", host.ID, instanceID, fbErr)
				continue
			}
			if strings.TrimSpace(fallbackOut) == "" {
				log.Printf("ec2-snapshot adoption: host=%s instance=%s: fallback found no run dirs with manifest", host.ID, instanceID)
				procs, perr := discoverEC2SnapshotOrphanProcesses(ctx, cl.ssm, instanceID)
				if perr != nil {
					log.Printf("ec2-snapshot adoption: host=%s instance=%s: process fallback failed: %v", host.ID, instanceID, perr)
					continue
				}
				if len(procs) == 0 {
					log.Printf("ec2-snapshot adoption: host=%s instance=%s: process fallback found no tm-worker processes", host.ID, instanceID)
					continue
				}
				log.Printf("ec2-snapshot adoption: host=%s instance=%s: process fallback discovered %d worker process(es)", host.ID, instanceID, len(procs))
				for _, p := range procs {
					runID := fmt.Sprintf("orphan-%s-%d", instanceID, p.PID)
					id := m.Adopt(host.ID, instanceID, *host.EC2Snapshot, runID, 1, p.PID, p.LogPath, p.Args)
					if id != "" {
						adopted++
					}
				}
				continue
			}
			runID := strings.TrimSpace(fallbackOut)
			latestOut = runID
			log.Printf("ec2-snapshot adoption: host=%s instance=%s: fallback selected run=%s", host.ID, instanceID, runID)
		}
		runID := strings.TrimSpace(latestOut)
		log.Printf("ec2-snapshot adoption: host=%s instance=%s: latest run=%s", host.ID, instanceID, runID)

		// Query manifest for that run.
		manifestCmd := fmt.Sprintf("run_id=%s; cache_root=\"${XDG_CACHE_HOME:-$HOME/.cache}/tm-worker-farm/runs\"; cat \"$cache_root/$run_id/manifest.json\" 2>/dev/null || true", shQuote(runID))
		manifestOut, err := runSSMShell(ctx, cl.ssm, instanceID, manifestCmd)
		if err != nil {
			log.Printf("ec2-snapshot adoption: host=%s instance=%s run=%s: manifest probe failed: %v", host.ID, instanceID, runID, err)
			continue
		}
		if strings.TrimSpace(manifestOut) == "" {
			log.Printf("ec2-snapshot adoption: host=%s instance=%s run=%s: manifest missing/empty", host.ID, instanceID, runID)
			continue // No manifest; nothing to adopt.
		}

		// Parse manifest.
		var rm ec2RemoteManifest
		if err := json.Unmarshal([]byte(manifestOut), &rm); err != nil {
			log.Printf("ec2-snapshot adoption: host=%s run=%s: parse manifest: %v", host.ID, runID, err)
			continue
		}
		log.Printf("ec2-snapshot adoption: host=%s instance=%s run=%s: manifest workers=%d", host.ID, instanceID, rm.RunID, len(rm.Workers))

		// Adopt each worker (we assume they're all alive initially; polling will catch deaths).
		for i, rw := range rm.Workers {
			slot := i + 1
			id := m.Adopt(host.ID, instanceID, *host.EC2Snapshot, rm.RunID, slot, rw.PID, rw.Log, rm.Args)
			if id != "" {
				adopted++
			}
		}
	}
	if hostsTotal == 0 {
		log.Printf("ec2-snapshot adoption: no ec2-snapshot hosts configured")
	} else {
		log.Printf("ec2-snapshot adoption: completed hosts=%d adopted=%d stale=%d", hostsTried, adopted, stale)
	}
	return adopted, stale
}

type ec2SnapshotOrphanProcess struct {
	PID     int
	Args    []string
	LogPath string
}

func discoverEC2SnapshotOrphanProcesses(ctx context.Context, ssmClient *ssm.Client, instanceID string) ([]ec2SnapshotOrphanProcess, error) {
	cmd := "ps -eo pid=,args= | grep -E '(^|[[:space:]/])tm-worker([[:space:]]|$)' | grep -v grep || true"
	out, err := runSSMShell(ctx, ssmClient, instanceID, cmd)
	if err != nil {
		return nil, err
	}
	var procs []ec2SnapshotOrphanProcess
	sc := bufio.NewScanner(strings.NewReader(out))
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" {
			continue
		}
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}
		pid, convErr := strconv.Atoi(fields[0])
		if convErr != nil || pid <= 0 {
			continue
		}
		p := ec2SnapshotOrphanProcess{PID: pid, Args: append([]string{}, fields[1:]...)}
		if lp, lerr := resolveRemoteWorkerLogPath(ctx, ssmClient, instanceID, pid); lerr == nil {
			p.LogPath = lp
		}
		procs = append(procs, p)
	}
	sort.Slice(procs, func(i, j int) bool { return procs[i].PID < procs[j].PID })
	return procs, nil
}

func resolveRemoteWorkerLogPath(ctx context.Context, ssmClient *ssm.Client, instanceID string, pid int) (string, error) {
	cmd := fmt.Sprintf("readlink -f /proc/%d/fd/1 2>/dev/null || true", pid)
	out, err := runSSMShell(ctx, ssmClient, instanceID, cmd)
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(out), nil
}

func remoteAdoptedID(hostIndex int, runID string, slot, pid int) string {
	host := fmt.Sprintf("h%02d", hostIndex)
	if strings.HasPrefix(runID, "orphan-") && pid > 0 {
		return fmt.Sprintf("%s-p%d", host, pid)
	}
	return fmt.Sprintf("%s-%s-%02d", host, shortIDTail(runID, 6), slot)
}

func hostIndexFromInventory(inv *inventory.Inventory, hostID string) int {
	if inv == nil {
		return 0
	}
	for i, h := range inv.Hosts {
		if h.ID == hostID {
			return i + 1 // 1-based index for operator readability
		}
	}
	return 0
}

func shortIDTail(s string, n int) string {
	if n <= 0 {
		return "x"
	}
	s = strings.TrimSpace(s)
	if s == "" {
		return "x"
	}
	if len(s) <= n {
		return s
	}
	return s[len(s)-n:]
}

func (m *Manager) pollOnce(ctx context.Context) {
	type watch struct {
		id    string
		pid   int
		state registry.State
	}
	type instInfo struct {
		watches []watch
		ec2Cfg  inventory.EC2SnapshotCfg
	}
	byInst := map[string]*instInfo{}
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
		script := fmt.Sprintf(`for pid in %s; do kill -0 "$pid" 2>/dev/null && echo "$pid" || true; done`, strings.Join(pids, " "))
		cl, clErr := newClients(ctx, info.ec2Cfg.Region)
		var out string
		var ssmErr error
		if clErr != nil {
			ssmErr = clErr
		} else {
			pollCtx, cancel := context.WithTimeout(ctx, 30*time.Second)
			out, ssmErr = runSSMShell(pollCtx, cl.ssm, instanceID, script)
			cancel()
		}
		if ssmErr != nil {
			log.Printf("ec2snapshot poll instance=%s: %v", instanceID, ssmErr)
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
			if w.state == registry.StateStopping {
				m.ec2MarkExited(w.id, "stop-request-completed")
				continue
			}
			m.ec2MarkExited(w.id, fmt.Sprintf("poll: pid %d not alive", w.pid))
		}
	}
	activeInsts := make(map[string]bool, len(byInst))
	for k := range byInst {
		activeInsts[k] = true
	}
	m.handleIdleInstances(ctx, activeInsts)
}

func (m *Manager) handleIdleInstances(ctx context.Context, active map[string]bool) {
	known := map[string]inventory.EC2SnapshotCfg{}
	m.mu.Lock()
	for _, st := range m.runs {
		known[st.instanceID] = st.ec2Cfg
	}
	m.mu.Unlock()
	now := time.Now().UTC()
	for instanceID, cfg := range known {
		if active[instanceID] {
			m.mu.Lock()
			delete(m.idleSince, instanceID)
			m.mu.Unlock()
			continue
		}
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
			log.Printf("ec2snapshot idle-terminate: instance %s idle for %s (>= %s); terminating", instanceID, idleDur.Round(time.Second), m.idleTimeout)
			stopCtx, cancel := context.WithTimeout(ctx, 60*time.Second)
			if err := TerminateInstance(stopCtx, cfg, instanceID); err != nil {
				log.Printf("ec2snapshot idle-terminate: TerminateInstance %s: %v", instanceID, err)
			} else {
				m.mu.Lock()
				delete(m.idleSince, instanceID)
				// Clean up runs for this terminated instance.
				for runID, st := range m.runs {
					if st.instanceID == instanceID {
						delete(m.runs, runID)
					}
				}
				m.mu.Unlock()
			}
			cancel()
		}
	}
}

func (m *Manager) ensureSpawnScriptUploaded(ctx context.Context, cli *ssm.Client, instanceID string) error {
	m.mu.Lock()
	already := m.uploadedScript[instanceID]
	m.mu.Unlock()
	if already {
		return nil
	}
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

func (m *Manager) ec2StoppingFor(id string, now time.Time) time.Duration {
	m.mu.Lock()
	defer m.mu.Unlock()
	t, ok := m.stopRequested[id]
	if !ok {
		return 0
	}
	return now.Sub(t)
}

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
		adoptedTag := ""
		if w, ok := m.reg.Get(id); ok && w.Adopted {
			adoptedTag = " (adopted=true)"
		}
		log.Printf("ec2snapshot exit %s%s: %s", id, adoptedTag, reason)
		m.mu.Lock()
		delete(m.stopRequested, id)
		m.mu.Unlock()
	}
}

func (m *Manager) ec2RunForWorker(id string) *ec2SnapshotRunState {
	w, ok := m.reg.Get(id)
	if !ok {
		return nil
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.runs[w.RunID]
}

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

func (m *Manager) mirrorManifest(hostID, runID string, raw []byte) {
	if m.cacheDir == "" {
		return
	}
	dir := filepath.Join(m.cacheDir, "runs", "ec2-snapshot-"+hostID, runID)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		log.Printf("ec2snapshot: mirror manifest mkdir: %v", err)
		return
	}
	dst := filepath.Join(dir, "manifest.json")
	tmp := dst + ".tmp"
	if err := os.WriteFile(tmp, raw, 0o644); err != nil {
		log.Printf("ec2snapshot: mirror manifest write: %v", err)
		return
	}
	if err := os.Rename(tmp, dst); err != nil {
		log.Printf("ec2snapshot: mirror manifest rename: %v", err)
	}
}

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

func ec2NewWorkerID() string {
	var b [6]byte
	if _, err := rand.Read(b[:]); err != nil {
		return fmt.Sprintf("w-%x", time.Now().UnixNano())
	}
	return "w-" + hex.EncodeToString(b[:])
}
