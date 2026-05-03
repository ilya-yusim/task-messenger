// Package gh wraps the `gh` (GitHub CLI) binary for the bits the
// controller needs: codespace list/ssh/cp and an auth-scope preflight.
//
// We shell out instead of speaking the GitHub API directly because:
//
//   - `gh` already knows where the user's tokens live (keyring / config).
//   - `gh codespace ssh` does the WireGuard tunnel setup for us.
//   - `gh codespace cp` ships files through that same tunnel.
//
// Operators must have `gh` on `$PATH`. We never auto-install it; the
// preflight surfaces a clean error when it's missing.
package gh

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os/exec"
	"regexp"
	"strings"
)

// Required scope on the GitHub token for `gh codespace ssh/cp`.
const codespaceScope = "codespace"

// Binary returns the absolute path to `gh` if it's on $PATH, or an
// error suitable for surfacing to the operator (with the exact apt /
// brew / scoop hint left to the caller — we don't second-guess the
// install method).
func Binary() (string, error) {
	p, err := exec.LookPath("gh")
	if err != nil {
		return "", &MissingBinaryError{}
	}
	return p, nil
}

// MissingBinaryError is returned when `gh` is not on $PATH. The
// caller decides how to remediate; the controller surfaces a 503 on
// /hosts/{id}/status and asks the operator to install gh.
type MissingBinaryError struct{}

func (e *MissingBinaryError) Error() string {
	return "gh CLI not found on PATH; install it from https://cli.github.com/ and run `gh auth login`"
}

// NeedsCodespaceScopeError is returned by AuthStatus when the user
// is logged in but the token is missing the `codespace` scope. The
// remediation hint matches the one the plan calls for.
type NeedsCodespaceScopeError struct {
	// Scopes is the set of scopes currently on the token, for
	// diagnostic display. Never nil; may be empty.
	Scopes []string
}

func (e *NeedsCodespaceScopeError) Error() string {
	return fmt.Sprintf(
		"gh token is missing the 'codespace' scope (have: %s); run: gh auth refresh -h github.com -s codespace",
		strings.Join(e.Scopes, ", "),
	)
}

// NotLoggedInError is returned when `gh auth status` reports no
// active session for github.com.
type NotLoggedInError struct{}

func (e *NotLoggedInError) Error() string {
	return "gh is not logged in to github.com; run: gh auth login"
}

// AuthStatusInfo summarises what AuthStatus discovered. It's
// returned alongside the error so callers can render UI even when
// the token is good but missing scopes.
type AuthStatusInfo struct {
	LoggedIn      bool     `json:"logged_in"`
	Username      string   `json:"username,omitempty"`
	Scopes        []string `json:"scopes,omitempty"`
	HasCodespaces bool     `json:"has_codespace_scope"`
}

// scopesLine matches the "Token scopes:" line in `gh auth status`'s
// human-readable output. `gh` doesn't have a machine-readable mode
// for auth status (as of gh 2.45); regex parsing is the documented
// workaround.
var (
	scopesLine = regexp.MustCompile(`Token scopes:\s*(.+)`)
	// scopeItem captures items inside the comma-separated scope list,
	// stripping the surrounding single quotes gh emits.
	scopeItem = regexp.MustCompile(`'([^']+)'`)
	// loggedInLine matches "Logged in to github.com account <user>".
	loggedInLine = regexp.MustCompile(`Logged in to github\.com account ([^\s]+)`)
	// loggedInLineLegacy matches older `gh` versions that say
	// "Logged in to github.com as <user>".
	loggedInLineLegacy = regexp.MustCompile(`Logged in to github\.com as ([^\s]+)`)
)

