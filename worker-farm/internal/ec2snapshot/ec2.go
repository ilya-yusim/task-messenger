package ec2snapshot

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"strings"
	"time"

	"github.com/aws/aws-sdk-go-v2/aws"
	awsconfig "github.com/aws/aws-sdk-go-v2/config"
	e2 "github.com/aws/aws-sdk-go-v2/service/ec2"
	ec2types "github.com/aws/aws-sdk-go-v2/service/ec2/types"
	"github.com/aws/aws-sdk-go-v2/service/ssm"
	ssmtypes "github.com/aws/aws-sdk-go-v2/service/ssm/types"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/inventory"
	"github.com/ilya-yusim/task-messenger/worker-farm/internal/paths"
)

const (
	tagHostKey       = "tm-worker-farm-host"
	tagControllerKey = "tm-worker-farm-ctrl"

	defaultLTVersion = "$Latest"
	ssmPollInterval  = 10 * time.Second
	ssmOnlineTimeout = 5 * time.Minute
	ec2WaitTimeout   = 5 * time.Minute
)

// Instance is the resolved managed EC2 instance for a worker-farm host.
type Instance struct {
	InstanceID string
	Region     string
	SSMTarget  string
}

type clients struct {
	ec2 *e2.Client
	ssm *ssm.Client
}

// EnsureInstance resolves or creates the managed EC2 instance for one
// worker-farm host, ensures it is running, then waits for SSM online.
func EnsureInstance(ctx context.Context, cfg inventory.EC2SnapshotCfg, hostID, controllerID string) (*Instance, error) {
	if strings.TrimSpace(hostID) == "" {
		return nil, errors.New("ec2snapshot.EnsureInstance: hostID is required")
	}
	if strings.TrimSpace(controllerID) == "" {
		return nil, errors.New("ec2snapshot.EnsureInstance: controllerID is required")
	}
	cfg = overlayPromotedImage(cfg, hostID)

	cl, err := newClients(ctx, cfg.Region)
	if err != nil {
		return nil, err
	}

	inst, err := findManagedInstance(ctx, cl.ec2, hostID, controllerID)
	if err != nil {
		return nil, err
	}
	if inst == nil {
		inst, err = createManagedInstance(ctx, cl.ec2, cfg, hostID, controllerID)
		if err != nil {
			return nil, err
		}
	}

	state := instanceStateName(inst)
	if err := ensureInstanceRunning(ctx, cl.ec2, aws.ToString(inst.InstanceId), state); err != nil {
		return nil, err
	}
	if err := waitForSSMOnline(ctx, cl.ssm, aws.ToString(inst.InstanceId)); err != nil {
		return nil, err
	}

	id := aws.ToString(inst.InstanceId)
	return &Instance{InstanceID: id, Region: strings.TrimSpace(cfg.Region), SSMTarget: id}, nil
}

// QueryStatus probes the current EC2 + SSM state for a managed host
// without mutating anything.
func QueryStatus(ctx context.Context, cfg inventory.EC2SnapshotCfg, hostID, controllerID string) InstanceStatus {
	region := strings.TrimSpace(cfg.Region)
	cl, err := newClients(ctx, region)
	if err != nil {
		return InstanceStatus{Status: StatusAWSAuthError, Region: region, Detail: err.Error()}
	}

	inst, err := findManagedInstance(ctx, cl.ec2, hostID, controllerID)
	if err != nil {
		return InstanceStatus{Status: StatusAWSAuthError, Region: region, Detail: err.Error()}
	}
	if inst == nil {
		return InstanceStatus{Status: StatusInstanceNotFound, Region: region}
	}

	id := aws.ToString(inst.InstanceId)
	state := instanceStateName(inst)

	switch state {
	case string(ec2types.InstanceStateNameTerminated), string(ec2types.InstanceStateNameShuttingDown):
		return InstanceStatus{Status: StatusInstanceTerminated, InstanceID: id, Region: region}
	case string(ec2types.InstanceStateNameStopped), string(ec2types.InstanceStateNameStopping):
		return InstanceStatus{Status: StatusStopped, InstanceID: id, Region: region}
	case string(ec2types.InstanceStateNamePending):
		return InstanceStatus{Status: StatusStarting, InstanceID: id, Region: region}
	case string(ec2types.InstanceStateNameRunning):
		// fall through to SSM check
	default:
		return InstanceStatus{Status: StatusAWSAuthError, InstanceID: id, Region: region,
			Detail: fmt.Sprintf("unexpected EC2 state %q", state)}
	}

	online, err := isSSMOnline(ctx, cl.ssm, id)
	if err != nil {
		return InstanceStatus{Status: StatusAWSAuthError, InstanceID: id, Region: region, Detail: err.Error()}
	}
	if !online {
		return InstanceStatus{Status: StatusSSMOffline, InstanceID: id, Region: region}
	}
	return InstanceStatus{Status: StatusOK, InstanceID: id, Region: region}
}

