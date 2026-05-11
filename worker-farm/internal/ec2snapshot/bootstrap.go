package ec2snapshot

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
	e2 "github.com/aws/aws-sdk-go-v2/service/ec2"
	"github.com/aws/aws-sdk-go-v2/service/ssm"
	ssmtypes "github.com/aws/aws-sdk-go-v2/service/ssm/types"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/gh"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/inventory"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/paths"
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
	EC2          inventory.EC2SnapshotCfg
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
	ImageID        string `json:"image_id,omitempty"`
}

type bootstrapState struct {
	Hosts map[string]bootstrapHostState `json:"hosts"`
}

type bootstrapHostState struct {
	HelperHash string `json:"helper_hash,omitempty"`
	AssetName  string `json:"asset_name,omitempty"`
	Tag        string `json:"tag,omitempty"`
}

// Bootstrap installs tm-worker on a managed EC2 instance through SSM,
// verifies it, and promotes a new AMI for the host lineage.
func Bootstrap(ctx context.Context, req BootstrapRequest) (*BootstrapResult, error) {
	if strings.TrimSpace(req.HostID) == "" {
		return nil, errors.New("ec2snapshot bootstrap: host id is required")
	}
	if strings.TrimSpace(req.ControllerID) == "" {
		return nil, errors.New("ec2snapshot bootstrap: controller id is required")
	}
	repo := strings.TrimSpace(req.Repo)
	if repo == "" {
		repo = DefaultRepo
	}
	ctx, cancel := context.WithTimeout(ctx, ssmBootstrapTotalTimeout)
	defer cancel()

	inst, err := EnsureInstance(ctx, req.EC2, req.HostID, req.ControllerID)
	if err != nil {
		return nil, fmt.Errorf("ensure ec2 snapshot instance: %w", err)
	}

	info, err := gh.ReleaseView(ctx, repo, req.Tag)
	if err != nil {
		return nil, fmt.Errorf("resolve release: %w", err)
	}
	asset, err := pickAsset(info)
	if err != nil {
		return nil, err
	}

	tmpDir, err := os.MkdirTemp("", "tm-worker-ec2snapshot-asset-*")
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
	log.Printf("ec2snapshot bootstrap: downloaded asset %s (%d bytes) in %s", asset.Name, assetInfo.Size(), downloadDur.Round(time.Millisecond))

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
	assetRef := asset.APIURL
	if strings.TrimSpace(assetRef) == "" {
		assetRef = asset.URL
	}
	downloadURL, err := gh.ReleaseAssetDownloadURL(ctx, assetRef)
	if err != nil {
		return nil, fmt.Errorf("get asset download URL: %w", err)
	}
	log.Printf("ec2snapshot bootstrap: fetching asset %s on instance %s via curl", asset.Name, inst.InstanceID)
	fetchStart := time.Now()
	curlCmd := "curl -fsSL -o " + shQuote(assetRemotePath) + " " + shQuote(downloadURL)
	if _, err := runSSMShell(ctx, cl.ssm, inst.SSMTarget, curlCmd); err != nil {
		log.Printf("ec2snapshot bootstrap: remote curl failed (%v); falling back to SSM upload", err)
		if upErr := writeRemoteFileFromPath(ctx, cl.ssm, inst.SSMTarget, localAsset, assetRemotePath); upErr != nil {
			return nil, fmt.Errorf("remote curl asset: %v; fallback upload failed: %w", err, upErr)
		}
	} else {
		fetchDur := time.Since(fetchStart)
		log.Printf("ec2snapshot bootstrap: instance downloaded asset %s in %s", asset.Name, fetchDur.Round(time.Millisecond))
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

	versionOut, vErr := runSSMShell(ctx, cl.ssm, inst.SSMTarget,
		"bash -lc 'export PATH=\"$HOME/.local/bin:$PATH\"; tm-worker --version 2>&1 || true'")
	if vErr != nil {
		installerLog += "\n[warn] tm-worker --version check failed: " + vErr.Error()
	}
	if strings.Contains(versionOut, "GLIBC_") && strings.Contains(versionOut, "not found") {
		return nil, fmt.Errorf("installed tm-worker cannot run on this image: %s", strings.TrimSpace(versionOut))
	}

	imageName := fmt.Sprintf("tm-worker-farm-%s-%s-%d", req.HostID, sanitizeImageTag(info.TagName), time.Now().UTC().Unix())
	createOut, err := cl.ec2.CreateImage(ctx, &e2.CreateImageInput{
		InstanceId:  aws.String(inst.InstanceID),
		Name:        aws.String(imageName),
		Description: aws.String("tm-worker-farm promotion for host " + req.HostID + " tag " + info.TagName),
		NoReboot:    aws.Bool(true),
	})
	if err != nil {
		return nil, fmt.Errorf("create image: %w", err)
	}
	imageID := aws.ToString(createOut.ImageId)
	if imageID == "" {
		return nil, errors.New("create image: missing image id")
	}
	log.Printf("ec2snapshot bootstrap: image create requested host=%s instance=%s image=%s", req.HostID, inst.InstanceID, imageID)
	if err := waitForImageAvailable(ctx, cl.ec2, imageID); err != nil {
		return nil, err
	}

	if err := savePromotedImage(req.HostID, req.Tag, imageID, req.CacheDir); err != nil {
		installerLog += "\n[warn] failed to persist image promotion state: " + err.Error()
	}

	state.Hosts[req.HostID] = bootstrapHostState{HelperHash: installerScriptHash, AssetName: asset.Name, Tag: info.TagName}
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
		ImageID:        imageID,
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

func waitForImageAvailable(ctx context.Context, cli *e2.Client, imageID string) error {
	waiter := e2.NewImageAvailableWaiter(cli)
	if err := waiter.Wait(ctx, &e2.DescribeImagesInput{ImageIds: []string{imageID}}, 20*time.Minute); err != nil {
		return fmt.Errorf("wait image %s available: %w", imageID, err)
	}
	return nil
}

func sanitizeImageTag(tag string) string {
	tag = strings.TrimSpace(tag)
	if tag == "" {
		return "untagged"
	}
	var b strings.Builder
	for _, r := range tag {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r >= '0' && r <= '9':
			b.WriteRune(r)
		case r == '-', r == '.', r == '_':
			b.WriteRune(r)
		default:
			b.WriteByte('-')
		}
	}
	return strings.Trim(b.String(), "-._")
}

func loadBootstrapState(cacheDir string) bootstrapState {
	state := bootstrapState{Hosts: map[string]bootstrapHostState{}}
	path := filepath.Join(cacheDir, ec2BootstrapStateFile)
	data, err := os.ReadFile(path)
	if err != nil {
		return state
	}
	_ = json.Unmarshal(data, &state)
	if state.Hosts == nil {
		state.Hosts = map[string]bootstrapHostState{}
	}
	return state
}

func saveBootstrapState(cacheDir string, state bootstrapState) error {
	if state.Hosts == nil {
		state.Hosts = map[string]bootstrapHostState{}
	}
	path := filepath.Join(cacheDir, ec2BootstrapStateFile)
	tmp := path + ".tmp"
	data, err := json.MarshalIndent(state, "", "  ")
	if err != nil {
		return err
	}
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

func savePromotedImage(hostID, releaseTag, imageID, cacheDir string) error {
	statePath, err := paths.EC2SnapshotStatePath()
	if err != nil {
		return err
	}
	state := snapshotState{Version: 1, Hosts: map[string]snapshotHostState{}}
	data, err := os.ReadFile(statePath)
	if err == nil {
		_ = json.Unmarshal(data, &state)
	}
	if state.Hosts == nil {
		state.Hosts = map[string]snapshotHostState{}
	}
	prev := state.Hosts[hostID]
	state.Hosts[hostID] = snapshotHostState{
		CurrentAmiID:            imageID,
		PreviousAmiID:           prev.CurrentAmiID,
		LineageTag:              hostID,
		UpdatedAt:               time.Now().UTC(),
		LastBootstrapReleaseTag: releaseTag,
	}
	if err := os.MkdirAll(filepath.Dir(statePath), 0o755); err != nil {
		return err
	}
	tmp := statePath + ".tmp"
	encoded, err := json.MarshalIndent(state, "", "  ")
	if err != nil {
		return err
	}
	if err := os.WriteFile(tmp, encoded, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, statePath)
}

func resolveRemoteHome(ctx context.Context, cli *ssm.Client, target string) (string, error) {
	out, err := runSSMShell(ctx, cli, target, `if [ -n "${HOME:-}" ]; then printf '%s' "$HOME"; else getent passwd $(id -u) | cut -d: -f6; fi`)
	if err != nil {
		return "", err
	}
	home := strings.TrimSpace(out)
	if home == "" {
		return "", errors.New("remote home empty")
	}
	return home, nil
}

func remoteFileExistsSSM(ctx context.Context, cli *ssm.Client, target, path string) (bool, error) {
	out, err := runSSMShell(ctx, cli, target, "test -e "+shQuote(path)+" && echo yes || echo no")
	if err != nil {
		return false, err
	}
	return strings.Contains(out, "yes"), nil
}

func writeRemoteFileFromBytes(ctx context.Context, cli *ssm.Client, target string, data []byte, remotePath string) error {
	encoded := base64.StdEncoding.EncodeToString(data)
	var cmds []string
	for len(encoded) > 0 {
		chunk := encoded
		if len(chunk) > chunkSizeBase64 {
			chunk = encoded[:chunkSizeBase64]
			encoded = encoded[chunkSizeBase64:]
		} else {
			encoded = ""
		}
		cmds = append(cmds, "printf %s "+shQuote(chunk)+" >> "+shQuote(remotePath)+".b64")
	}
	if _, err := runSSMShell(ctx, cli, target, "rm -f "+shQuote(remotePath)+".b64 && touch "+shQuote(remotePath)+".b64"); err != nil {
		return err
	}
	for _, cmd := range cmds {
		if _, err := runSSMShell(ctx, cli, target, cmd); err != nil {
			return err
		}
	}
	if _, err := runSSMShell(ctx, cli, target, "base64 -d "+shQuote(remotePath)+".b64 > "+shQuote(remotePath)+" && rm -f "+shQuote(remotePath)+".b64"); err != nil {
		return err
	}
	return nil
}

func writeRemoteFileFromPath(ctx context.Context, cli *ssm.Client, target, localPath, remotePath string) error {
	data, err := os.ReadFile(localPath)
	if err != nil {
		return err
	}
	return writeRemoteFileFromBytes(ctx, cli, target, data, remotePath)
}

func verifyRemoteSHA256(ctx context.Context, cli *ssm.Client, target string, localPath, remotePath string) error {
	data, err := os.ReadFile(localPath)
	if err != nil {
		return err
	}
	sum := sha256.Sum256(data)
	want := hex.EncodeToString(sum[:])
	out, err := runSSMShell(ctx, cli, target, "sha256sum "+shQuote(remotePath))
	if err != nil {
		return err
	}
	got := strings.Fields(strings.TrimSpace(out))
	if len(got) == 0 || got[0] != want {
		return fmt.Errorf("remote sha256 mismatch for %s", remotePath)
	}
	return nil
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
	ticker := time.NewTicker(ssmCommandPollInterval)
	defer ticker.Stop()
	deadline := time.NewTimer(ssmBootstrapTotalTimeout)
	defer deadline.Stop()
	for {
		out, err := cli.GetCommandInvocation(ctx, &ssm.GetCommandInvocationInput{CommandId: aws.String(commandID), InstanceId: aws.String(instanceID)})
		if err != nil {
			var nf *ssmtypes.InvocationDoesNotExist
			if !errors.As(err, &nf) {
				return "", err
			}
		} else {
			if out.Status == ssmtypes.CommandInvocationStatusSuccess {
				return aws.ToString(out.StandardOutputContent), nil
			}
			if out.Status == ssmtypes.CommandInvocationStatusFailed || out.Status == ssmtypes.CommandInvocationStatusTimedOut || out.Status == ssmtypes.CommandInvocationStatusCancelled {
				return aws.ToString(out.StandardErrorContent), fmt.Errorf("ssm command %s on %s failed with %s", commandID, instanceID, out.Status)
			}
		}
		select {
		case <-ctx.Done():
			return "", ctx.Err()
		case <-deadline.C:
			return "", fmt.Errorf("ssm command %s on %s timed out", commandID, instanceID)
		case <-ticker.C:
		}
	}
}

func shQuote(s string) string {
	return "'" + strings.ReplaceAll(s, "'", "'\\''") + "'"
}