// AuthStatus shells `gh auth status -h github.com` and reports the
// parsed result. Returns:
//
//   - (info, nil)                                   — logged in with codespace scope.
//   - (info, *NeedsCodespaceScopeError)             — logged in, missing scope.
//   - (info, *NotLoggedInError)                     — not logged in.
//   - (nil, *MissingBinaryError)                    — gh not on PATH.
//   - (nil, otherErr)                               — anything else
//     (network, gh internal error). Caller should display verbatim.
func AuthStatus(ctx context.Context) (*AuthStatusInfo, error) {
	bin, err := Binary()
	if err != nil {
		return nil, err
	}
	// `gh auth status` writes its human-readable output to stderr
	// (success path) and exits non-zero when not logged in. We
	// capture both streams and parse merged text.
	cmd := exec.CommandContext(ctx, bin, "auth", "status", "-h", "github.com")
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	runErr := cmd.Run()
	merged := stdout.String() + "\n" + stderr.String()

	info := &AuthStatusInfo{}
	if m := loggedInLine.FindStringSubmatch(merged); m != nil {
		info.LoggedIn = true
		info.Username = m[1]
	} else if m := loggedInLineLegacy.FindStringSubmatch(merged); m != nil {
		info.LoggedIn = true
		info.Username = m[1]
	}
	if m := scopesLine.FindStringSubmatch(merged); m != nil {
		for _, sm := range scopeItem.FindAllStringSubmatch(m[1], -1) {
			info.Scopes = append(info.Scopes, sm[1])
		}
	}
	for _, s := range info.Scopes {
		if s == codespaceScope {
			info.HasCodespaces = true
			break
		}
	}

	if !info.LoggedIn {
		// Distinguish "gh ran but no session" from "gh blew up".
		if runErr != nil {
			// `gh auth status` exits 1 when not logged in; if we also
			// failed to parse a username, treat as not-logged-in.
			return info, &NotLoggedInError{}
		}
		return info, &NotLoggedInError{}
	}
	if !info.HasCodespaces {
		return info, &NeedsCodespaceScopeError{Scopes: info.Scopes}
	}
	if runErr != nil {
		return info, fmt.Errorf("gh auth status: %w: %s", runErr, strings.TrimSpace(merged))
	}
	return info, nil
}

// Codespace is one row of `gh codespace list --json ...`. Only the
// fields the controller actually consumes are decoded.
type Codespace struct {
	Name        string `json:"name"`
	DisplayName string `json:"displayName,omitempty"`
	State       string `json:"state,omitempty"`
	Repository  string `json:"repository,omitempty"`
}

// List returns every codespace visible to the authenticated user.
func List(ctx context.Context) ([]Codespace, error) {
	bin, err := Binary()
	if err != nil {
		return nil, err
	}
	cmd := exec.CommandContext(ctx, bin, "codespace", "list",
		"--json", "name,displayName,state,repository")
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("gh codespace list: %w: %s", err, strings.TrimSpace(stderr.String()))
	}
	var out []Codespace
	if err := json.Unmarshal(stdout.Bytes(), &out); err != nil {
		return nil, fmt.Errorf("parse `gh codespace list` output: %w", err)
	}
	return out, nil
}

// Resolve returns the codespace named `name`, or — if name is empty
// — the first codespace in state "Available". Returns *NotFoundError
// when nothing matches so callers can render a tidy 404.
func Resolve(ctx context.Context, name string) (*Codespace, error) {
	cs, err := List(ctx)
	if err != nil {
		return nil, err
	}
	if name != "" {
		for i := range cs {
			if cs[i].Name == name {
				return &cs[i], nil
			}
		}
		return nil, &NotFoundError{Name: name}
	}
	for i := range cs {
		if strings.EqualFold(cs[i].State, "Available") {
			return &cs[i], nil
		}
	}
	return nil, &NotFoundError{Name: ""}
}

// NotFoundError is returned by Resolve when no codespace matches.
type NotFoundError struct{ Name string }

func (e *NotFoundError) Error() string {
	if e.Name == "" {
		return "no running codespace found (start one in the GitHub UI or pass --name)"
	}
	return fmt.Sprintf("codespace %q not found in `gh codespace list`", e.Name)
}

