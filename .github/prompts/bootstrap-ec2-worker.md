# Bootstrap an EC2 worker — agent hand-off

Use this prompt to provision tm-worker on an AWS EC2 host through
worker-farm and start workers through the EC2 backend. The controller
(tm-worker-farm) owns the lifecycle: create/start, bootstrap, spawn,
stop, log tail, and idle stop.

## Inputs

- A host entry in hosts.json with backend set to ec2 and a valid EC2
  config block.
- AWS credentials for the controller process with EC2 + SSM permissions
  and iam:PassRole for the Launch Template instance profile role.
- A Launch Template ID and version in the target region.
- A release tag whose tm-worker linux x86_64 installer asset exists.
- A worker config path on the instance (default
  ~/.config/task-messenger/tm-worker/config-worker.json).

## Files of interest

- [worker-farm/README.md](../../worker-farm/README.md) — operator
  documentation and troubleshooting.
- [worker-farm/internal/inventory/inventory.go](../../worker-farm/internal/inventory/inventory.go) —
  ec2 inventory schema and validation.
- [worker-farm/internal/ec2/ec2.go](../../worker-farm/internal/ec2/ec2.go) —
  resolve/create/start/stop instance and SSM online checks.
- [worker-farm/internal/ec2/bootstrap.go](../../worker-farm/internal/ec2/bootstrap.go) —
  release asset resolution, remote download/upload, install execution.
- [worker-farm/internal/ec2/install_tm_worker_release.sh](../../worker-farm/internal/ec2/install_tm_worker_release.sh) —
  helper script run on the instance during bootstrap.
- [worker-farm/internal/ec2/manager.go](../../worker-farm/internal/ec2/manager.go) —
  spawn/stop/poll lifecycle and idle auto-stop.
- [worker-farm/internal/ec2/start_workers_local.sh](../../worker-farm/internal/ec2/start_workers_local.sh) —
  remote spawn helper script.
- [worker-farm/internal/api/server.go](../../worker-farm/internal/api/server.go) —
  EC2 route dispatch for host status/bootstrap/spawn/stop/log.
- [worker-farm/cmd/tm-worker-farm/main.go](../../worker-farm/cmd/tm-worker-farm/main.go) —
  manager wiring and controller startup.

## Steps the agent performs

1. Read the EC2 host entry and validate region, Launch Template, and
   optional worker_bin/config overrides.
2. Resolve the managed instance by tags, or create/start it from the
   Launch Template, then wait for SSM online state.
3. Resolve the requested release and worker installer asset from GitHub.
4. Bootstrap tm-worker on the instance:
   download asset, upload helper when needed, run installer with
   non-interactive flags, and persist helper-state cache.
5. Spawn workers via SSM using start_workers_local.sh and parse
   manifest output fenced by sentinel markers.
6. Register workers in the controller registry and mirror manifest
   locally for audit/debugging.
7. Poll liveness and transition workers to exited when remote PIDs
   disappear.
8. Auto-stop idle instances after the idle timeout when no workers are
   active.

## Verification

- GET /hosts/{id}/status returns ok for the EC2 host.
- POST /hosts/{id}/bootstrap returns success and installer_log without
  shell-unbound-variable failures.
- POST /workers for an EC2 host returns started workers with valid IDs
  and PIDs.
- GET /workers shows running rows for the EC2 host and logs are
  retrievable from /workers/{id}/log.
- After workers exit, the instance reaches stopped state after the idle
  timeout.

## Out of scope

- Auto Scaling Group or multi-instance fleet scheduling.
- CloudWatch log shipping.
- SSH fallback transport.
- Cross-glibc binary compatibility fixes in the release pipeline.
