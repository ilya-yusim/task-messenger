package ec2snapshot

import "time"

// SpawnResult mirrors the uniform API shape used by other backends.
type SpawnResult struct {
	ID    string `json:"id"`
	OK    bool   `json:"ok"`
	PID   int    `json:"pid,omitempty"`
	Error string `json:"error,omitempty"`
}

// StatusValue matches the EC2 status vocabulary so the API can render
// the same host-state badges for ec2-snapshot hosts.
type StatusValue string

const (
	StatusOK                 StatusValue = "ok"
	StatusStopped            StatusValue = "stopped"
	StatusStarting           StatusValue = "starting"
	StatusSSMOffline         StatusValue = "ssm-offline"
	StatusInstanceNotFound   StatusValue = "instance-not-found"
	StatusInstanceTerminated StatusValue = "instance-terminated"
	StatusAWSAuthError       StatusValue = "aws-auth-error"
)

// InstanceStatus is the snapshot returned by QueryStatus.
type InstanceStatus struct {
	Status     StatusValue
	InstanceID string
	Region     string
	Detail     string
}

type snapshotState struct {
	Version int                          `json:"version"`
	Hosts   map[string]snapshotHostState `json:"hosts"`
}

type snapshotHostState struct {
	CurrentAmiID            string    `json:"current_ami_id,omitempty"`
	PreviousAmiID           string    `json:"previous_ami_id,omitempty"`
	LineageTag              string    `json:"lineage_tag,omitempty"`
	UpdatedAt               time.Time `json:"updated_at,omitempty"`
	LastBootstrapReleaseTag string    `json:"last_bootstrap_release_tag,omitempty"`
}
