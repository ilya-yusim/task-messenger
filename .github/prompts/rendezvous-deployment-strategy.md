# Rendezvous deployment strategy (handoff brief)

This file describes how `tm-rendezvous` is hosted, deployed, and updated, so
another agent can take over operational tasks without re-discovering the
design.

## What is being deployed

`tm-rendezvous` is the TaskMessenger service that brokers connections
between dispatchers and workers over a ZeroTier network. It also serves a
read-only HTTP dashboard. It runs as a single Linux x86_64 binary.
Connectivity to ZeroTier is provided by **libzt embedded in the binary** —
no system-wide ZeroTier daemon is required on the host.

## Where it runs

A single Google Cloud `e2-micro` Compute Engine VM in one of the three
free-tier US regions (project default: `task-messenger-prod` /
`us-west1-a`). The VM is provisioned via cloud-init and has:

- `tmrdv` system user, homedir `/var/lib/tm-rendezvous`, no login shell,
  no sudo.
- `tm-rendezvous.service` systemd unit (hardened: `NoNewPrivileges`,
  `ProtectSystem=strict`, `ReadWritePaths=/var/lib/tm-rendezvous`,
  `PrivateTmp=true`).
- Caddy v2 reverse-proxying `https://<duckdns-host>` to
  `127.0.0.1:8080`, with an auto-renewing Let's Encrypt cert.
- A DuckDNS systemd timer refreshing the A record every 5 minutes.

The dashboard binds to `127.0.0.1:8080`. The rendezvous service binds to
the libzt virtual NIC and is unreachable from the public internet.

## Repository layout

- `extras/scripts/setup_gcp_deployer.ps1` — one-time per project: enables
  required APIs, creates `github-deployer` SA, grants IAM roles
  (`compute.instanceAdmin.v1`, `iap.tunnelResourceAccessor`,
  `compute.osLogin`, plus `iam.serviceAccountUser` on the default compute
  SA), creates a Workload Identity Federation pool/provider scoped to
  `<owner>/task-messenger`, binds `roles/iam.workloadIdentityUser`. Idempotent.
- `extras/scripts/create_rendezvous_vm.ps1` — wraps `gcloud compute
  instances create` with the cloud-init user-data and required metadata.
  Only `-DuckdnsToken` is required; everything else has sensible defaults.
- `extras/scripts/cloud-init-rendezvous.yaml` — first-boot bootstrap.
  Creates `tmrdv`, installs Caddy + DuckDNS timer, drops the systemd
  unit. If `rendezvous-tag` metadata is supplied, also runs the `.run`
  installer for that release. Otherwise leaves the binary install to the
  GH Actions workflow.
- `.github/workflows/deploy-rendezvous.yml` — deploy workflow. Two jobs:
  - `resolve` decides which tag to deploy and whether prereleases are
    allowed; sets `should_run` output.
  - `deploy` (gated by `should_run`) authenticates via WIF, sets up
    gcloud, verifies the release asset exists, IAP-tunnels SSH into the
    VM, downloads the `.run`, runs it as `tmrdv` with `--accept`,
    restarts the service, runs a 6×5s healthcheck loop on
    `RENDEZVOUS_HEALTHCHECK_URL`.
- `extras/docs/rendezvous-hosting-gcp.md` — operator-facing documentation
  (TL;DR table at the top, drilling into details below).

## Auth model

GitHub Actions → GCP via **Workload Identity Federation** (no long-lived
keys in the repo). The OIDC provider's `attribute-condition` restricts
impersonation to the configured repository:

```
assertion.repository == 'OWNER/task-messenger'
```

The deploy workflow uses `google-github-actions/auth@v2` with
`workload_identity_provider` + `service_account` from repo Variables.

## GitHub repo Variables (required by the workflow)

| Name | Purpose |
| --- | --- |
| `GCP_PROJECT` | Target GCP project ID |
| `GCP_ZONE` | VM zone (`us-west1-a`) |
| `GCP_VM_NAME` | VM name (`tm-rendezvous`) |
| `GCP_WIF_PROVIDER` | Full WIF provider resource path (printed by setup script) |
| `GCP_DEPLOY_SA` | `github-deployer@<project>.iam.gserviceaccount.com` |
| `RENDEZVOUS_HEALTHCHECK_URL` | `https://<duckdns-host>/healthz` |

No GitHub secrets are required.

## Update behavior

| Trigger | Tag is prerelease | `deploy_prerelease` input | Deploys |
| --- | --- | --- | --- |
| `release: published` | no | n/a | yes |
| `release: published` | yes | n/a | no (skipped) |
| `workflow_dispatch` (no tag input) | latest non-prerelease | false | yes |
| `workflow_dispatch` (tag set) | yes | false | no (skipped) |
| `workflow_dispatch` (tag set) | yes | true | yes |
| `workflow_dispatch` (tag set) | no | n/a | yes |

Prereleases are never deployed automatically. To deploy a prerelease (e.g.
`vtest`), trigger the workflow manually with `deploy_prerelease=true`.

