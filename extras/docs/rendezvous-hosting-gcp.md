# Hosting `tm-rendezvous` on GCP Free Tier

This document describes how to host the TaskMessenger rendezvous server on
Google Cloud Platform at zero ongoing cost, with deployment fully automated
via cloud-init and GitHub Actions.

- **Stack:** `e2-micro` Compute Engine VM + Caddy + DuckDNS + libzt-embedded
  rendezvous service.
- **Bootstrap:** `cloud-init` user-data installs Caddy + DuckDNS on first
  boot and creates the `tmrdv` service user.
- **Updates:** GitHub Actions deploy workflow installs/updates the
  `tm-rendezvous` binary on every release (auth via Workload Identity
  Federation — no long-lived service account keys in GitHub).

---

## TL;DR — operate the deployment

| Action | Command |
| --- | --- |
| One-time GCP IAM setup (per project) | `.\extras\scripts\setup_gcp_deployer.ps1 -Project <proj> -Owner <gh-user>` |
| Create / recreate the VM | `.\extras\scripts\create_rendezvous_vm.ps1 -DuckdnsToken <token>` |
| Deploy latest non-prerelease | `gh workflow run deploy-rendezvous.yml` |
| Deploy a prerelease (`vtest`) | `gh workflow run deploy-rendezvous.yml -f tag=vtest -f deploy_prerelease=true` |
| Watch a deploy | `gh run watch` |
| Tail rendezvous logs | `gcloud compute ssh tm-rendezvous --zone=us-west1-a --tunnel-through-iap --command "sudo journalctl -u tm-rendezvous -f"` |
| Health check | `curl -fsS https://taskmessenger-rdv.duckdns.org/healthz` |
| Destroy VM | `gcloud compute instances delete tm-rendezvous --zone=us-west1-a` |

After publishing a non-prerelease GitHub release, deployment is automatic —
the workflow runs on the `release: published` event, installs the new
`.run`, restarts the service, and verifies `/healthz`.

---

## Files in this repo

| File | Purpose |
| --- | --- |
| [`extras/scripts/setup_gcp_deployer.ps1`](../scripts/setup_gcp_deployer.ps1) | One-time per project: enables required APIs, creates `github-deployer` SA, grants IAM roles, creates Workload Identity Federation pool/provider scoped to the repo, binds `iam.serviceAccountUser` on the default compute SA. Idempotent. |
| [`extras/scripts/create_rendezvous_vm.ps1`](../scripts/create_rendezvous_vm.ps1) | Wraps `gcloud compute instances create` with the cloud-init user-data and required metadata. Only `-DuckdnsToken` is required. |
| [`extras/scripts/cloud-init-rendezvous.yaml`](../scripts/cloud-init-rendezvous.yaml) | First-boot bootstrap: creates `tmrdv` user, installs Caddy + DuckDNS timer, drops the `tm-rendezvous.service` unit. Optionally pre-installs a release if `rendezvous-tag` metadata is provided. |
| [`.github/workflows/deploy-rendezvous.yml`](../../.github/workflows/deploy-rendezvous.yml) | Deploy workflow. Triggered by `release: published` (non-prereleases only) or `workflow_dispatch`. WIF-auth → IAP-tunneled SSH → installer → restart → healthcheck. |

The ZeroTier identity files (`vn-rendezvous-identity/`) and
`config-rendezvous.json` are produced by the `tm-rendezvous` installer
itself — neither cloud-init nor the workflow manages them.

---

## Architecture

```
Public internet  ──HTTPS──▶  [Caddy :443]  ──HTTP──▶  127.0.0.1:8080
                                                              │
                                                              ▼
                                                  tm-rendezvous process
                                                              │
                                                              ▼ (libzt virtual NIC)
ZeroTier network  ◀── rendezvous service port (bound to ZT IP only) ──▶  dispatchers / workers
```

- DuckDNS A record `taskmessenger-rdv.duckdns.org` → VM ephemeral IPv4
  (refreshed every 5 min by a systemd timer on the VM).
- Caddy auto-provisions a Let's Encrypt certificate for the DuckDNS hostname
  the first time it's hit.
- The dashboard listens on `127.0.0.1:8080` (loopback only); only Caddy can
  reach it from outside.
- The rendezvous service binds to the libzt virtual NIC and is
  unreachable from the public internet — even if 8080 were exposed,
  dispatchers/workers must be ZeroTier members to talk to it.
