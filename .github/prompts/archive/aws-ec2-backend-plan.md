# AWS EC2 Backend — Implementation Plan

This document records the full design and step-by-step implementation
plan for adding an EC2 backend to `tm-worker-farm`, parallel to the
existing GitHub Codespaces backend. When this work ships, archive this
file and create a present-tense `bootstrap-ec2-worker.md` hand-off
prompt at the root of [`.github/prompts/`](README.md).

---

## Goals

- Run `tm-worker` processes on a managed AWS EC2 instance with the
  same controller UI, spawn/stop lifecycle, and worker registry as the
  existing `local` and `codespace` backends.
- Avoid unnecessary charges: the instance stops automatically when
  idle; `tm-worker` persists on the root EBS volume so no re-bootstrap
  is needed after a stop/start cycle.
- All remote execution uses AWS Systems Manager (SSM) Session Manager.
  No inbound SSH port, no key distribution.

## Non-goals

- No Auto Scaling Group, no multi-instance fleet.
- No CloudWatch log integration (local mirror only, same as codespace
  backend).
- No S3 artifact staging (direct controller push via SSM).
- No SSH fallback path.

---

## Architecture

### Identity model

Each inventory host with `backend = "ec2"` maps to exactly one EC2
instance.  The controller identifies that instance by two AWS resource
tags:

| Tag key              | Value                   |
| -------------------- | ----------------------- |
| `tm-worker-farm-host`| `<host.ID>`             |
| `tm-worker-farm-ctrl`| `<controller-id>`       |

The controller resolves the instance by these tags at runtime (never
by hard-coded instance id).  On first bootstrap it creates the
instance from a Launch Template; on subsequent runs it starts the
stopped instance.  Tag-anchored identity survives stop/start cycles
and works correctly if the instance is accidentally terminated and
re-created.

### Transport

All remote execution uses the AWS SDK for Go v2:
- **SSM `SendCommand`** — used by bootstrap (run installer, chmod,
  verify binary).
- **SSM `StartSession` / `SendCommand`** — used by spawn, stop, and
  liveness poll.

No SSH; no `gh` dependency.

### Artifact delivery

Bootstrap pushes the `tm-worker` `.run` installer and the embedded
helper script directly from the controller via SSM
`SendCommand` with the script body inlined.  For the initial `.run`
payload (up to ~50 MB) the mechanism is:

1. Controller base64-encodes the asset in chunks.
2. Each chunk is appended to a remote staging file via a sequence of
   `SendCommand` calls using `printf '%s' '<chunk>' >> <file>`.
3. After all chunks land, a final `SendCommand` base64-decodes and
   verifies the SHA-256.

This keeps the delivery entirely within SSM with no S3 dependency.
If payload limits or latency become a blocker a presigned-S3 fallback
can be layered in without changing the rest of the bootstrap contract.

### Log handling

- Spawn writes a `manifest.json` to the instance's local run
  directory (same schema as the codespace backend).
- The controller mirrors the manifest over SSM after spawn.
- Worker stdout/stderr land in per-worker log files on the instance.
- The controller tails/mirrors log segments on demand via SSM
  `SendCommand`, same as the codespace log-tail endpoint.
- No CloudWatch integration in this phase.

### Cost control

| Event                        | Action                      |
| ---------------------------- | --------------------------- |
| Spawn returns; workers start | mark host `active`          |
| All workers exit             | start 15-minute idle timer  |
| Idle timer fires             | controller calls EC2 `StopInstances` |
| Bootstrap POST received      | controller starts instance if stopped |
| Explicit operator terminate  | controller calls EC2 `TerminateInstances`; clears state |

The auto-stop threshold is 15 minutes and is not configurable in the
first release (can be added later as an inventory field).

Auto-terminate is disabled by default.  When it is enabled, the next
bootstrap re-creates the instance from the Launch Template and
re-bootstraps `tm-worker`.

---

## Inventory schema

New backend discriminator and config block (extends
[`worker-farm/internal/inventory/inventory.go`](../../worker-farm/internal/inventory/inventory.go)):

```json
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
```

