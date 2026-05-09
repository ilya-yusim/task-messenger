# EC2 Snapshot Backend — Implementation Plan

This plan adds a new `ec2-snapshot` backend to `tm-worker-farm` as a
separate implementation from the current `ec2` backend.

The `ec2-snapshot` model is image-first:

- Bootstrap installs the requested `tm-worker` version on a running
  instance.
- Bootstrap publishes a new AMI for that host after successful
  verification.
- Spawn launches from the host's current AMI.
- When the last worker exits, the instance is terminated.

## Locked decisions

- Backend name: `ec2-snapshot`.
- Separate backend implementation (no major behavior branching inside
  existing `internal/ec2`).
- Per-host image lineage: each host manages its own AMI pointer.
- Automatic image promotion after successful bootstrap.
- Idle action: terminate instance when no workers remain.

## Target inventory schema

Add a new backend discriminator and config block in
`worker-farm/internal/inventory/inventory.go`:

```jsonc
{
  "id": "aws-worker-1",
  "backend": "ec2-snapshot",
  "ec2_snapshot": {
    "region": "us-east-2",
    "launch_template_id": "lt-xxxxxxxxxxxxxxxxx",
    "launch_template_version": "$Latest",
    "current_ami_id": "ami-xxxxxxxxxxxxxxxxx",
    "worker_bin": "tm-worker",
    "config": "~/.config/task-messenger/tm-worker/config-worker.json",
    "terminate_on_idle": true
  }
}
```

Validation rules:

- `region` required.
- `launch_template_id` required and must match launch-template format.
- `current_ami_id` required and must match AMI format.
- `terminate_on_idle` defaults to `true` for this backend.

## Concrete implementation sequence

### 1) Add new backend type and inventory fields

Files:

- `worker-farm/internal/inventory/inventory.go`
- `worker-farm/internal/inventory/inventory_test.go`

Tasks:

1. Add `BackendEC2Snapshot` discriminator string `"ec2-snapshot"`.
2. Add `EC2SnapshotCfg` struct.
3. Add host field `EC2Snapshot *EC2SnapshotCfg`.
4. Add validation for required `current_ami_id` and field formats.
5. Add tests for valid/invalid `ec2-snapshot` host entries.

### 2) Create new backend package

Create directory:

- `worker-farm/internal/ec2snapshot/`

Initial files:

- `ec2snapshot/ec2.go`
- `ec2snapshot/bootstrap.go`
- `ec2snapshot/manager.go`
- `ec2snapshot/types.go`

Tasks:

1. Copy minimal reusable patterns from `internal/ec2` without reusing
   lifecycle assumptions.
2. Keep package APIs parallel to current backend where practical:
   `EnsureInstance`, `QueryStatus`, `Bootstrap`, `Manager.Spawn/Stop/Run`.

### 3) Factor shared AWS/SSM helpers

Create shared package:

- `worker-farm/internal/ec2common/`

Candidate helpers:

- AWS client construction
- SSM `SendCommand` + invocation polling
- tag constants + managed instance lookup
- common status checks (running/stopped/SSM online)

Tasks:

1. Move helper code that is generic and stateless.
2. Keep backend-specific behavior in backend packages.
3. Add small unit tests where possible for parser/helper logic.

### 4) Implement image-first instance resolution

Files:

- `worker-farm/internal/ec2snapshot/ec2.go`

Tasks:

1. Resolve active instance for host via tags.
2. If no active instance exists, run instance using launch template plus
   AMI override from `current_ami_id`.
3. Wait for `running`, then SSM online.
4. Return instance identity.

Notes:

- Ensure tag identity remains `host + controller`.
- Avoid creating duplicate instances under concurrent spawn calls.

### 5) Implement bootstrap + AMI promotion

Files:

- `worker-farm/internal/ec2snapshot/bootstrap.go`

Tasks:

1. Ensure instance running.
2. Install selected `tm-worker` release using existing helper flow
   (download/verify/install).
3. Verify installed binary can run (`--version`) and detect GLIBC
   mismatch before promotion.
4. Create image from instance (`CreateImage`) after success.
5. Wait for image availability.
6. Atomically update host's `current_ami_id` pointer in controller state.
7. Optionally deregister immediate previous AMI after successful
   promotion (development flag can keep one prior image).

### 6) Persist per-host AMI pointer and metadata

Files:

- `worker-farm/internal/ec2snapshot/bootstrap.go`
- `worker-farm/internal/ec2snapshot/types.go`
- (if needed) `worker-farm/internal/paths` or state file helpers

Tasks:

1. Add controller-local state file for mutable host image metadata,
   separate from static inventory.
2. Store at least: `host_id`, `current_ami_id`, `previous_ami_id`,
   `tag`, `updated_at`.
3. On startup, merge runtime state over inventory baseline.

### 7) Implement spawn/stop/poll with terminate-on-idle

Files:

- `worker-farm/internal/ec2snapshot/manager.go`

Tasks:

