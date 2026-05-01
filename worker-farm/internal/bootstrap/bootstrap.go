// Package bootstrap installs tm-worker on a remote host (codespace
// in Phase 3). Mirrors install_tm_worker_codespace.ps1: resolve
// release tag → discover linux-x86_64 asset → download locally →
// `gh codespace cp` it + the helper script → `gh codespace ssh -- bash`
// to run the makeself .run via the helper.
//
// Helper-script hashes are tracked in a small JSON state file so the
// next bootstrap on the same host can skip the cp if the helper
// hasn't changed. Asset uploads are always re-checked: the cost of
// hashing a 30 MB .run is negligible compared to the download.
package bootstrap

import (
	"context"
	"crypto/sha256"
	_ "embed"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/ilya-yusim/task-messenger/worker-farm/internal/gh"
)

// installerScript is the bash helper that runs the makeself .run on
// the remote. Embedded so the controller is single-binary —
// operators don't have to ship extras/scripts/ to use bootstrap.
//
//go:embed install_tm_worker_release.sh
var installerScript []byte

// installerScriptHash is computed lazily; cached at package scope
// because the embedded bytes never change at runtime.
var installerScriptHash = func() string {
	sum := sha256.Sum256(installerScript)
	return hex.EncodeToString(sum[:])
}()

// DefaultRepo is used when the operator doesn't specify one. Mirrors
// the install_tm_worker_codespace.ps1 fallback.
const DefaultRepo = "ilya-yusim/task-messenger"

// remoteDir on the codespace where the asset + helper land. Lives
// under ~/.local/share so it survives codespace restarts.
const remoteDir = "~/.local/share/tm-worker-farm"

// assetPattern matches the linux-x86_64 makeself asset name shape we
// publish. Same regex install_tm_worker_codespace.ps1 uses.
var assetPattern = regexp.MustCompile(`^tm-worker-v.*-linux-x86_64\.run$`)

// stateFileName lives next to the controller's hosts.json under
// $XDG_CACHE_HOME/tm-worker-farm and tracks per-host helper hashes
// so re-bootstraps skip the cp when nothing changed.
const stateFileName = "bootstrap-state.json"

// State is the on-disk record of what we've already uploaded per
// host. Schema is permissive: unknown fields are preserved on
// rewrite so we can extend later without breaking older controllers.
type State struct {
	Hosts map[string]HostState `json:"hosts"`
}

// HostState captures the last-known good upload for one host id.
type HostState struct {
	HelperHash string `json:"helper_hash,omitempty"`
	AssetName  string `json:"asset_name,omitempty"`
	Tag        string `json:"tag,omitempty"`
}

// statePath returns where bootstrap-state.json lives for the given
// cache dir. Empty cacheDir disables the cache (useful for tests).
func statePath(cacheDir string) string {
	if cacheDir == "" {
		return ""
	}
	return filepath.Join(cacheDir, stateFileName)
}

// loadState reads bootstrap-state.json; returns an empty state if
// the file doesn't exist.
func loadState(cacheDir string) *State {
	p := statePath(cacheDir)
	if p == "" {
		return &State{Hosts: map[string]HostState{}}
	}
	data, err := os.ReadFile(p)
	if err != nil {
		return &State{Hosts: map[string]HostState{}}
	}
	var s State
	if err := json.Unmarshal(data, &s); err != nil {
		return &State{Hosts: map[string]HostState{}}
	}
	if s.Hosts == nil {
		s.Hosts = map[string]HostState{}
	}
	return &s
}

func saveState(cacheDir string, s *State) error {
	p := statePath(cacheDir)
	if p == "" {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return fmt.Errorf("mkdir %s: %w", filepath.Dir(p), err)
	}
	data, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	tmp := p + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, p)
}

// Request describes one bootstrap operation.
type Request struct {
	HostID    string // inventory host id (used as the state-file key)
	Codespace string // codespace name; required for backend=codespace
	Repo      string // OWNER/REPO; empty ⇒ DefaultRepo
	Tag       string // release tag; empty or "latest" ⇒ latest
	CacheDir  string // controller cache dir for the state file
}

// Result is the structured outcome of a bootstrap. Sent to the UI
// verbatim as the POST /hosts/{id}/bootstrap response body.
type Result struct {
	HostID         string `json:"host_id"`
	Codespace      string `json:"codespace"`
	Repo           string `json:"repo"`
	Tag            string `json:"tag"`
	AssetName      string `json:"asset_name"`
	HelperUploaded bool   `json:"helper_uploaded"` // false ⇒ skipped (hash match)
	AssetUploaded  bool   `json:"asset_uploaded"`  // always true today; reserved for future caching
	InstallerLog   string `json:"installer_log,omitempty"`
}

