// Command tm-worker-farm is the worker-farm controller. See
// worker-farm/README.md for the full design.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/adopt"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/api"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/codespace"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/identity"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/inventory"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/local"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/paths"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/pidfile"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/recent"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/registry"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/webassets"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/worker"
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, "tm-worker-farm:", err)
		os.Exit(1)
	}
}

func run() error {
	var (
		addr          string
		port          int
		configArg     string
		workerBin     string
		logFileArg    string
		restartLast   bool
		inventoryPath string
	)

	defaultConfig, _ := paths.DefaultWorkerConfig()
	defaultInventory, _ := inventory.DefaultPath()

	flag.StringVar(&addr, "addr", "127.0.0.1", "Bind address (loopback by default; do not bind publicly without auth).")
	flag.IntVar(&port, "port", 8090, "TCP port to listen on. Fails fast if the port is in use.")
	flag.StringVar(&configArg, "config", defaultConfig, "Path to config-worker.json passed to every spawned worker via -c.")
	flag.StringVar(&workerBin, "worker-bin", "", "Override path to tm-worker. Default: $PATH lookup, then OS-specific fallback.")
	flag.StringVar(&logFileArg, "log-file", "", "Append controller log to this file in addition to stderr. Default: <cacheDir>/controller.log. Pass '-' to disable.")
	flag.BoolVar(&restartLast, "restart-last", false, "On startup, re-spawn the most recent run from recent.json.")
	flag.StringVar(&inventoryPath, "inventory", defaultInventory, "Path to hosts.json. Missing file ⇒ synthesized single-host {id:local}.")
	flag.Parse()

	cacheDir, err := paths.CacheDir()
	if err != nil {
		return fmt.Errorf("resolve cache dir: %w", err)
	}
	if err := os.MkdirAll(cacheDir, 0o755); err != nil {
		return fmt.Errorf("create cache dir %s: %w", cacheDir, err)
	}

	// Warn (don't migrate) if the pre-Phase-2 cache dir still exists.
	// Operators rename their habits, not their data; the legacy directory
	// is harmless but no longer read.
	if legacy, lerr := paths.LegacyCacheDir(); lerr == nil {
		if info, statErr := os.Stat(legacy); statErr == nil && info.IsDir() && legacy != cacheDir {
			log.Printf("note: legacy cache dir still present at %s (no longer used; safe to remove)", legacy)
		}
	}

	// Tee the controller log to a file (in addition to stderr) unless
	// disabled with --log-file=-. Default: <cacheDir>/controller.log.
	logFilePath := logFileArg
	if logFilePath == "" {
		logFilePath = paths.ControllerLogPath(cacheDir)
	}
	if logFilePath != "-" {
		if err := os.MkdirAll(filepath.Dir(logFilePath), 0o755); err != nil {
			return fmt.Errorf("create log dir for %s: %w", logFilePath, err)
		}
		lf, err := os.OpenFile(logFilePath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
		if err != nil {
			return fmt.Errorf("open log file %s: %w", logFilePath, err)
		}
		// Don't close it: it must outlive run() so late writes (during
		// graceful shutdown) still land. The OS reclaims it on exit.
		log.SetOutput(io.MultiWriter(os.Stderr, lf))
	}

	// Single-instance guard.
	release, err := pidfile.Acquire(paths.PidfilePath(cacheDir))
	if err != nil {
		return err
	}
	defer release()

	// Resolve tm-worker (Tactical Decision #6) — fail at startup, not on
	// the first spawn request.
	resolvedWorker, err := worker.Resolve(workerBin)
	if err != nil {
		return err
	}

	// Validate the configured worker config exists if it's the default;
	// if the user explicitly passed --config to a missing file, that's
	// their problem to surface later. We just warn here.
	if configArg != "" {
		if _, statErr := os.Stat(configArg); statErr != nil {
			log.Printf("warning: worker config not readable yet: %s (%v)", configArg, statErr)
		}
	}

	controllerID, fresh, err := identity.LoadOrCreate(paths.IdentityPath(cacheDir))
	if err != nil {
		return fmt.Errorf("load controller identity: %w", err)
	}
	if fresh {
		log.Printf("minted new controller id: %s", controllerID)
	} else {
		log.Printf("loaded existing controller id: %s", controllerID)
	}
	_, history, _, _ := identity.Load(paths.IdentityPath(cacheDir))
	reg := registry.New()
	mgr := local.New(local.Options{
		Registry:     reg,
		CacheDir:     cacheDir,
		WorkerBin:    resolvedWorker,
		ConfigPath:   configArg,
		ControllerID: controllerID,
	})
	recentLog := recent.New(paths.RecentRunsPath(cacheDir))

	// Adoption pass: scan worker-NN.adopt sentinels left by previous
	// controller runs and classify each. ClassMine → auto-adopt;
	// ClassStale → register as exited; ClassTheirs → quarantine for
	// operator decision through the UI.
	cands, scanErr := adopt.Scan(cacheDir, history)
	if scanErr != nil {
		log.Printf("adopt scan: %v", scanErr)
	}
	var quarantine []adopt.Candidate
	mineN, staleN, theirsN := 0, 0, 0
	for _, c := range cands {
		switch c.Class {
		case adopt.ClassMine:
			mgr.Adopt(c)
			mineN++
		case adopt.ClassStale:
			mgr.RegisterStale(c)
			staleN++
		case adopt.ClassTheirs:
			quarantine = append(quarantine, c)
			theirsN++
		}
	}
	mgr.SetQuarantine(quarantine)
	if len(cands) > 0 {
		log.Printf("adopt scan: %d candidate(s): adopted=%d stale=%d quarantined=%d", len(cands), mineN, staleN, theirsN)
	}

	inv, synthesized, err := inventory.Load(inventoryPath)
	if err != nil {
		// Validation errors are *inventory.Error and already include
		// the path + offending host index; surface verbatim.
		return err
	}
	if synthesized {
		log.Printf("inventory: no file at %s, using synthesized [{id:local,backend:local}]", inventoryPath)
	} else {
		log.Printf("inventory: %d host(s) loaded from %s", len(inv.Hosts), inventoryPath)
	}

	// Codespace manager is built whenever the inventory has at least
	// one codespace host; constructing it is cheap and lets the API
	// surface remote support uniformly. The poll goroutine starts
	// alongside the HTTP server below.
	var csmgr *codespace.Manager
	for _, h := range inv.Hosts {
		if h.Backend == inventory.BackendCodespace {
			csmgr = codespace.New(codespace.Options{
				Registry:     reg,
				Inventory:    inv,
				CacheDir:     cacheDir,
				ControllerID: controllerID,
			})
			break
		}
	}

	srv := api.New(api.Options{
		WebFS:      webassets.FS(),
		Registry:   reg,
		Manager:    mgr,
		Codespace:  csmgr,
		Recent:     recentLog,
		ConfigPath: configArg,
		WorkerBin:  resolvedWorker,
		CacheDir:   cacheDir,
		Inventory:  inv,
	})

	listenAddr := net.JoinHostPort(addr, fmt.Sprint(port))
	listener, err := net.Listen("tcp", listenAddr)
	if err != nil {
		// Tactical Decision #4: fail fast with a clear, port-naming message.
		if isAddrInUse(err) {
			return fmt.Errorf("port %d on %s is already in use; pass --port <N> to pick another", port, addr)
		}
		return fmt.Errorf("listen on %s: %w", listenAddr, err)
	}

	httpServer := &http.Server{
		Handler:           srv.Handler(),
		ReadHeaderTimeout: 5 * time.Second,
	}

	log.Printf("tm-worker-farm listening on http://%s/", listenAddr)
	log.Printf("  controller id: %s", controllerID)
	log.Printf("  cache dir:    %s", cacheDir)
	log.Printf("  worker bin:   %s", resolvedWorker)
	log.Printf("  worker conf:  %s", configArg)
	log.Printf("  pidfile:      %s", paths.PidfilePath(cacheDir))
	if logFilePath != "-" {
		log.Printf("  log file:     %s", logFilePath)
	} else {
		log.Print("  log file:     (disabled, stderr only)")
	}

	if restartLast {
		if entry, ok, err := recentLog.Latest(); err != nil {
			log.Printf("--restart-last: %v", err)
		} else if !ok {
			log.Print("--restart-last: no prior run recorded")
		} else {
			log.Printf("--restart-last: respawning %d worker(s) from %s", entry.Count, entry.Timestamp.Format(time.RFC3339))
			results := mgr.Spawn(context.Background(), entry.Count, entry.Args)
			for _, r := range results {
				if !r.OK {
					log.Printf("  spawn failed: %s: %s", r.ID, r.Error)
				}
			}
		}
	}

	// Graceful shutdown on SIGINT/SIGTERM.
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	// Codespace liveness poll. Started after `ctx` so the same
	// signal that drains HTTP also stops polling.
	if csmgr != nil {
		go csmgr.Run(ctx)
	}

	serveErr := make(chan error, 1)
	go func() {
		err := httpServer.Serve(listener)
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			serveErr <- err
			return
		}
		serveErr <- nil
	}()

	select {
	case err := <-serveErr:
		return err
	case <-ctx.Done():
		log.Print("shutdown signal received; draining HTTP server")
	}

	shutdownCtx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	// Workers are detached and outlive the controller by design.
	// We do NOT call mgr.StopAll here. Manifests + .adopt sentinels
	// stay on disk so the next controller launch picks them up via
	// the adoption scan. Operators who actually want to stop workers
	// must use the UI / `POST /workers/stop-all` before quitting.
	if err := httpServer.Shutdown(shutdownCtx); err != nil {
		return fmt.Errorf("graceful shutdown: %w", err)
	}
	return nil
}

func newControllerID() string {
	return fmt.Sprintf("ctl-%x", time.Now().UnixNano())
}

// (newControllerID is kept as a fallback signature for now but is no
// longer the source of truth for controller identity — see
// internal/identity.LoadOrCreate.)

// isAddrInUse returns true for the platform-specific "port already in
// use" errno without pulling in syscall constants we'd otherwise have to
// duplicate. It's a string match because Go's net package doesn't
// expose a stable error value for this case across platforms.
func isAddrInUse(err error) bool {
	if err == nil {
		return false
	}
	msg := strings.ToLower(err.Error())
	return strings.Contains(msg, "address already in use") ||
		strings.Contains(msg, "only one usage of each socket address") ||
		strings.Contains(msg, "bind: permission denied") // best-effort
}