1. Spawn:
   - Ensure instance from `current_ami_id`.
   - Run `start_workers_local.sh` through SSM, parse manifest.
   - Register workers in registry.
2. Stop:
   - Same PID-based terminate flow as current backend.
3. Poll:
   - Same liveness poll strategy.
4. Idle:
   - Replace stop-on-idle with terminate-on-idle.
   - Clear cached in-memory state for terminated instance.

### 8) Wire API server and main

Files:

- `worker-farm/internal/api/server.go`
- `worker-farm/cmd/tm-worker-farm/main.go`

Tasks:

1. Add `ec2snapshot.Manager` field and options.
2. Route status/bootstrap/spawn/stop/log/purge by backend discriminator.
3. Keep existing `ec2` backend unchanged and runnable in parallel.

### 9) UI + status vocabulary

Files:

- `worker-farm/internal/api/server.go`
- `worker-farm/internal/webassets/web/app.js`

Tasks:

1. Ensure host status payload includes `backend: "ec2-snapshot"`.
2. Reuse EC2-like status values where possible.
3. Ensure Bootstrap button visibility includes `ec2-snapshot` backend.

### 10) Documentation and prompts

Files:

- `worker-farm/README.md`
- `.github/prompts/bootstrap-ec2-worker.md`

Tasks:

1. Add operator docs for `ec2-snapshot` inventory and lifecycle.
2. Document AMI promotion behavior and per-host lineage.
3. Document costs and tradeoffs vs stop/start backend.
4. Update handoff prompt with explicit image-promotion steps.

### 11) Verification matrix

1. Build and unit tests pass:
   - `meson compile -C builddir`
   - backend-specific tests.
2. Bootstrap on `aws-worker-1` updates only host 1 AMI.
3. Bootstrap on `aws-worker-2` updates only host 2 AMI.
4. Terminate instances manually; next spawn recreates from each host's
   own `current_ami_id` without re-bootstrap.
5. Last worker exit triggers instance termination.
6. Existing `ec2` backend still works unchanged.

## Finalized issue decisions

1. Runtime state location and format for mutable host AMI pointers:
    - Use a controller-local runtime state file at
       `%APPDATA%/tm-worker-farm/ec2-snapshot-state.json` on Windows and
       `${XDG_STATE_HOME:-~/.local/state}/tm-worker-farm/ec2-snapshot-state.json`
       on Linux.
    - Keep inventory as immutable baseline config; store mutable AMI
       lineage only in runtime state.
    - Use versioned JSON shape:
       - top-level: `version`, `hosts`
       - per-host: `current_ami_id`, `previous_ami_id`, `lineage_tag`,
          `updated_at`, `last_bootstrap_release_tag`
    - Startup merge rule: load inventory first, then overlay runtime
       state for matching `host_id` entries.
    - Write rule: update state atomically (`.tmp` write + rename)
       immediately after successful AMI promotion.
    - Failure rule: if runtime state is missing/corrupt, log warning and
       continue from inventory `current_ami_id`.

2. AMI cleanup policy during development:
      - Keep exactly one previous AMI per host for rollback.
      - On successful promotion, set `previous_ami_id` to the prior
         `current_ami_id`, then set `current_ami_id` to the new AMI.
      - Deregister AMIs older than `previous_ami_id` for that host lineage
         to avoid accumulation.
      - If rollback succeeds and a new promotion is later completed,
         continue retaining only the new `current_ami_id` and one
         `previous_ami_id`.

3. Locking model for concurrent bootstrap/spawn per host:
      - Use a per-host lock keyed by `host_id`; different hosts do not
         block each other.
      - Use shared mode for spawn operations and exclusive mode for
         bootstrap/promotion, rollback, and idle-terminate.
      - Admission rule: once a bootstrap request is queued for a host,
         pause admission of new spawn operations on that host to prevent
         bootstrap starvation.
      - Ensure-instance deduplication: guard instance ensure/start path
         with singleflight keyed by `host_id` to prevent duplicate
         launch/start calls under concurrent spawns.
      - Cancellation/timeout rule: waiting lock acquisitions respect
         context cancellation and return retryable contention errors.

4. AMI promotion visibility in first cut:
      - Use controller logs only (no new API fields and no UI progress
         surface in first cut).
      - Emit structured stage logs for promotion lifecycle:
         `promotion_started`, `image_create_requested`, `image_pending`,
         `image_available`, `state_pointer_updated`,
         `old_images_cleanup_started`, `old_images_cleanup_done`,
         `promotion_failed`.
      - Include stable fields on each event:
         `host_id`, `operation_id`, `from_ami_id`, `to_ami_id`,
         `release_tag`, `duration_ms`, and error details on failure.

## Remaining unresolved issues

- None for first cut.

## Out of scope for first cut

- Cross-account image sharing.
- Multi-region AMI replication.
- Full image bakery pipeline automation (Packer/GitHub Actions) beyond
  the controller-driven promotion flow.