// SSH runs `script` on the named codespace through `gh codespace
// ssh`. The script is fed via stdin to bash, so it can be
// multi-line. Returns the combined stdout/stderr.
//
// This is the workhorse for codespace bootstrap and remote spawn.
// All host-side commands route through here.
//
// Optional scriptArgs are appended after `bash -s --` so the script
// receives them as positional parameters ($1, $2, ...). This lets us
// invoke embedded helpers (start_workers_local.sh, etc.) with their
// own flag set without quoting nightmares.
func SSH(ctx context.Context, name, script string, scriptArgs ...string) ([]byte, error) {
	bin, err := Binary()
	if err != nil {
		return nil, err
	}
	if name == "" {
		return nil, errors.New("gh.SSH: codespace name is required")
	}
	cmdArgs := []string{"codespace", "ssh", "-c", name, "--", "bash", "-s"}
	if len(scriptArgs) > 0 {
		// `bash -s -- arg1 arg2` — the inner `--` ends bash's own
		// option parsing so getopts inside the script sees arg1 as $1.
		cmdArgs = append(cmdArgs, "--")
		cmdArgs = append(cmdArgs, scriptArgs...)
	}
	cmd := exec.CommandContext(ctx, bin, cmdArgs...)
	cmd.Stdin = strings.NewReader(script)
	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out
	if err := cmd.Run(); err != nil {
		return out.Bytes(), fmt.Errorf("gh codespace ssh -c %s: %w: %s", name, err, strings.TrimSpace(out.String()))
	}
	return out.Bytes(), nil
}

// CP copies a single local file to the named codespace using `gh
// codespace cp`. `dst` is a path on the remote (e.g. "~/foo.run").
func CP(ctx context.Context, name, src, dst string) error {
	bin, err := Binary()
	if err != nil {
		return err
	}
	if name == "" {
		return errors.New("gh.CP: codespace name is required")
	}
	// `gh codespace cp -e` interprets `remote:` syntax; we always
	// upload, so the remote path goes second.
	cmd := exec.CommandContext(ctx, bin, "codespace", "cp", "-e",
		"-c", name, src, "remote:"+dst)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("gh codespace cp %s -> %s: %w: %s", src, dst, err, strings.TrimSpace(stderr.String()))
	}
	return nil
}

// ReleaseAsset is one entry in a GitHub release's `assets` array.
// Only the fields the controller consumes are decoded.
type ReleaseAsset struct {
	Name string `json:"name"`
	Size int64  `json:"size"`
}

// ReleaseInfo is the subset of `gh release view --json ...` we use.
type ReleaseInfo struct {
	TagName string         `json:"tagName"`
	Assets  []ReleaseAsset `json:"assets"`
}

// ReleaseView returns release metadata (tag + assets). If tag is
// "latest" or empty, gh resolves the latest release for the repo.
// repo is "OWNER/REPO".
func ReleaseView(ctx context.Context, repo, tag string) (*ReleaseInfo, error) {
	bin, err := Binary()
	if err != nil {
		return nil, err
	}
	if repo == "" {
		return nil, errors.New("gh.ReleaseView: repo is required")
	}
	args := []string{"release", "view", "-R", repo}
	if tag != "" && tag != "latest" {
		args = append(args, tag)
	}
	args = append(args, "--json", "tagName,assets")
	cmd := exec.CommandContext(ctx, bin, args...)
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("gh release view %s %s: %w: %s", repo, tag, err, strings.TrimSpace(stderr.String()))
	}
	var info ReleaseInfo
	if err := json.Unmarshal(stdout.Bytes(), &info); err != nil {
		return nil, fmt.Errorf("parse `gh release view` output: %w", err)
	}
	return &info, nil
}

// ReleaseDownload downloads a single asset from a release into
// destDir. Pattern is matched by `gh release download --pattern`.
func ReleaseDownload(ctx context.Context, repo, tag, pattern, destDir string) error {
	bin, err := Binary()
	if err != nil {
		return err
	}
	args := []string{"release", "download", "-R", repo}
	if tag != "" && tag != "latest" {
		args = append(args, tag)
	}
	args = append(args, "--pattern", pattern, "--dir", destDir, "--clobber")
	cmd := exec.CommandContext(ctx, bin, args...)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("gh release download %s %s pattern=%s: %w: %s", repo, tag, pattern, err, strings.TrimSpace(stderr.String()))
	}
	return nil
}
