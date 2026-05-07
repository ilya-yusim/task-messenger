package inventory

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeInventoryFile(t *testing.T, body string) string {
	t.Helper()
	dir := t.TempDir()
	p := filepath.Join(dir, "hosts.json")
	if err := os.WriteFile(p, []byte(body), 0o644); err != nil {
		t.Fatalf("write hosts.json: %v", err)
	}
	return p
}

func TestLoadEC2HostValid(t *testing.T) {
	p := writeInventoryFile(t, `{
  "hosts": [
    {
      "id": "aws-worker-1",
      "backend": "ec2",
      "ec2": {
        "region": "us-east-1",
        "launch_template_id": "lt-0abc123",
        "launch_template_version": "$Latest",
        "worker_bin": "tm-worker",
        "config": "~/.config/task-messenger/tm-worker/config-worker.json",
        "auto_terminate": false
      }
    }
  ]
}`)

	inv, synthesized, err := Load(p)
	if err != nil {
		t.Fatalf("Load() error = %v", err)
	}
	if synthesized {
		t.Fatalf("Load() synthesized = true, want false")
	}
	if len(inv.Hosts) != 1 {
		t.Fatalf("len(inv.Hosts) = %d, want 1", len(inv.Hosts))
	}
	h := inv.Hosts[0]
	if h.Backend != BackendEC2 {
		t.Fatalf("host backend = %q, want %q", h.Backend, BackendEC2)
	}
	if h.EC2 == nil {
		t.Fatal("host.ec2 is nil, want populated config")
	}
	if h.EC2.Region != "us-east-1" {
		t.Fatalf("host.ec2.region = %q, want us-east-1", h.EC2.Region)
	}
	if h.EC2.LaunchTemplateID != "lt-0abc123" {
		t.Fatalf("host.ec2.launch_template_id = %q, want lt-0abc123", h.EC2.LaunchTemplateID)
	}
}

func TestLoadEC2HostInvalid(t *testing.T) {
	tests := []struct {
		name       string
		inventory  string
		wantSubstr string
	}{
		{
			name: "missing region",
			inventory: `{
  "hosts": [
    {
      "id": "aws-worker-1",
      "backend": "ec2",
      "ec2": {
        "launch_template_id": "lt-0abc123"
      }
    }
  ]
}`,
			wantSubstr: "hosts[0].ec2.region",
		},
		{
			name: "missing launch template id",
			inventory: `{
  "hosts": [
    {
      "id": "aws-worker-1",
      "backend": "ec2",
      "ec2": {
        "region": "us-east-1"
      }
    }
  ]
}`,
			wantSubstr: "hosts[0].ec2.launch_template_id",
		},
		{
			name: "bad launch template id format",
			inventory: `{
  "hosts": [
    {
      "id": "aws-worker-1",
      "backend": "ec2",
      "ec2": {
        "region": "us-east-1",
        "launch_template_id": "template-123"
      }
    }
  ]
}`,
			wantSubstr: "must match ^lt-[0-9a-f]+$",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			p := writeInventoryFile(t, tt.inventory)
			_, synthesized, err := Load(p)
			if synthesized {
				t.Fatalf("Load() synthesized = true, want false")
			}
			if err == nil {
				t.Fatalf("Load() error = nil, want validation error containing %q", tt.wantSubstr)
			}
			if got := err.Error(); !strings.Contains(got, tt.wantSubstr) {
				t.Fatalf("Load() error = %q, want substring %q", got, tt.wantSubstr)
			}
		})
	}
}
