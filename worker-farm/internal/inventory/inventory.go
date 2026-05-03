// Package inventory loads and validates the operator's host
// inventory file (`hosts.json`).
//
// The inventory describes every host the controller can talk to.
// Today only the `local` and `codespace` backends are wired up; `ssh`
// and `gcp-iap` are accepted by the parser so future backends can
// activate them without a config-file format break.
//
// The schema is JSON, not YAML, to keep `worker-farm` stdlib-only.
package inventory

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
)

// BackendKind is the discriminator string in `hosts[].backend`.
type BackendKind string

const (
	BackendLocal     BackendKind = "local"
	BackendCodespace BackendKind = "codespace"
	BackendSSH       BackendKind = "ssh"
	BackendGCPIAP    BackendKind = "gcp-iap"
)

// Inventory is the parsed contents of hosts.json. The default
// (synthesized when no file exists) is a single-host inventory with
// `local` so the existing flag-driven UX keeps working.
type Inventory struct {
	Hosts []Host `json:"hosts"`
}

// Host is one entry in the inventory.
type Host struct {
	ID        string        `json:"id"`
	Backend   BackendKind   `json:"backend"`
	Codespace *CodespaceCfg `json:"codespace,omitempty"`
	SSH       *SSHCfg       `json:"ssh,omitempty"`
	GCPIAP    *GCPIAPCfg    `json:"gcp_iap,omitempty"`
}

// CodespaceCfg is the per-host config for backend=codespace.
type CodespaceCfg struct {
	// Name is the codespace name as reported by `gh codespace list`.
	// Empty string means "auto-pick first running codespace" — useful
	// for the common single-codespace case.
	Name string `json:"name"`
	// WorkerBin is the path to tm-worker on the remote host. Empty ⇒
	// resolve via $PATH on the remote.
	WorkerBin string `json:"worker_bin,omitempty"`
	// Config is the path to config-worker.json on the remote host.
	// Empty ⇒ remote default (~/.config/task-messenger/...).
	Config string `json:"config,omitempty"`
}

// SSHCfg reserves the shape for backend=ssh; not used yet.
type SSHCfg struct {
	Host string `json:"host"`
	User string `json:"user,omitempty"`
	Port int    `json:"port,omitempty"`
}

// GCPIAPCfg reserves the shape for backend=gcp-iap; not used yet.
type GCPIAPCfg struct {
	Project  string `json:"project"`
	Zone     string `json:"zone"`
	Instance string `json:"instance"`
}

// Error is the typed parse/validation error. It carries the host
// index and the offending field so the operator gets a precise
// "fix this line" message instead of a generic "bad config".
type Error struct {
	// Path is the inventory file path (empty when validating a
	// synthesized default).
	Path string
	// Index is the 0-based host index. -1 for whole-file errors.
	Index int
	// Field is the offending JSON field name (e.g. "id", "backend").
	// Empty for structural errors.
	Field string
	// Msg is the human-readable explanation.
	Msg string
}

func (e *Error) Error() string {
	switch {
	case e.Index >= 0 && e.Field != "":
		return fmt.Sprintf("inventory %s: hosts[%d].%s: %s", e.Path, e.Index, e.Field, e.Msg)
	case e.Index >= 0:
		return fmt.Sprintf("inventory %s: hosts[%d]: %s", e.Path, e.Index, e.Msg)
	default:
		return fmt.Sprintf("inventory %s: %s", e.Path, e.Msg)
	}
}

// DefaultPath returns the OS-specific location of hosts.json.
//
//	POSIX:   $XDG_CONFIG_HOME/tm-worker-farm/hosts.json
//	         (fallback ~/.config/tm-worker-farm/hosts.json)
//	Windows: %APPDATA%\tm-worker-farm\hosts.json
func DefaultPath() (string, error) {
	if runtime.GOOS == "windows" {
		base := os.Getenv("APPDATA")
		if base == "" {
			home, err := os.UserHomeDir()
			if err != nil {
				return "", err
			}
			base = filepath.Join(home, "AppData", "Roaming")
		}
		return filepath.Join(base, "tm-worker-farm", "hosts.json"), nil
	}
	if base := os.Getenv("XDG_CONFIG_HOME"); base != "" {
		return filepath.Join(base, "tm-worker-farm", "hosts.json"), nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".config", "tm-worker-farm", "hosts.json"), nil
}

// Default returns the synthesized single-host inventory used when no
// hosts.json exists.
func Default() *Inventory {
	return &Inventory{Hosts: []Host{{ID: "local", Backend: BackendLocal}}}
}

// Load reads and validates hosts.json. If path does not exist,
// returns Default() and the bool `synthesized=true`. Any other I/O
// error or validation failure is returned as-is (validation errors
// are *Error).
func Load(path string) (*Inventory, bool, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return Default(), true, nil
		}
		return nil, false, fmt.Errorf("read inventory %s: %w", path, err)
	}
	var inv Inventory
	if err := json.Unmarshal(data, &inv); err != nil {
		return nil, false, &Error{Path: path, Index: -1, Msg: "parse JSON: " + err.Error()}
	}
	if err := inv.validate(path); err != nil {
		return nil, false, err
	}
	return &inv, false, nil
}

// Get returns the host with the given id, or false.
func (inv *Inventory) Get(id string) (Host, bool) {
	for _, h := range inv.Hosts {
		if h.ID == id {
			return h, true
		}
	}
	return Host{}, false
}

func (inv *Inventory) validate(path string) error {
	if len(inv.Hosts) == 0 {
		return &Error{Path: path, Index: -1, Msg: "hosts list is empty (delete the file to use the synthesized default, or add at least one entry)"}
	}
	seen := make(map[string]int, len(inv.Hosts))
	for i, h := range inv.Hosts {
		if h.ID == "" {
			return &Error{Path: path, Index: i, Field: "id", Msg: "must be non-empty"}
		}
		if prev, dup := seen[h.ID]; dup {
			return &Error{Path: path, Index: i, Field: "id", Msg: fmt.Sprintf("duplicate id %q (also at hosts[%d])", h.ID, prev)}
		}
		seen[h.ID] = i

		switch h.Backend {
		case BackendLocal:
			// no extra config required.
		case BackendCodespace:
			if h.Codespace == nil {
				return &Error{Path: path, Index: i, Field: "codespace", Msg: "required for backend=codespace"}
			}
		case BackendSSH:
			if h.SSH == nil || h.SSH.Host == "" {
				return &Error{Path: path, Index: i, Field: "ssh", Msg: "required for backend=ssh (with non-empty host)"}
			}
		case BackendGCPIAP:
			if h.GCPIAP == nil || h.GCPIAP.Project == "" || h.GCPIAP.Zone == "" || h.GCPIAP.Instance == "" {
				return &Error{Path: path, Index: i, Field: "gcp_iap", Msg: "required for backend=gcp-iap (with project/zone/instance)"}
			}
		case "":
			return &Error{Path: path, Index: i, Field: "backend", Msg: "must be set (one of: local, codespace, ssh, gcp-iap)"}
		default:
			return &Error{Path: path, Index: i, Field: "backend", Msg: fmt.Sprintf("unknown backend %q (one of: local, codespace, ssh, gcp-iap)", h.Backend)}
		}
	}
	return nil
}