- libzt joins the ZT network using the identity in
  `~tmrdv/.config/task-messenger/tm-rendezvous/vn-rendezvous-identity/`
  and connects outbound to ZeroTier root servers over UDP/9993 (allowed
  by GCP's default egress rules).

### Cost summary

| Item | Cost |
| --- | --- |
| `e2-micro` VM in `us-west1` / `us-central1` / `us-east1`, 24/7 | **Free** (730 h/mo included) |
| 30 GB standard persistent disk | **Free** (free tier covers 30 GB) |
| Ephemeral IPv4 attached to a free-tier VM | **Free** |
| 1 GB/month outbound network egress | **Free** |
| DuckDNS hostname + Let's Encrypt cert | **Free** |
| **Total** | **$0/mo** |

Watch out for: regions other than the three US ones above; egress beyond
1 GB/mo (~$0.12/GB); SSDs (`pd-ssd` is not free).

### Auth model

```
GitHub Actions runner ──OIDC token──▶ GCP STS
                                        │
                                        ▼
                          Workload Identity Federation pool/provider
                                        │
                                        ▼ (scoped to OWNER/task-messenger)
                          Impersonates github-deployer@<proj>.iam.gserviceaccount.com
                                        │
                                        ▼
                          IAP-tunneled SSH into tm-rendezvous VM
```

No long-lived secrets are stored in GitHub. The provider's
`attribute-condition` restricts impersonation to the configured repository.

---

## First-time setup (per project)

### 1. One-time GCP IAM setup

```powershell
.\extras\scripts\setup_gcp_deployer.ps1 `
    -Project task-messenger-prod -Owner <github-user>
```

The script:

1. Enables `iamcredentials`, `iam`, `compute`, `sts` APIs.
2. Creates the `github-deployer` service account.
3. Grants `roles/compute.instanceAdmin.v1`, `roles/iap.tunnelResourceAccessor`,
   `roles/compute.osLogin` at the project level.
4. Grants `roles/iam.serviceAccountUser` on the default compute SA (required
   to push SSH keys into instance metadata).
5. Creates the `github` Workload Identity pool + OIDC provider scoped to
   `<owner>/task-messenger`.
6. Binds `roles/iam.workloadIdentityUser` so the GitHub repo can impersonate
   the SA.
7. Prints the values to paste into GitHub repo Variables.

### 2. GitHub repo variables

Repo → **Settings → Secrets and variables → Actions → Variables**:

| Variable | Example |
| --- | --- |
| `GCP_PROJECT` | `task-messenger-prod` |
| `GCP_ZONE` | `us-west1-a` |
| `GCP_VM_NAME` | `tm-rendezvous` |
| `GCP_WIF_PROVIDER` | (printed by script) |
| `GCP_DEPLOY_SA` | `github-deployer@task-messenger-prod.iam.gserviceaccount.com` |
| `RENDEZVOUS_HEALTHCHECK_URL` | `https://taskmessenger-rdv.duckdns.org/healthz` |

No secrets needed — WIF is keyless.

### 3. Allow IAP SSH (one-time)

```bash
gcloud compute firewall-rules create allow-iap-ssh \
  --direction=INGRESS --action=ALLOW --rules=tcp:22 \
  --source-ranges=35.235.240.0/20
```

### 4. Create the VM

```powershell
.\extras\scripts\create_rendezvous_vm.ps1 -DuckdnsToken <token>
```

Defaults match this doc (`task-messenger-prod`, `us-west1-a`,
`tm-rendezvous`, repo `ilya-yusim/task-messenger`, domain
`taskmessenger-rdv`, dashboard port `8080`). Override with named
parameters; see the script header.

cloud-init runs in ~3–5 minutes:

- Creates the `tmrdv` system user (homedir `/var/lib/tm-rendezvous`).
- Installs Caddy from Cloudsmith's apt repo and writes a Caddyfile that
  reverse-proxies the DuckDNS host to `127.0.0.1:8080`.
- Installs a DuckDNS systemd timer (every 5 min) and runs it once
  immediately so Caddy can fetch a cert.
- Drops `tm-rendezvous.service` (hardened: `NoNewPrivileges`,
  `ProtectSystem=strict`, `ReadWritePaths=/var/lib/tm-rendezvous`).
- If `-Tag` was supplied, downloads and runs that release's `.run`
  installer as `tmrdv` and starts the service. Otherwise leaves
  `tm-rendezvous` un-installed for the GH Actions workflow to handle.

Watch progress:

```powershell
gcloud compute ssh tm-rendezvous --zone=us-west1-a --tunnel-through-iap `
  --command "sudo tail -f /var/log/cloud-init-output.log"
```

### 5. First deploy

Trigger the workflow manually:

```powershell
gh workflow run deploy-rendezvous.yml -f tag=vtest -f deploy_prerelease=true
gh run watch
```

(or publish a non-prerelease release on GitHub).

### 6. Authorize the libzt node in ZeroTier Central

After the service starts, find its node ID:

```powershell
gcloud compute ssh tm-rendezvous --zone=us-west1-a --tunnel-through-iap `
  --command "sudo journalctl -u tm-rendezvous -n 30 --no-pager | grep 'ZeroTier node online'"
```

Authorize that ID in [ZeroTier Central](https://my.zerotier.com/) for the
network. Confirm:

```powershell
curl -fsS https://taskmessenger-rdv.duckdns.org/healthz
```

---

## Operations

### Update behavior

| Trigger | `deploy_prerelease` | Tag is prerelease? | Deploys? |
| --- | --- | --- | --- |
| `release: published` | n/a | no | ✅ |
| `release: published` | n/a | yes (e.g. `vtest`) | ❌ skipped |
| `workflow_dispatch` (no input) | false | latest non-prerelease | ✅ |
| `workflow_dispatch` (tag=`vtest`) | false | yes | ❌ skipped |
| `workflow_dispatch` (tag=`vtest`) | **true** | yes | ✅ |
| `workflow_dispatch` (tag=`v1.2.3`) | false | no | ✅ |

Prereleases are **never** deployed automatically. To push a prerelease, run
the workflow manually with `deploy_prerelease=true`.

### Common tasks

```powershell
# Status of the three services
gcloud compute ssh tm-rendezvous --zone=us-west1-a --tunnel-through-iap `
  --command "sudo systemctl is-active tm-rendezvous caddy duckdns.timer"

# Tail rendezvous logs
gcloud compute ssh tm-rendezvous --zone=us-west1-a --tunnel-through-iap `
  --command "sudo journalctl -u tm-rendezvous -f"

# Force a DuckDNS update
gcloud compute ssh tm-rendezvous --zone=us-west1-a --tunnel-through-iap `
  --command "sudo systemctl start duckdns.service && sudo cat /var/log/duckdns.log"
```

### Identity & config

The libzt identity and `config-rendezvous.json` live under
`/var/lib/tm-rendezvous/.config/task-messenger/tm-rendezvous/`. **Both are
owned by the running tm-rendezvous installer**, not by cloud-init or the
workflow. To preserve identity across a VM rebuild, either:

- Copy that directory off the VM before destroying it, then drop it back
  before the next deploy runs, **or**
- Skip preservation and authorize the new node ID in ZeroTier Central
  after the rebuild.

### Backups

Snapshot the boot disk, or just back up
`/var/lib/tm-rendezvous/.config/task-messenger/tm-rendezvous/`. Losing
`identity.secret` means dispatchers/workers will reject the rebuilt
rendezvous server.

### Billing tripwire

Set a $1 budget alert in **Billing → Budgets & alerts**. Any unexpected
charge means something fell out of the free tier.

---

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| Workflow: *"IAM Service Account Credentials API has not been used…"* | Re-run `setup_gcp_deployer.ps1` (it enables the API), or `gcloud services enable iamcredentials.googleapis.com --project=<proj>`. |
| Workflow: *"User does not have access to service account 'N-compute@developer.gserviceaccount.com'"* | The deployer SA needs `iam.serviceAccountUser` on the VM's compute SA. Re-run `setup_gcp_deployer.ps1`. |
| Installer hangs on license prompt | The `.run` is invoked without `--accept`; the deploy workflow and cloud-init both pass it. If you ran the installer manually, re-run with `./tm-rendezvous-*.run --accept`. |
| `mkdir: cannot create directory '/home/tmrdv'` during install | `tmrdv` was created with the wrong homedir. Fix: `sudo usermod -d /var/lib/tm-rendezvous tmrdv`. cloud-init now uses `homedir:` (not `home:`) so new VMs are correct. |
| Healthcheck `502` from Caddy | tm-rendezvous isn't listening on the port Caddy proxies to. Check `sudo ss -tlnp` and the dashboard port in `config-rendezvous.json` vs. `/etc/caddy/Caddyfile`. Default is 8080 on both sides. |
| `cd: <tmpdir>: Permission denied` running installer over SSH | `mktemp -d` gave 0700 to the SSH user; `sudo -u tmrdv` then can't enter it. Workflow does `chmod 0755` after `mktemp -d`. |
| New ephemeral IP after VM stop/start | DuckDNS timer corrects it within 5 min; cert is preserved. |

---

## Manual reference

If you need to do this without the helper scripts (debugging, different
cloud, different repo layout, etc.), the equivalent raw commands are below.

<details>
<summary><strong>Raw <code>gcloud</code> for one-time IAM setup</strong></summary>

```bash
PROJECT=task-messenger-prod
OWNER=<github-user>
SA=github-deployer@${PROJECT}.iam.gserviceaccount.com

# Required APIs
gcloud services enable iamcredentials.googleapis.com iam.googleapis.com \
  compute.googleapis.com sts.googleapis.com --project=$PROJECT

# Service account + project-level roles
gcloud iam service-accounts create github-deployer \
  --display-name="GitHub Actions deployer"
for role in roles/compute.instanceAdmin.v1 roles/iap.tunnelResourceAccessor \
            roles/compute.osLogin; do
  gcloud projects add-iam-policy-binding $PROJECT \
    --member="serviceAccount:$SA" --role="$role" --condition=None
done

# Allow deployer to "act as" the VM's default compute SA (for SSH metadata)
PROJECT_NUM=$(gcloud projects describe $PROJECT --format='value(projectNumber)')
COMPUTE_SA="${PROJECT_NUM}-compute@developer.gserviceaccount.com"
gcloud iam service-accounts add-iam-policy-binding $COMPUTE_SA \
  --member="serviceAccount:$SA" --role=roles/iam.serviceAccountUser \
  --project=$PROJECT

# WIF pool + provider scoped to this repo
gcloud iam workload-identity-pools create github \
  --location=global --display-name="GitHub Actions"
gcloud iam workload-identity-pools providers create-oidc github \
  --location=global --workload-identity-pool=github \
  --display-name="GitHub" \
  --issuer-uri="https://token.actions.githubusercontent.com" \
  --attribute-mapping="google.subject=assertion.sub,attribute.repository=assertion.repository" \
  --attribute-condition="assertion.repository == '${OWNER}/task-messenger'"

gcloud iam service-accounts add-iam-policy-binding $SA \
  --role=roles/iam.workloadIdentityUser \
  --member="principalSet://iam.googleapis.com/projects/${PROJECT_NUM}/locations/global/workloadIdentityPools/github/attribute.repository/${OWNER}/task-messenger"

gcloud compute firewall-rules create allow-iap-ssh \
  --direction=INGRESS --action=ALLOW --rules=tcp:22 \
  --source-ranges=35.235.240.0/20
```
</details>

<details>
<summary><strong>Raw <code>gcloud</code> for VM creation with cloud-init</strong></summary>

```bash
gcloud compute instances create tm-rendezvous \
  --zone=us-west1-a \
  --machine-type=e2-micro \
  --image-family=ubuntu-2404-lts-amd64 \
  --image-project=ubuntu-os-cloud \
  --boot-disk-size=30GB \
  --boot-disk-type=pd-standard \
  --tags=http-server,https-server \
  --metadata-from-file=user-data=extras/scripts/cloud-init-rendezvous.yaml \
  --metadata=duckdns-domain=taskmessenger-rdv,duckdns-token=YOUR_DUCKDNS_TOKEN,rendezvous-repo=OWNER/task-messenger,rendezvous-dashboard-port=8080
```

Add `,rendezvous-tag=v1.2.3` to bake a release into first boot.
</details>

<details>
<summary><strong>Fully manual install on a hand-provisioned VM</strong></summary>

The earlier (pre-automation) recipe still works if you want to provision
everything by hand. Summary:

1. Create an `e2-micro` Ubuntu 24.04 VM in one of `us-west1`, `us-central1`,
   `us-east1` with a 30 GB `pd-standard` disk and tags `http-server,https-server`.
2. SSH in. Download the `.run` installer from a release and execute it
   (`./tm-rendezvous-*.run --accept`). Files land under
   `~/.local/share/task-messenger/tm-rendezvous/` and
   `~/.config/task-messenger/tm-rendezvous/`.
3. Install Caddy from the Cloudsmith apt repo (the `debian-keyring` packages
   from Caddy's upstream instructions are Debian-only — skip them on Ubuntu).
   Caddyfile: `taskmessenger-rdv.duckdns.org { reverse_proxy 127.0.0.1:8080 }`.
4. Drop a `duckdns-update.sh` script + `duckdns.service` + `duckdns.timer`
   that POSTs to `https://www.duckdns.org/update?domains=…&token=…&ip=`
   every 5 minutes. The empty `ip=` parameter tells DuckDNS to use the
   request source IP.
5. Drop a `tm-rendezvous.service` systemd unit pointing at the installed
   binary and config. Enable and start it.
6. Authorize the libzt node ID in ZeroTier Central.

This is what cloud-init automates. Use the cloud-init YAML as a reference
if you ever need to redo it manually.
</details>

---

## Alternative: Oracle Cloud Always Free

If a static public IPv4 (no DDNS hassle) matters more than ARM64 build
work, **Oracle Cloud Always Free** is worth knowing about:

- Up to **2 reserved public IPv4 addresses** at no cost while attached to
  running instances.
- The Ampere ARM (`VM.Standard.A1.Flex`) shape gives up to **4 OCPUs / 24
  GB RAM** total across up to 4 VMs, free indefinitely.

Trade-offs vs. GCP free tier:

- Oracle has historically reclaimed Always Free instances during regional
  capacity pressure; GCP has not.
- The Ampere ARM shape can be hard to provision in popular regions.
- ARM64 means you need an ARM64 Linux build of `tm-rendezvous` (the
  current release matrix only produces `linux-x86_64`).

For most workloads the GCP + DuckDNS recipe above is the simpler path.
