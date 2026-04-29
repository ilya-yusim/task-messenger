// Command tm-worker-farm is the worker-farm controller. See
// worker-farm/README.md for the full design.
package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
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

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/api"
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
		addr        string
		port        int
		configArg   string
		workerBin   string
		logFileArg  string
		restartLast bool
	)

	defaultConfig, _ := paths.DefaultWorkerConfig()

	flag.StringVar(&addr, "addr", "127.0.0.1", "Bind address (loopback by default; do not bind publicly without auth).")
	flag.IntVar(&port, "port", 8090, "TCP port to listen on. Fails fast if the port is in use.")
	flag.StringVar(&configArg, "config", defaultConfig, "Path to config-worker.json passed to every spawned worker via -c.")
	flag.StringVar(&workerBin, "worker-bin", "", "Override path to tm-worker. Default: $PATH lookup, then OS-specific fallback.")
	flag.StringVar(&logFileArg, "log-file", "", "Append controller log to this file in addition to stderr. Default: <cacheDir>/controller.log. Pass '-' to disable.")
	flag.BoolVar(&restartLast, "restart-last", false, "On startup, re-spawn the most recent run from recent.json.")
	flag.Parse()

	cacheDir, err := paths.CacheDir()
	if err != nil {
		return fmt.Errorf("resolve cache dir: %w", err)
	}
	if err := os.MkdirAll(cacheDir, 0o755); err != nil {
		return fmt.Errorf("create cache dir %s: %w", cacheDir, err)
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

	controllerID := newControllerID()
	reg := registry.New()
	mgr := local.New(local.Options{
		Registry:     reg,
		CacheDir:     cacheDir,
		WorkerBin:    resolvedWorker,
		ConfigPath:   configArg,
		ControllerID: controllerID,
	})
	recentLog := recent.New(paths.RecentRunsPath(cacheDir))

	srv := api.New(api.Options{
		WebFS:      webassets.FS(),
		Registry:   reg,
		Manager:    mgr,
		Recent:     recentLog,
		ConfigPath: configArg,
		WorkerBin:  resolvedWorker,
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
	log.Print("stopping all workers")
	mgr.StopAll(shutdownCtx)
	if err := httpServer.Shutdown(shutdownCtx); err != nil {
		return fmt.Errorf("graceful shutdown: %w", err)
	}
	return nil
}

func newControllerID() string {
	var b [8]byte
	if _, err := rand.Read(b[:]); err != nil {
		return fmt.Sprintf("ctl-%x", time.Now().UnixNano())
	}
	return "ctl-" + hex.EncodeToString(b[:])
}

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