| Field                    | Required | Description                                                                   |
| ------------------------ | -------- | ----------------------------------------------------------------------------- |
| `region`                 | yes      | AWS region for the instance and SSM calls.                                    |
| `launch_template_id`     | yes      | Launch Template used to create the managed instance on first bootstrap.       |
| `launch_template_version`| no       | Default `$Latest`.                                                            |
| `worker_bin`             | no       | Path to `tm-worker` on the instance. Default `tm-worker` (resolved via PATH).|
| `config`                 | no       | Path to `config-worker.json` on the instance.                                 |
| `auto_terminate`         | no       | When true, terminate (not stop) the instance after all workers exit.          |

Validation rules (same error style as existing inventory errors):
- `region` must be non-empty.
- `launch_template_id` must match `^lt-[0-9a-f]+$`.
- `auto_terminate` defaults to `false` when absent.

---

## Operational setup record

This section captures the exact AWS console steps and IAM decisions
made during the first working setup. Copy this into the permanent
`bootstrap-ec2-worker.md` prompt when the work ships.

### Step 1 — Create the EC2 instance IAM role

1. Open **IAM → Roles → Create role**.
2. Trusted entity type: **AWS service**; use case: **EC2**.
3. Attach the managed policy `AmazonSSMManagedInstanceCore`.
4. Name the role (e.g. `ec2-ssm-role`). No other policies needed for
   `tm-worker` itself.

This role is what lets the SSM agent on the instance register with the
Systems Manager service. Without it the instance never appears as
`Online` in SSM and every `SendCommand` call returns
`InvalidInstanceId`.

### Step 2 — Create the Launch Template

1. Open **EC2 → Launch Templates → Create launch template**.
2. Under **Advanced details → IAM instance profile**, select the role
   created in Step 1 (`ec2-ssm-role`).
3. Set **AMI**, **instance type**, **key pair** (key pair is optional
   — SSM removes the need for it), and **security group** (no inbound
   rules required; SSM traffic is outbound only).
4. Note the **Launch Template ID** (`lt-xxxxxxxxxxxxxxxxx`); this goes
   in `hosts.json` as `launch_template_id`.
5. If you update the template later (e.g. to change the AMI), bump to
   a new version. The controller always uses `$Latest` unless
   `launch_template_version` is pinned in the inventory.

### Step 3 — Create the controller IAM user and policy

The machine running `tm-worker-farm` needs an IAM identity with
permissions to manage the instance and issue SSM commands.

1. Open **IAM → Users → Create user** (or use an existing user /
   instance role if the controller runs on AWS).
