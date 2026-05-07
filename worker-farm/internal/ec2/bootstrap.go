package ec2

import (
	"context"
	"crypto/sha256"
	_ "embed"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/service/ssm"
	ssmtypes "github.com/aws/aws-sdk-go-v2/service/ssm/types"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/gh"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/inventory"
)

const (
	ec2RemoteDirSuffix       = "/.local/share/tm-worker-farm"
	ec2BootstrapStateFile    = "ec2-bootstrap-state.json"
	ssmCommandPollInterval   = 2 * time.Second
	ssmPerCommandTimeout     = 30 * time.Second
	ssmBootstrapTotalTimeout = 10 * time.Minute
	chunkSizeBase64          = 3500
)

var ec2AssetPattern = regexp.MustCompile(`^tm-worker-v.*-linux-x86_64\.run$`)

//go:embed install_tm_worker_release.sh
var installerScript []byte

var installerScriptHash = func() string {
	sum := sha256.Sum256(installerScript)
	return hex.EncodeToString(sum[:])
}()

// DefaultRepo is used when the operator doesn't provide a repo.
const DefaultRepo = "ilya-yusim/task-messenger"

// BootstrapRequest describes one EC2 bootstrap operation.
type BootstrapRequest struct {
	HostID       string
	ControllerID string
	EC2          inventory.EC2Cfg
	Repo         string
	Tag          string
	CacheDir     string
}

// BootstrapResult mirrors bootstrap response shape and adds EC2 details.
type BootstrapResult struct {
	HostID         string `json:"host_id"`
	Repo           string `json:"repo"`
	Tag            string `json:"tag"`
	AssetName      string `json:"asset_name"`
	HelperUploaded bool   `json:"helper_uploaded"`
	AssetUploaded  bool   `json:"asset_uploaded"`
	InstallerLog   string `json:"installer_log,omitempty"`
	InstanceID     string `json:"instance_id,omitempty"`
	Region         string `json:"region,omitempty"`
}

type bootstrapState struct {
	Hosts map[string]bootstrapHostState `json:"hosts"`
}

type bootstrapHostState struct {
	HelperHash string `json:"helper_hash,omitempty"`
	AssetName  string `json:"asset_name,omitempty"`
	Tag        string `json:"tag,omitempty"`
}