## State preserved across deploys

The libzt identity files and `config-rendezvous.json` live under
`/var/lib/tm-rendezvous/.config/task-messenger/tm-rendezvous/` and are
**owned by the running tm-rendezvous installer**. The deploy workflow runs
the installer over the existing tree, which leaves identity and config
files in place. cloud-init does not touch them.

If the VM is destroyed and recreated, the new VM gets a fresh libzt node
ID and must be re-authorized in ZeroTier Central — unless the operator
copies `/var/lib/tm-rendezvous/.config/task-messenger/tm-rendezvous/` off
the old VM and back onto the new one before the first deploy runs.

## Common operational commands

```powershell
# Create / recreate the VM
.\extras\scripts\create_rendezvous_vm.ps1 -DuckdnsToken <token>

# Deploy latest non-prerelease
gh workflow run deploy-rendezvous.yml

# Deploy a specific (pre)release
gh workflow run deploy-rendezvous.yml -f tag=<tag> -f deploy_prerelease=true

# Watch a deploy
gh run watch

# Tail rendezvous logs
gcloud compute ssh tm-rendezvous --zone=us-west1-a --tunnel-through-iap `
  --command "sudo journalctl -u tm-rendezvous -f"

# Healthcheck
curl -fsS https://taskmessenger-rdv.duckdns.org/healthz
```

## Known pitfalls (already mitigated, do not regress)

- **PowerShell 5.1 + native commands.** `setup_gcp_deployer.ps1` uses
  `gcloud.cmd` (not `gcloud.ps1`) because the `.ps1` wrapper mangles
  splatted args. Helpers use `[CmdletBinding(PositionalBinding=$false)]`
  so `ValueFromRemainingArguments` doesn't get bound positionally to
  `[int]` parameters. `Test-GcloudSuccess` temporarily sets
  `$ErrorActionPreference='Continue'` so `describe` failures (NOT_FOUND
  on stderr) don't trigger `NativeCommandError`.
- **PowerShell array-arg handling.** `gcloud compute instances create`
  values like `--tags=http-server,https-server` must be quoted as a
  single string in PS, otherwise PS splits on the comma into an array
  and passes the values back as `http-server https-server` (invalid for
  GCP).
- **cloud-init user homedir field.** Use `homedir:` (the cloud-init
  schema), **not** `home:`. The latter is silently ignored and the
  user gets the default `/home/<name>`, which then breaks the
  installer's `mkdir` call.
- **mktemp + sudo.** `mktemp -d` creates a 0700 directory; the workflow
  `chmod 0755`s it before `sudo -u tmrdv` enters it. Don't drop that.
- **makeself license prompt.** `.run` installers built with `makeself
  --license …` block on a `Do you accept?` prompt unless invoked with
  `--accept`. Both cloud-init and the workflow pass it.
- **Dashboard port mismatch.** Caddy proxies to `127.0.0.1:8080` (the
  current default). `RendezvousOptions.cpp` and `config/config-rendezvous.json`
  default to 8080. If you change one, change all three (Caddyfile in
  cloud-init, the option default, and the shipped config) together.
- **Required GCP APIs.** `iamcredentials.googleapis.com` must be enabled
  for WIF impersonation. `setup_gcp_deployer.ps1` enables it; if you
  ever set up WIF manually, don't forget it.
- **`iam.serviceAccountUser` on the compute SA.** `gcloud compute ssh`
  needs to push SSH keys to instance metadata, which requires
  `actAs` permission on the SA attached to the VM (the default compute
  SA unless the VM was created with `--service-account=<other>`).
  `setup_gcp_deployer.ps1` grants this.

## Changing this design — guardrails

- Keep WIF as the auth path. Do not introduce JSON service account keys
  in the repo or in GitHub Secrets.
- Keep the dashboard on loopback. Public exposure of the dashboard or the
  rendezvous service (other than via Caddy on 443) is out of scope for
  this deployment and would require a security review.
- Keep the deployment idempotent. cloud-init bootstrap and
  `setup_gcp_deployer.ps1` must remain safe to re-run. The deploy workflow
  must be safe to re-run on the same tag.
- Keep the free-tier shape. Switching off `e2-micro`, `pd-standard`, or
  the three US regions breaks the cost model.

## Reference docs

- Operator guide: [`extras/docs/rendezvous-hosting-gcp.md`](../../extras/docs/rendezvous-hosting-gcp.md)
- Deploy workflow: [`.github/workflows/deploy-rendezvous.yml`](../workflows/deploy-rendezvous.yml)
- cloud-init: [`extras/scripts/cloud-init-rendezvous.yaml`](../../extras/scripts/cloud-init-rendezvous.yaml)
- Setup script: [`extras/scripts/setup_gcp_deployer.ps1`](../../extras/scripts/setup_gcp_deployer.ps1)
- VM-create script: [`extras/scripts/create_rendezvous_vm.ps1`](../../extras/scripts/create_rendezvous_vm.ps1)