// TerminateInstance requests EC2 termination for the instance.
func TerminateInstance(ctx context.Context, cfg inventory.EC2SnapshotCfg, instanceID string) error {
	if strings.TrimSpace(instanceID) == "" {
		return errors.New("ec2snapshot.TerminateInstance: instanceID is required")
	}
	cl, err := newClients(ctx, cfg.Region)
	if err != nil {
		return err
	}
	_, err = cl.ec2.TerminateInstances(ctx, &e2.TerminateInstancesInput{InstanceIds: []string{instanceID}})
	if err != nil {
		return fmt.Errorf("ec2 terminate %s: %w", instanceID, err)
	}
	return nil
}

func newClients(ctx context.Context, region string) (*clients, error) {
	region = strings.TrimSpace(region)
	if region == "" {
		return nil, errors.New("ec2snapshot: region is required")
	}
	cfg, err := awsconfig.LoadDefaultConfig(ctx, awsconfig.WithRegion(region))
	if err != nil {
		return nil, fmt.Errorf("aws config load region=%s: %w", region, err)
	}
	return &clients{ec2: e2.NewFromConfig(cfg), ssm: ssm.NewFromConfig(cfg)}, nil
}

func findManagedInstance(ctx context.Context, cli *e2.Client, hostID, controllerID string) (*ec2types.Instance, error) {
	out, err := cli.DescribeInstances(ctx, &e2.DescribeInstancesInput{
		Filters: []ec2types.Filter{
			{Name: aws.String("tag:" + tagHostKey), Values: []string{hostID}},
			{Name: aws.String("tag:" + tagControllerKey), Values: []string{controllerID}},
		},
	})
	if err != nil {
		return nil, fmt.Errorf("describe managed instances host=%s: %w", hostID, err)
	}

	var pick *ec2types.Instance
	for i := range out.Reservations {
		for j := range out.Reservations[i].Instances {
			inst := out.Reservations[i].Instances[j]
			state := instanceStateName(&inst)
			if state == string(ec2types.InstanceStateNameTerminated) || state == string(ec2types.InstanceStateNameShuttingDown) {
				continue
			}
			if pick == nil {
				pick = &inst
				continue
			}
			if inst.LaunchTime != nil && pick.LaunchTime != nil && inst.LaunchTime.After(*pick.LaunchTime) {
				pick = &inst
			}
		}
	}
	return pick, nil
}

func createManagedInstance(ctx context.Context, cli *e2.Client, cfg inventory.EC2SnapshotCfg, hostID, controllerID string) (*ec2types.Instance, error) {
	ltVersion := cfg.LaunchTemplateVersion
	if strings.TrimSpace(ltVersion) == "" {
		ltVersion = defaultLTVersion
	}
	input := &e2.RunInstancesInput{
		LaunchTemplate: &ec2types.LaunchTemplateSpecification{
			LaunchTemplateId: aws.String(cfg.LaunchTemplateID),
			Version:          aws.String(ltVersion),
		},
		MinCount: aws.Int32(1),
		MaxCount: aws.Int32(1),
	}
	if amiID := strings.TrimSpace(cfg.CurrentAmiID); amiID != "" {
		input.ImageId = aws.String(amiID)
	}
	input.TagSpecifications = []ec2types.TagSpecification{
		{
			ResourceType: ec2types.ResourceTypeInstance,
			Tags: []ec2types.Tag{
				{Key: aws.String(tagHostKey), Value: aws.String(hostID)},
				{Key: aws.String(tagControllerKey), Value: aws.String(controllerID)},
			},
		},
	}
	out, err := cli.RunInstances(ctx, input)
	if err != nil {
		return nil, fmt.Errorf("run instance host=%s launch_template=%s: %w", hostID, cfg.LaunchTemplateID, err)
	}
	if len(out.Instances) == 0 {
		return nil, fmt.Errorf("run instance host=%s: AWS returned zero instances", hostID)
	}
	return &out.Instances[0], nil
}