2. Attach an inline or managed policy. The policy requires **two
   statements** because `ec2:RunInstances` cannot be tag-conditioned
   at creation time:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Sid": "EC2ManageTagged",
      "Effect": "Allow",
      "Action": [
        "ec2:DescribeInstances",
        "ec2:StartInstances",
        "ec2:StopInstances",
        "ec2:TerminateInstances",
        "ec2:CreateTags",
        "ssm:SendCommand",
        "ssm:GetCommandInvocation",
        "ssm:DescribeInstanceInformation"
      ],
      "Resource": "*"
    },
    {
      "Sid": "EC2RunFromTemplate",
      "Effect": "Allow",
      "Action": "ec2:RunInstances",
      "Resource": "*"
    },
    {
      "Sid": "PassRoleToInstance",
      "Effect": "Allow",
      "Action": "iam:PassRole",
      "Resource": "arn:aws:iam::<account-id>:role/ec2-ssm-role"
    }
  ]
}
```

   The `iam:PassRole` statement is required because `RunInstances`
   with an instance profile internally calls `iam:PassRole`. Without
   it the API returns `UnauthorizedOperation` even when the rest of
   the EC2/SSM permissions are correct.

3. Generate an **access key** for the user. Supply the credentials to
   the controller via environment variables:

   ```powershell
   $env:AWS_ACCESS_KEY_ID     = "AKIA..."
   $env:AWS_SECRET_ACCESS_KEY = "..."
   $env:AWS_REGION            = "us-east-2"
   ```

   Rotate the key immediately if it is ever exposed in logs or chat.

### Step 4 — Verify the instance reaches SSM Online

After the first bootstrap (or after manually launching the instance
for testing):

1. Open **Systems Manager → Fleet Manager** (or **Session Manager →
   Managed instances**).
2. Confirm the instance appears with **SSM Agent status: Online**.
3. If it does not appear after 5 minutes:
   - Confirm the IAM role is attached to the instance (EC2 console →
     instance details → IAM role).
   - Confirm the instance has outbound internet access or a VPC
     endpoint for SSM (`com.amazonaws.<region>.ssm`,
     `com.amazonaws.<region>.ec2messages`,
     `com.amazonaws.<region>.ssmmessages`).
   - Check the SSM agent log on the instance:
     `/var/log/amazon/ssm/amazon-ssm-agent.log`.

### Step 5 — Populate hosts.json

```json
{
  "hosts": [
    {
      "id": "aws-worker-1",
      "backend": "ec2",
      "ec2": {
        "region": "us-east-2",
        "launch_template_id": "lt-xxxxxxxxxxxxxxxxx",
        "launch_template_version": "$Latest",
        "worker_bin": "tm-worker",
        "config": "~/.config/task-messenger/tm-worker/config-worker.json",
        "auto_terminate": false
      }
    }
  ]
}
```

Default path: `%APPDATA%\tm-worker-farm\hosts.json` (Windows) or
`$XDG_CONFIG_HOME/tm-worker-farm/hosts.json` (Linux/macOS).

### Known issues encountered during setup

| Symptom | Root cause | Fix |
| --- | --- | --- |
| `AuthFailure` / `InvalidClientTokenId` on every API call | `AWS_ACCESS_KEY_ID` / `AWS_SECRET_ACCESS_KEY` not set or stale | Set env vars; rotate if previously exposed |
| `UnauthorizedOperation` on `RunInstances` | Launch Template was created but `ec2:RunInstances` was missing from the user policy | Add the `EC2RunFromTemplate` statement above |
| `UnauthorizedOperation: not authorized to perform iam:PassRole` | `iam:PassRole` for the instance role ARN was absent | Add `PassRoleToInstance` statement with the exact role ARN |
| Instance reaches `running` but SSM stays offline | IAM instance profile not attached to the Launch Template | Edit the template, attach the role, and re-launch |
| `InvalidInstanceId` on `SendCommand` | SSM agent not yet online (can take 2–3 min after instance start) | `waitForSSMOnline` polls for up to 5 min; if it still fails, check agent logs on the instance |
| Bootstrap SSM upload times out for large `.run` assets | Slow network path through SSM chunked write for 50 MB+ | Increase `ssmBootstrapTotalTimeout` or use a presigned S3 URL for the asset download (future enhancement) |
| `2/2 checks initializing` in EC2 status | Normal — instance health checks take ~2 min after first boot | Wait; the controller's SSM online poll handles this transparently |

---

## IAM requirements

### Controller principal (the machine running `tm-worker-farm`)

```json
{
  "Effect": "Allow",
  "Action": [
    "ec2:DescribeInstances",
    "ec2:RunInstances",
    "ec2:StartInstances",
    "ec2:StopInstances",
    "ec2:TerminateInstances",
    "ec2:CreateTags",
    "ssm:SendCommand",
    "ssm:GetCommandInvocation",
    "ssm:StartSession",
    "ssm:DescribeInstanceInformation"
  ],
  "Resource": "*",
  "Condition": {
    "StringEquals": {
      "aws:ResourceTag/tm-worker-farm-ctrl": "<controller-id>"
    }
  }
}
```

`ec2:RunInstances` on the Launch Template ARN needs a separate
statement without the tag condition (the instance has no tags yet at
creation time).

### EC2 instance role

The instance needs only the SSM managed policy so the SSM agent can
register:

```
arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore
```

No other permissions required for `tm-worker` itself unless the
worker config calls AWS-specific skills (future concern).

---

## Package layout

```
worker-farm/internal/
  ec2/
    ec2.go          — resolve/create/start/stop instance; wait for SSM online
    bootstrap.go    — install tm-worker on the instance via SSM push
    manager.go      — spawn/stop/poll lifecycle, parallel to codespace/codespace.go
    chunker.go      — base64-chunk encoding for SSM SendCommand artifact push
  inventory/
    inventory.go    — add BackendEC2 + EC2Cfg + validation
  api/
    server.go       — dispatch status/bootstrap/spawn/stop to ec2 manager
  bootstrap/
    bootstrap.go    — (codespace only; no change needed in existing file)
