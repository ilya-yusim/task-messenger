# EC2 snapshot backend — agent hand-off

Use this prompt to implement or update the `ec2-snapshot` backend in
`tm-worker-farm`. The backend launches from a host-specific AMI pointer,
promotes a new AMI after successful bootstrap, and terminates idle
instances when no workers remain.

## Inputs

- A host entry in `hosts.json` with `backend` set to `ec2-snapshot` and
a valid `ec2_snapshot` config block.
- AWS credentials for the controller process with EC2 + SSM permissions
and `iam:PassRole` for the Launch Template instance profile role.
- A Launch Template ID/version and a baseline `current_ami_id` in the
target region.
- A `tm-worker` release tag whose Linux installer asset is available.
- A controller-local runtime state path for mutable AMI lineage.

## Files of interest

- [worker-farm/README.md](../../worker-farm/README.md) — operator-facing
  backend behavior and inventory examples.
- [worker-farm/internal/inventory/inventory.go](../../worker-farm/internal/inventory/inventory.go) — `ec2-snapshot` schema and validation.
- [worker-farm/internal/inventory/inventory_test.go](../../worker-farm/internal/inventory/inventory_test.go) — schema test coverage.
- [worker-farm/internal/ec2snapshot/ec2.go](../../worker-farm/internal/ec2snapshot/ec2.go) — ensure/start instance from launch template + AMI override.
- [worker-farm/internal/ec2snapshot/bootstrap.go](../../worker-farm/internal/ec2snapshot/bootstrap.go) — install `tm-worker` and promote AMIs.
- [worker-farm/internal/ec2snapshot/manager.go](../../worker-farm/internal/ec2snapshot/manager.go) — spawn/stop/poll lifecycle and idle terminate.
- [worker-farm/internal/ec2snapshot/types.go](../../worker-farm/internal/ec2snapshot/types.go) — runtime types and state shape.
- [worker-farm/internal/ec2common/](../../worker-farm/internal/ec2common/) — shared AWS/SSM helpers.
- [worker-farm/internal/api/server.go](../../worker-farm/internal/api/server.go) — API routing by backend.
- [worker-farm/cmd/tm-worker-farm/main.go](../../worker-farm/cmd/tm-worker-farm/main.go) — manager wiring and controller startup.

## Steps the agent performs

1. Validate `ec2-snapshot` inventory fields (`region`, launch template,
   `current_ami_id`, and optional overrides), then reject malformed host
   entries with clear errors.
2. Resolve the active instance for the host by tags, or launch/start one
   from the configured Launch Template with `current_ami_id` override,
   then wait for SSM online state.
3. Bootstrap `tm-worker` on the instance through SSM using the selected
   release asset and verify the installed binary can run.
4. Promote a new AMI after successful bootstrap, wait until it is
   available, and atomically update the host runtime state so
   `current_ami_id` points at the new image (while retaining one rollback
   image when configured).
5. Spawn workers through SSM, parse manifest output, register workers in
   the controller, and keep liveness polling aligned with remote PID
   state.
6. Terminate the instance after idle timeout when the host has no active
   workers, and clear stale instance cache/state so later spawns can
   recreate cleanly from the current AMI pointer.
7. Keep API routes and UI host status paths backend-aware so
   `ec2-snapshot` supports the same operational flows as other managed
   backends.

## Verification

- `go test ./worker-farm/internal/inventory` passes with
  `ec2-snapshot` valid/invalid cases.
- `go test ./worker-farm/internal/ec2snapshot/...` passes.
- `go test ./worker-farm/internal/api` passes with backend routing for
  `ec2-snapshot` paths.
- Bootstrap updates only the target host AMI pointer and leaves other
  hosts unchanged.
- Spawn works from the host's current AMI after instance recreation.
- Idle termination occurs only when no workers remain for the host.

## Out of scope

- Cross-account AMI sharing.
- Multi-region AMI replication.
- External image bakery/orchestration systems outside controller-driven
  promotion.
- Fleet scheduling across multiple instances per host entry.