// Bootstrap installs tm-worker on a managed EC2 instance through SSM.
func Bootstrap(ctx context.Context, req BootstrapRequest) (*BootstrapResult, error) {
	if strings.TrimSpace(req.HostID) == "" {
		return nil, errors.New("ec2 bootstrap: host id is required")
	}
	if strings.TrimSpace(req.ControllerID) == "" {
		return nil, errors.New("ec2 bootstrap: controller id is required")
	}
	repo := strings.TrimSpace(req.Repo)
	if repo == "" {
		repo = DefaultRepo
	}
	ctx, cancel := context.WithTimeout(ctx, ssmBootstrapTotalTimeout)
	defer cancel()

	inst, err := EnsureInstance(ctx, req.EC2, req.HostID, req.ControllerID)
	if err != nil {
		return nil, fmt.Errorf("ensure ec2 instance: %w", err)
	}

	info, err := gh.ReleaseView(ctx, repo, req.Tag)
	if err != nil {
		return nil, fmt.Errorf("resolve release: %w", err)
	}
	asset, err := pickAsset(info)
	if err != nil {
		return nil, err
	}

	tmpDir, err := os.MkdirTemp("", "tm-worker-ec2-asset-*")
	if err != nil {
		return nil, fmt.Errorf("mkdtemp: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	downloadStart := time.Now()
	if err := gh.ReleaseDownload(ctx, repo, info.TagName, asset.Name, tmpDir); err != nil {
		return nil, fmt.Errorf("download asset: %w", err)
	}
	downloadDur := time.Since(downloadStart)
	localAsset := filepath.Join(tmpDir, asset.Name)
	assetInfo, err := os.Stat(localAsset)
	if err != nil {
		return nil, fmt.Errorf("downloaded asset missing at %s: %w", localAsset, err)
	}
	log.Printf("ec2 bootstrap: downloaded asset %s (%d bytes) in %s", asset.Name, assetInfo.Size(), downloadDur.Round(time.Millisecond))

	cl, err := newClients(ctx, req.EC2.Region)
	if err != nil {
		return nil, err
	}
	remoteHome, err := resolveRemoteHome(ctx, cl.ssm, inst.SSMTarget)
	if err != nil {
		return nil, fmt.Errorf("resolve remote home: %w", err)
	}
	remoteDir := remoteHome + ec2RemoteDirSuffix
	if _, err := runSSMShell(ctx, cl.ssm, inst.SSMTarget, "mkdir -p "+shQuote(remoteDir)); err != nil {
		return nil, fmt.Errorf("ssm mkdir: %w", err)
	}

	state := loadBootstrapState(req.CacheDir)
	prev := state.Hosts[req.HostID]
	helperRemotePath := remoteDir + "/install_tm_worker_release.sh"
	helperUploaded := false
	helperPresent := false
	if prev.HelperHash == installerScriptHash {
		helperPresent, err = remoteFileExistsSSM(ctx, cl.ssm, inst.SSMTarget, helperRemotePath)
		if err != nil {
			return nil, fmt.Errorf("probe helper presence: %w", err)
		}
	}
	if prev.HelperHash != installerScriptHash || !helperPresent {
		if err := writeRemoteFileFromBytes(ctx, cl.ssm, inst.SSMTarget, installerScript, helperRemotePath); err != nil {
			return nil, fmt.Errorf("upload helper script: %w", err)
		}
		helperUploaded = true
	}

	assetRemotePath := remoteDir + "/" + asset.Name
	// Obtain a short-lived pre-signed download URL so the instance can
	// pull the asset directly from GitHub (one curl → no chunked upload).
	var downloadURL string
	assetRef := asset.APIURL
	if strings.TrimSpace(assetRef) == "" {
		assetRef = asset.URL
	}
	downloadURL, err = gh.ReleaseAssetDownloadURL(ctx, assetRef)
	if err != nil {
		return nil, fmt.Errorf("get asset download URL: %w", err)
	}
	log.Printf("ec2 bootstrap: fetching asset %s on instance %s via curl", asset.Name, inst.InstanceID)
	fetchStart := time.Now()
	curlCmd := "curl -fsSL -o " + shQuote(assetRemotePath) + " " + shQuote(downloadURL)
	if _, err := runSSMShell(ctx, cl.ssm, inst.SSMTarget, curlCmd); err != nil {
		// Some release URLs are not directly reachable from the instance
		// (e.g., private browser URLs returning 404). Fall back to the
		// controller-side download + SSM upload path.
		log.Printf("ec2 bootstrap: remote curl failed (%v); falling back to SSM upload", err)
		uploadStart := time.Now()
		if upErr := writeRemoteFileFromPath(ctx, cl.ssm, inst.SSMTarget, localAsset, assetRemotePath); upErr != nil {
			return nil, fmt.Errorf("remote curl asset: %v; fallback upload failed: %w", err, upErr)
		}
		uploadDur := time.Since(uploadStart)
		log.Printf("ec2 bootstrap: fallback upload completed for %s in %s", asset.Name, uploadDur.Round(time.Millisecond))
	} else {
		fetchDur := time.Since(fetchStart)
		log.Printf("ec2 bootstrap: instance downloaded asset %s in %s", asset.Name, fetchDur.Round(time.Millisecond))
	}

	if err := verifyRemoteSHA256(ctx, cl.ssm, inst.SSMTarget, localAsset, assetRemotePath); err != nil {
		return nil, err
	}

	if _, err := runSSMShell(ctx, cl.ssm, inst.SSMTarget,
		"chmod +x "+shQuote(helperRemotePath)+" "+shQuote(assetRemotePath)); err != nil {
		return nil, fmt.Errorf("chmod helper/asset: %w", err)
	}

	runCmd := "HOME=" + shQuote(remoteHome) + " " + shQuote(helperRemotePath) + " -f " + shQuote(assetRemotePath)
	installerLog, err := runSSMShell(ctx, cl.ssm, inst.SSMTarget, runCmd)
	if err != nil {
		return nil, fmt.Errorf("remote installer: %w\n--- installer output ---\n%s", err, installerLog)
	}

	state.Hosts[req.HostID] = bootstrapHostState{
		HelperHash: installerScriptHash,
		AssetName:  asset.Name,
		Tag:        info.TagName,
	}
	if err := saveBootstrapState(req.CacheDir, state); err != nil {
		installerLog += "\n[warn] failed to persist bootstrap state: " + err.Error()
	}

	return &BootstrapResult{
		HostID:         req.HostID,
		Repo:           repo,
		Tag:            info.TagName,
		AssetName:      asset.Name,
		HelperUploaded: helperUploaded,
		AssetUploaded:  true,
		InstallerLog:   installerLog,
		InstanceID:     inst.InstanceID,
		Region:         inst.Region,
	}, nil
}

func pickAsset(info *gh.ReleaseInfo) (*gh.ReleaseAsset, error) {
	if info == nil {
		return nil, errors.New("resolve release: empty response")
	}
	for i := range info.Assets {
		if ec2AssetPattern.MatchString(info.Assets[i].Name) {
			return &info.Assets[i], nil
		}
	}
	names := make([]string, len(info.Assets))
	for i, a := range info.Assets {
		names[i] = a.Name
	}
	return nil, fmt.Errorf("no tm-worker-v*-linux-x86_64.run asset on %s; available: %s",
		info.TagName, strings.Join(names, ", "))
}

func runSSMShell(ctx context.Context, cli *ssm.Client, instanceID, command string) (string, error) {
	ctxOne, cancel := context.WithTimeout(ctx, ssmPerCommandTimeout)
	defer cancel()
	out, err := cli.SendCommand(ctxOne, &ssm.SendCommandInput{
		DocumentName: aws.String("AWS-RunShellScript"),
		InstanceIds:  []string{instanceID},
		Parameters: map[string][]string{
			"commands": {command},
		},
	})
	if err != nil {
		return "", fmt.Errorf("ssm send command: %w", err)
	}
	if out.Command == nil || out.Command.CommandId == nil {
		return "", errors.New("ssm send command: missing command id")
	}
	return waitCommandInvocation(ctxOne, cli, aws.ToString(out.Command.CommandId), instanceID)
}

func waitCommandInvocation(ctx context.Context, cli *ssm.Client, commandID, instanceID string) (string, error) {
	t := time.NewTicker(ssmCommandPollInterval)
	defer t.Stop()
	for {
		inv, err := cli.GetCommandInvocation(ctx, &ssm.GetCommandInvocationInput{
			CommandId:  aws.String(commandID),
			InstanceId: aws.String(instanceID),
		})
		if err == nil {
			status := inv.Status
			switch status {
			case ssmtypes.CommandInvocationStatusSuccess:
				return aws.ToString(inv.StandardOutputContent), nil
			case ssmtypes.CommandInvocationStatusCancelled,
				ssmtypes.CommandInvocationStatusTimedOut,
				ssmtypes.CommandInvocationStatusFailed,
				ssmtypes.CommandInvocationStatusCancelling:
				return aws.ToString(inv.StandardOutputContent), fmt.Errorf("ssm command %s failed: %s: %s",
					commandID, status, aws.ToString(inv.StandardErrorContent))
			}
		}

		select {
		case <-ctx.Done():
			return "", fmt.Errorf("wait command %s: %w", commandID, ctx.Err())
		case <-t.C:
		}
	}
}

func writeRemoteFileFromPath(ctx context.Context, cli *ssm.Client, instanceID, localPath, remotePath string) error {
	data, err := os.ReadFile(localPath)
	if err != nil {
		return err
	}
	return writeRemoteFileFromBytes(ctx, cli, instanceID, data, remotePath)
}

func writeRemoteFileFromBytes(ctx context.Context, cli *ssm.Client, instanceID string, data []byte, remotePath string) error {
	b64 := base64.StdEncoding.EncodeToString(data)
	b64Path := remotePath + ".b64"
	if _, err := runSSMShell(ctx, cli, instanceID, ": > "+shQuote(b64Path)); err != nil {
		return err
	}
	for i := 0; i < len(b64); i += chunkSizeBase64 {
		end := i + chunkSizeBase64
		if end > len(b64) {
			end = len(b64)
		}
		chunk := b64[i:end]
		cmd := "printf '%s' '" + chunk + "' >> " + shQuote(b64Path)
		if _, err := runSSMShell(ctx, cli, instanceID, cmd); err != nil {
			return err
		}
	}
	if _, err := runSSMShell(ctx, cli, instanceID,
		"base64 -d "+shQuote(b64Path)+" > "+shQuote(remotePath)+" && rm -f "+shQuote(b64Path)); err != nil {
		return err
	}
	return nil
}

func verifyRemoteSHA256(ctx context.Context, cli *ssm.Client, instanceID, localPath, remotePath string) error {
	data, err := os.ReadFile(localPath)
	if err != nil {
		return fmt.Errorf("read local asset for hash: %w", err)
	}
	s := sha256.Sum256(data)
	localHex := hex.EncodeToString(s[:])
	remoteOut, err := runSSMShell(ctx, cli, instanceID, "set -e; f="+shQuote(remotePath)+"; if command -v sha256sum >/dev/null 2>&1; then h=$(sha256sum \"$f\" | awk '{print $1}'); elif command -v shasum >/dev/null 2>&1; then h=$(shasum -a 256 \"$f\" | awk '{print $1}'); else echo 'no sha256 tool found' >&2; exit 1; fi; echo __TM_SHA=$h")
	if err != nil {
		return fmt.Errorf("remote sha256: %w", err)
	}
	marker := "__TM_SHA="
	idx := strings.LastIndex(remoteOut, marker)
	if idx < 0 {
		return fmt.Errorf("remote sha256 output missing marker: %s", remoteOut)
	}
	remoteHex := strings.TrimSpace(remoteOut[idx+len(marker):])
	if remoteHex != localHex {
		return fmt.Errorf("asset sha256 mismatch: local=%s remote=%s", localHex, remoteHex)
	}
	return nil
}

func remoteFileExistsSSM(ctx context.Context, cli *ssm.Client, instanceID, path string) (bool, error) {
	out, err := runSSMShell(ctx, cli, instanceID,
		"if [ -f "+shQuote(path)+" ]; then echo __TM_EXISTS=1; else echo __TM_EXISTS=0; fi")
	if err != nil {
		return false, err
	}
	return strings.Contains(out, "__TM_EXISTS=1"), nil
}

func resolveRemoteHome(ctx context.Context, cli *ssm.Client, instanceID string) (string, error) {
	out, err := runSSMShell(ctx, cli, instanceID,
		"h=\"${HOME:-}\"; if [ -z \"$h\" ]; then u=$(id -un 2>/dev/null || true); if [ -n \"$u\" ]; then h=$(getent passwd \"$u\" 2>/dev/null | cut -d: -f6 || true); fi; fi; if [ -z \"$h\" ]; then h=/root; fi; echo __TM_HOME=$h")
	if err != nil {
		return "", err
	}
	marker := "__TM_HOME="
	idx := strings.LastIndex(out, marker)
	if idx < 0 {
		return "", fmt.Errorf("remote home output missing marker: %s", out)
	}
	home := strings.TrimSpace(out[idx+len(marker):])
	if home == "" {
		return "", errors.New("remote home is empty")
	}
	return home, nil
}

func shQuote(s string) string {
	return "'" + strings.ReplaceAll(s, "'", "'\"'\"'") + "'"
}

func bootstrapStatePath(cacheDir string) string {
	if strings.TrimSpace(cacheDir) == "" {
		return ""
	}
	return filepath.Join(cacheDir, ec2BootstrapStateFile)
}

func loadBootstrapState(cacheDir string) *bootstrapState {
	p := bootstrapStatePath(cacheDir)
	if p == "" {
		return &bootstrapState{Hosts: map[string]bootstrapHostState{}}
	}
	data, err := os.ReadFile(p)
	if err != nil {
		return &bootstrapState{Hosts: map[string]bootstrapHostState{}}
	}
	var s bootstrapState
	if err := json.Unmarshal(data, &s); err != nil {
		return &bootstrapState{Hosts: map[string]bootstrapHostState{}}
	}
	if s.Hosts == nil {
		s.Hosts = map[string]bootstrapHostState{}
	}
	return &s
}

func saveBootstrapState(cacheDir string, s *bootstrapState) error {
	p := bootstrapStatePath(cacheDir)
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