```

The `ec2` package uses AWS SDK for Go v2. No shell-out to the `aws`
CLI.  Add the SDK as a Go module dependency:

```
go get github.com/aws/aws-sdk-go-v2/config
go get github.com/aws/aws-sdk-go-v2/service/ec2
go get github.com/aws/aws-sdk-go-v2/service/ssm
```

---

## Implementation steps

### 1 — Isolate codespace backend dispatch in the API layer

Before adding EC2, extract the backend-specific branches in
`server.go` so each backend's handler is a distinct method call rather
than a switch block in-line. No behavior changes; this is a pure
refactor.

Files:
- [`worker-farm/internal/api/server.go`](../../worker-farm/internal/api/server.go)
  — extract `handleHostStatusCodespace`, `handleHostBootstrapCodespace`,
  `handleSpawnCodespace`, `handleStopCodespace`.

Verify: `go build ./... ; go test ./...` unchanged.

### 2 — Extend inventory with `backend = "ec2"`

Files:
- [`worker-farm/internal/inventory/inventory.go`](../../worker-farm/internal/inventory/inventory.go)
  — add `BackendEC2`, `EC2Cfg` struct, validation.
- `worker-farm/internal/inventory/inventory_test.go` — add positive and
  negative test cases.

Verify: round-trip a hosts.json with `ec2` host; confirm validation
rejects missing `region`, bad `launch_template_id`.

### 3 — Build EC2 instance resolver (`ec2/ec2.go`)

Core function:

```go
func EnsureInstance(ctx context.Context, cfg EC2Cfg, hostID, controllerID string) (*Instance, error)
```

Sequence:

1. `ec2:DescribeInstances` filtered by tags `tm-worker-farm-host=hostID`
   and `tm-worker-farm-ctrl=controllerID`.
2. If zero results → `ec2:RunInstances` from Launch Template with tags.
3. If stopped → `ec2:StartInstances`; wait for state `running`.
4. After running → `ssm:DescribeInstanceInformation` polling until the
   SSM agent registers (up to 5 min; 10-second poll interval).
5. Return `Instance{InstanceID, Region, SSMTarget}`.

Also:

```go
func StopInstance(ctx context.Context, cfg EC2Cfg, instanceID string) error
func TerminateInstance(ctx context.Context, cfg EC2Cfg, instanceID string) error
```

### 4 — Implement EC2 status probe

Map instance+SSM state to the same status vocabulary used for codespace:

| Condition                         | `status` value          |
| --------------------------------- | ----------------------- |
| No credentials / SDK error        | `aws-auth-error`        |
| Instance not found, no template   | `instance-not-found`    |
| Instance pending / starting       | `starting`              |
| Instance running, SSM offline     | `ssm-offline`           |
| Instance running, SSM online      | `ok`                    |
| Instance stopped                  | `stopped`               |
| Instance terminated               | `instance-terminated`   |

`GET /hosts/{id}/status` response shape mirrors codespace:

```json
{
  "id": "aws-worker-1",
  "backend": "ec2",
  "status": "ok",
  "instance_id": "i-0abc123",
  "region": "us-east-1"
}
```

Files:
- `worker-farm/internal/ec2/ec2.go` — `InstanceStatus` function.
- `worker-farm/internal/api/server.go` — `handleHostStatusEC2`.

### 5 — Implement EC2 bootstrap

Parallel to `worker-farm/internal/bootstrap/bootstrap.go`.

Files:
- `worker-farm/internal/ec2/bootstrap.go`

Sequence:

1. Call `EnsureInstance` to get a running instance with SSM online.
2. Resolve release tag and asset via GitHub Releases API (reuse
   `gh.ReleaseView` / `gh.ReleaseDownload` — these talk to GitHub,
   not to AWS).
3. Push embedded helper script (`install_tm_worker_release.sh`) via
   SSM chunked write. Skip if remote hash matches (same hash-cache
   strategy as codespace bootstrap).
4. Push the `.run` asset via SSM chunked write.
5. `chmod +x` both files via SSM `SendCommand`.
6. Run `install_tm_worker_release.sh -f <asset>` via SSM
   `SendCommand`; surface `StandardOutputContent` as `installer_log`
   in response.
7. Persist bootstrap state (helper hash, asset name, tag) in
   `bootstrap-state.json` under the controller cache dir.

SSM `SendCommand` returns a `CommandId`.  Poll
`ssm:GetCommandInvocation` until status is `Success` or terminal
failure (timeout: 10 minutes for the full bootstrap, 30 seconds per
individual command).

Result type mirrors `bootstrap.Result` so the API response shape is
identical for both backends.

### 6 — Implement EC2 lifecycle manager (`ec2/manager.go`)

```go
type Manager struct { ... }
func (m *Manager) Spawn(ctx, host, count, extraArgs) []SpawnResult
func (m *Manager) Stop(ctx, workerID) error
func (m *Manager) Run(ctx) // liveness poll goroutine
```

Spawn:

1. Resolve (or start) the managed instance.
2. Upload `start_workers_local.sh` + epilogue (same embedded bytes as
   codespace manager) via SSM `SendCommand`.
3. Parse run manifest from `StandardOutputContent` (same sentinel
   markers as codespace: `===TM_FARM_RUN_DIR_BEGIN===` etc.).
4. Register workers in the shared registry; record `runState` with
   instance id, remote run dir, worker ids.

Stop:

1. Send `SIGTERM` + deferred `SIGKILL` via SSM (same shell one-liner as
   codespace stop).
2. Update registry state to `stopping`.

Poll (liveness, every 5 seconds):

1. For each active run, batch `kill -0 <pid>` via one SSM
   `SendCommand` per host.
2. Parse alive/dead list; call `registry.Update` to flip exited
   workers.
3. After all workers on an instance exit, start the 15-minute idle
   timer.
4. When idle timer fires, call `StopInstance`.

### 7 — Wire manager into `main.go` and `server.go`

Files:
- [`worker-farm/cmd/tm-worker-farm/main.go`](../../worker-farm/cmd/tm-worker-farm/main.go)
  — construct `ec2.Manager` when inventory contains an `ec2` host.
- [`worker-farm/internal/api/server.go`](../../worker-farm/internal/api/server.go)
  — add `ec2mgr` field; route `status` / `bootstrap` / `spawn` / `stop`
  to EC2 handlers when `host.Backend == inventory.BackendEC2`.

### 8 — Update operator documentation

Files:
- [`worker-farm/README.md`](../../worker-farm/README.md)
  — add `ec2` backend section: IAM setup, hosts.json example, Launch
  Template prerequisites, troubleshooting for common SSM errors.

---

## Verification

1. Unit tests for `ec2.EnsureInstance` state machine (no real AWS):
   use `smithy-go` test doubles or interface mocks for the EC2 and SSM
   clients.
2. Unit tests for `inventory` EC2 validation (positive + negative).
3. Unit tests for `chunker.go` (encode/decode round-trip; edge cases at
   chunk boundaries).
4. Integration smoke test against a dev AWS account:
   - Bootstrap an instance from scratch → `status` returns `ok`.
   - Spawn 2 workers → registry shows them `running`.
   - Stop one worker → registry flips to `exited`.
   - All workers exit → instance auto-stops after 15 minutes.
   - Second bootstrap reuses the stopped instance (no re-create).
5. Regression: `local` and `codespace` paths unaffected (`go test ./...`
   green; real codespace bootstrap smoke).

---

## Out of scope

- ASG-based fleet management.
- CloudWatch log forwarding.
- S3-staged artifact delivery (explicit fallback path only if SSM
  payload limits are hit in practice).
- SSH transport or open inbound ports.
- UI changes beyond wiring the existing host-status badge to the new
  `aws-auth-error`, `starting`, `ssm-offline`, `stopped` states.