// Bootstrap runs the full install flow. Errors carry enough context
// for the operator to fix things without re-reading source.
func Bootstrap(ctx context.Context, req Request) (*Result, error) {
	if req.Codespace == "" {
		return nil, errors.New("bootstrap: codespace name is required (set inventory.codespace.name)")
	}
	repo := req.Repo
	if repo == "" {
		repo = DefaultRepo
	}

	// 1) Resolve the release + pick the linux-x86_64 asset.
	info, err := gh.ReleaseView(ctx, repo, req.Tag)
	if err != nil {
		return nil, fmt.Errorf("resolve release: %w", err)
	}
	var asset *gh.ReleaseAsset
	for i := range info.Assets {
		if assetPattern.MatchString(info.Assets[i].Name) {
			asset = &info.Assets[i]
			break
		}
	}
	if asset == nil {
		names := make([]string, len(info.Assets))
		for i, a := range info.Assets {
			names[i] = a.Name
		}
		return nil, fmt.Errorf("no tm-worker-v*-linux-x86_64.run asset on %s %s; available: %s",
			repo, info.TagName, strings.Join(names, ", "))
	}

	// 2) Download the asset locally to a tempdir.
	tmpDir, err := os.MkdirTemp("", "tm-worker-asset-*")
	if err != nil {
		return nil, fmt.Errorf("mkdtemp: %w", err)
	}
	defer os.RemoveAll(tmpDir)
	if err := gh.ReleaseDownload(ctx, repo, info.TagName, asset.Name, tmpDir); err != nil {
		return nil, fmt.Errorf("download asset: %w", err)
	}
	localAsset := filepath.Join(tmpDir, asset.Name)
	if _, err := os.Stat(localAsset); err != nil {
		return nil, fmt.Errorf("downloaded asset missing at %s: %w", localAsset, err)
	}

	// 3) Stage the helper script next to the asset.
	helperPath := filepath.Join(tmpDir, "install_tm_worker_release.sh")
	if err := os.WriteFile(helperPath, installerScript, 0o755); err != nil {
		return nil, fmt.Errorf("write embedded helper: %w", err)
	}

	// 4) Upload to the codespace. The mkdir runs every time —
	// codespaces wipe /tmp on rebuild but ~/.local survives, so the
	// directory may already exist; mkdir -p is idempotent.
	if _, err := gh.SSH(ctx, req.Codespace, "mkdir -p "+remoteDir); err != nil {
		return nil, fmt.Errorf("ssh mkdir: %w", err)
	}

	state := loadState(req.CacheDir)
	prev := state.Hosts[req.HostID]
	helperUploaded := false
	if prev.HelperHash != installerScriptHash {
		if err := gh.CP(ctx, req.Codespace, helperPath, remoteDir+"/install_tm_worker_release.sh"); err != nil {
			return nil, fmt.Errorf("cp helper: %w", err)
		}
		helperUploaded = true
	}

	// Always re-upload the .run asset: its hash isn't tracked
	// remotely, and a partial upload from a previous failure could
	// otherwise be silently reused.
	if err := gh.CP(ctx, req.Codespace, localAsset, remoteDir+"/"+asset.Name); err != nil {
		return nil, fmt.Errorf("cp asset: %w", err)
	}

	// 5) chmod + run the installer remotely.
	chmod := fmt.Sprintf("chmod +x %s/install_tm_worker_release.sh %s/%s",
		remoteDir, remoteDir, asset.Name)
	if _, err := gh.SSH(ctx, req.Codespace, chmod); err != nil {
		return nil, fmt.Errorf("ssh chmod: %w", err)
	}
	runCmd := fmt.Sprintf("%s/install_tm_worker_release.sh -f %s/%s",
		remoteDir, remoteDir, asset.Name)
	logBytes, err := gh.SSH(ctx, req.Codespace, runCmd)
	installerLog := string(logBytes)
	if err != nil {
		// Surface the installer's stdout/stderr; that's where the
		// real diagnostic lives.
		return nil, fmt.Errorf("remote installer: %w\n--- installer output ---\n%s", err, installerLog)
	}

	// 6) Persist state so the next bootstrap can skip the helper cp.
	state.Hosts[req.HostID] = HostState{
		HelperHash: installerScriptHash,
		AssetName:  asset.Name,
		Tag:        info.TagName,
	}
	if err := saveState(req.CacheDir, state); err != nil {
		// Non-fatal: the install succeeded; the next bootstrap will
		// just re-cp the helper.
		installerLog += "\n[warn] failed to persist bootstrap state: " + err.Error()
	}

	return &Result{
		HostID:         req.HostID,
		Codespace:      req.Codespace,
		Repo:           repo,
		Tag:            info.TagName,
		AssetName:      asset.Name,
		HelperUploaded: helperUploaded,
		AssetUploaded:  true,
		InstallerLog:   installerLog,
	}, nil
}

// HelperHash exposes the embedded helper's SHA-256 for diagnostics
// (e.g. /hosts/{id}/status could surface "helper drift" later).
func HelperHash() string { return installerScriptHash }