func ensureInstanceRunning(ctx context.Context, cli *e2.Client, instanceID, state string) error {
	switch state {
	case string(ec2types.InstanceStateNameRunning):
		return nil
	case string(ec2types.InstanceStateNamePending):
		return waitInstanceRunning(ctx, cli, instanceID)
	case string(ec2types.InstanceStateNameStopped):
		if _, err := cli.StartInstances(ctx, &e2.StartInstancesInput{InstanceIds: []string{instanceID}}); err != nil {
			return fmt.Errorf("start instance %s: %w", instanceID, err)
		}
		return waitInstanceRunning(ctx, cli, instanceID)
	case string(ec2types.InstanceStateNameStopping):
		if err := waitInstanceStopped(ctx, cli, instanceID); err != nil {
			return err
		}
		if _, err := cli.StartInstances(ctx, &e2.StartInstancesInput{InstanceIds: []string{instanceID}}); err != nil {
			return fmt.Errorf("start instance %s: %w", instanceID, err)
		}
		return waitInstanceRunning(ctx, cli, instanceID)
	default:
		return fmt.Errorf("instance %s is in unsupported state %q", instanceID, state)
	}
}

func waitInstanceRunning(ctx context.Context, cli *e2.Client, instanceID string) error {
	waiter := e2.NewInstanceRunningWaiter(cli)
	if err := waiter.Wait(ctx, &e2.DescribeInstancesInput{InstanceIds: []string{instanceID}}, ec2WaitTimeout); err != nil {
		return fmt.Errorf("wait instance %s running: %w", instanceID, err)
	}
	return nil
}

func waitInstanceStopped(ctx context.Context, cli *e2.Client, instanceID string) error {
	waiter := e2.NewInstanceStoppedWaiter(cli)
	if err := waiter.Wait(ctx, &e2.DescribeInstancesInput{InstanceIds: []string{instanceID}}, ec2WaitTimeout); err != nil {
		return fmt.Errorf("wait instance %s stopped: %w", instanceID, err)
	}
	return nil
}

func waitForSSMOnline(ctx context.Context, cli *ssm.Client, instanceID string) error {
	timer := time.NewTimer(ssmOnlineTimeout)
	defer timer.Stop()
	ticker := time.NewTicker(ssmPollInterval)
	defer ticker.Stop()

	for {
		online, err := isSSMOnline(ctx, cli, instanceID)
		if err != nil {
			return err
		}
		if online {
			return nil
		}

		select {
		case <-ctx.Done():
			return fmt.Errorf("wait SSM online instance=%s: %w", instanceID, ctx.Err())
		case <-timer.C:
			return fmt.Errorf("wait SSM online instance=%s: timed out after %s", instanceID, ssmOnlineTimeout)
		case <-ticker.C:
		}
	}
}

func isSSMOnline(ctx context.Context, cli *ssm.Client, instanceID string) (bool, error) {
	out, err := cli.DescribeInstanceInformation(ctx, &ssm.DescribeInstanceInformationInput{
		Filters: []ssmtypes.InstanceInformationStringFilter{
			{Key: aws.String("InstanceIds"), Values: []string{instanceID}},
		},
		MaxResults: aws.Int32(5),
	})
	if err != nil {
		return false, fmt.Errorf("describe SSM instance info %s: %w", instanceID, err)
	}
	for _, info := range out.InstanceInformationList {
		if aws.ToString(info.InstanceId) == instanceID && info.PingStatus == ssmtypes.PingStatusOnline {
			return true, nil
		}
	}
	return false, nil
}

func instanceStateName(inst *ec2types.Instance) string {
	if inst == nil || inst.State == nil {
		return ""
	}
	return string(inst.State.Name)
}

func overlayPromotedImage(cfg inventory.EC2SnapshotCfg, hostID string) inventory.EC2SnapshotCfg {
	st, ok := loadSnapshotHostState(hostID)
	if !ok {
		return cfg
	}
	if amiID := strings.TrimSpace(st.CurrentAmiID); amiID != "" {
		cfg.CurrentAmiID = amiID
	}
	return cfg
}

func loadSnapshotHostState(hostID string) (snapshotHostState, bool) {
	statePath, err := paths.EC2SnapshotStatePath()
	if err != nil {
		log.Printf("ec2snapshot: resolve state path for host=%s: %v", hostID, err)
		return snapshotHostState{}, false
	}
	data, err := os.ReadFile(statePath)
	if err != nil {
		if !errors.Is(err, os.ErrNotExist) {
			log.Printf("ec2snapshot: read runtime state %s: %v", statePath, err)
		}
		return snapshotHostState{}, false
	}
	var state snapshotState
	if err := json.Unmarshal(data, &state); err != nil {
		log.Printf("ec2snapshot: parse runtime state %s: %v", statePath, err)
		return snapshotHostState{}, false
	}
	if state.Hosts == nil {
		return snapshotHostState{}, false
	}
	hostState, ok := state.Hosts[hostID]
	if !ok {
		return snapshotHostState{}, false
	}
	return hostState, true
}
