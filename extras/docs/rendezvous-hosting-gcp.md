# Hosting `tm-rendezvous` on GCP Free Tier (e2-micro + DuckDNS)

This document describes how to host the TaskMessenger rendezvous server on
Google Cloud Platform at zero ongoing cost, using:

- A free-tier **`e2-micro`** Compute Engine VM
- The VM's **automatically-assigned ephemeral external IPv4** (free while
  attached to a free-tier VM)
- **DuckDNS** to give the VM a stable hostname even if the IP changes

The dashboard is reachable over HTTP(S) from the public internet; the actual
rendezvous service binds to the ZeroTier virtual NIC and is unreachable from
outside the ZT network.

---

## Topology

```
Public internet  ──HTTPS──▶  [Caddy :443]  ──HTTP──▶  127.0.0.1:<dashboard-port>
                                                              │
                                                              ▼
                                                  tm-rendezvous process
                                                              │
                                                              ▼ (libzt virtual NIC)
ZeroTier network  ◀── rendezvous service port (bound to ZT IP only) ──▶  dispatchers / workers
```

DNS:

```
rendezvous.duckdns.org  ──A──▶  <VM ephemeral IPv4>   (refreshed every 5 min)
```

---

## Cost summary

| Item                                            | Cost       |
| ----------------------------------------------- | ---------- |
| `e2-micro` VM in `us-west1` / `us-central1` / `us-east1`, 24/7 | **Free** (730 hours/month included) |
| 30 GB standard persistent disk                  | **Free** (free tier includes 30 GB) |
| Ephemeral IPv4 attached to free-tier VM         | **Free**   |
| 1 GB/month outbound network egress (excl. China/Australia) | **Free**   |
| DuckDNS hostname                                | **Free**   |
| Let's Encrypt TLS certificate (via Caddy)       | **Free**   |
| **Total**                                       | **$0/month** |

Watch out:
- Region matters — only the three US regions above qualify for the free `e2-micro`.
- Egress beyond 1 GB/month is billed (~$0.12/GB to most destinations). The dashboard is light, but plan accordingly.
- Stopping and starting the VM may change the ephemeral IP. DuckDNS update job (below) handles this.

---

## Prerequisites

- A Google Cloud account with billing enabled (free tier still requires a billing account on file).
- A DuckDNS account (sign in with GitHub/Google at <https://www.duckdns.org/>) and a chosen subdomain, e.g. `taskmessenger-rdv.duckdns.org`. Note the **token** shown on the DuckDNS dashboard.
- A ZeroTier network ID and the rendezvous identity files from a local install (or generate them on the VM after install).
- The release artifact: `tm-rendezvous-v<VERSION>-linux-x86_64.run` from a GitHub Release.

---

## Step-by-step setup

### 1. Create the VM

```bash
gcloud compute instances create tm-rendezvous \
  --zone=us-west1-a \
  --machine-type=e2-micro \
  --image-family=ubuntu-2404-lts-amd64 \
  --image-project=ubuntu-os-cloud \
  --boot-disk-size=30GB \
  --boot-disk-type=pd-standard \
  --tags=http-server,https-server
```

> **PowerShell users:** PowerShell mangles the comma in `--tags=a,b`. Either
> quote the whole flag (`"--tags=http-server,https-server"`) or pass `--tags`
> twice (`--tags=http-server --tags=https-server`). Use backticks `` ` `` for
> line continuation instead of `\`.

Equivalent in the Console: **Compute Engine → VM instances → Create instance**,
machine type `e2-micro`, region one of `us-west1` / `us-central1` / `us-east1`,
30 GB standard disk, network tags `http-server` and `https-server`.

> If the Console shows a "monthly estimate" that isn't $0, double-check the
> region and disk type — only the three US regions and `pd-standard` qualify.

### 2. Configure firewall

GCP's default `default-allow-http` and `default-allow-https` rules cover the
network-tag setup above. Verify with:

```bash
gcloud compute firewall-rules list --filter="name~'http'"
```

If they're missing, create them:

```bash
gcloud compute firewall-rules create allow-http \
  --network=default --action=ALLOW --rules=tcp:80 \
  --source-ranges=0.0.0.0/0 --target-tags=http-server

gcloud compute firewall-rules create allow-https \
  --network=default --action=ALLOW --rules=tcp:443 \
  --source-ranges=0.0.0.0/0 --target-tags=https-server
```

**Do not** open the rendezvous service port to `0.0.0.0/0`. The service binds
only to the ZeroTier virtual NIC; libzt traffic tunnels over outbound UDP/9993
(allowed by default egress) and never traverses the GCP firewall on inbound.

### 3. SSH in to the VM

```bash
gcloud compute ssh tm-rendezvous --zone=us-west1-a
```

ZeroTier connectivity is provided by **libzt embedded in `tm-rendezvous`** —
no system ZeroTier daemon is required, and you do **not** need to run
`zerotier-cli` on the VM. The rendezvous server joins the ZT network using
the identity files in `vn-rendezvous-identity/` and binds its service port
to its libzt-assigned IP.

The libzt-managed NIC is private to the `tm-rendezvous` process; it does not
appear in `ip a` and is not reachable from the public internet. Outbound
UDP/9993 (used by libzt to talk to ZeroTier root servers) is permitted by
GCP's default egress rules — no firewall change needed.

> If you ever want to install the system-wide ZeroTier daemon instead (e.g.
> for SSH'ing into the VM over ZT), run `curl -s https://install.zerotier.com | sudo bash`
> and `sudo zerotier-cli join <network-id>`. That path is supported but not
> required for the rendezvous server itself.

### 4. Install `tm-rendezvous`

Download the latest Linux self-extracting installer (`.run`) and execute it.
The `.run` file unpacks itself into a temp directory and invokes
`install_linux.sh rendezvous` internally.

```bash
TAG=v1.2.3   # or vtest for the rolling prerelease
curl -L -o tm-rendezvous.run \
  "https://github.com/<owner>/task-messenger/releases/download/${TAG}/tm-rendezvous-${TAG}-linux-x86_64.run"
chmod +x tm-rendezvous.run
./tm-rendezvous.run
```

> Draft releases (e.g. those produced by `workflow_dispatch`) require
> authentication. Use `gh` instead:
> ```bash
> gh release download <tag> --repo <owner>/task-messenger \
>   --pattern 'tm-rendezvous-*-linux-x86_64.run'
> ```

This places binaries under `~/.local/share/task-messenger/tm-rendezvous/` and
configs under `~/.config/task-messenger/tm-rendezvous/`.

Edit `~/.config/task-messenger/tm-rendezvous/config-rendezvous.json` to:
- Bind the rendezvous service to your ZeroTier IP (or `0.0.0.0` — libzt only
  exposes the ZT-side NIC, so this is still ZT-only in practice).
- Bind the dashboard / monitoring HTTP port to `127.0.0.1:<port>` (loopback
  only). Caddy will reverse-proxy 443 → that port. Pick any unprivileged
  port (8080 is the convention used in the Caddyfile example below); the
  exact number doesn't matter as long as the Caddyfile matches.

### 5. Run as a systemd service

Create `/etc/systemd/system/tm-rendezvous.service`:

```ini
[Unit]
Description=TaskMessenger Rendezvous Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=<your-user>
Environment=LD_LIBRARY_PATH=/home/<your-user>/.local/share/task-messenger/tm-rendezvous/lib
ExecStart=/home/<your-user>/.local/share/task-messenger/tm-rendezvous/bin/tm-rendezvous \
  -c /home/<your-user>/.config/task-messenger/tm-rendezvous/config-rendezvous.json
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now tm-rendezvous
sudo systemctl status tm-rendezvous
```

### 6. Set up DuckDNS auto-update

Pick a subdomain on the DuckDNS dashboard (e.g. `taskmessenger-rdv`) and copy
the token shown at the top of the page.

Create `/usr/local/bin/duckdns-update.sh`:

```bash
#!/bin/bash
DOMAIN="taskmessenger-rdv"
TOKEN="<your-duckdns-token>"
# Empty ip= means "use the source IP of this request" (your VM's external IP).
curl -fsS "https://www.duckdns.org/update?domains=${DOMAIN}&token=${TOKEN}&ip=" \
  >> /var/log/duckdns.log 2>&1
```

```bash
sudo chmod +x /usr/local/bin/duckdns-update.sh
sudo /usr/local/bin/duckdns-update.sh   # initial registration; should print "OK"
```

Run it every 5 minutes via systemd timer:

`/etc/systemd/system/duckdns.service`:

```ini
[Unit]
Description=DuckDNS update

[Service]
Type=oneshot
ExecStart=/usr/local/bin/duckdns-update.sh
```

`/etc/systemd/system/duckdns.timer`:

```ini
[Unit]
Description=Run DuckDNS update every 5 minutes

[Timer]
OnBootSec=1min
OnUnitActiveSec=5min
Persistent=true

[Install]
WantedBy=timers.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now duckdns.timer
```

(A simple `*/5 * * * * /usr/local/bin/duckdns-update.sh` cron entry works
equally well.)

### 7. Reverse proxy + TLS with Caddy

Install Caddy (Ubuntu):

```bash
sudo apt install -y curl gnupg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' \
  | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' \
  | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update && sudo apt install -y caddy
```

> The `debian-keyring` / `debian-archive-keyring` packages from Caddy's
> upstream instructions are Debian-only and are **not** in Ubuntu's apt
> repository — skip them. Cloudsmith's own GPG key (fetched above) is what
> actually verifies the Caddy package.

Replace `/etc/caddy/Caddyfile` with:

```caddyfile
taskmessenger-rdv.duckdns.org {
    reverse_proxy 127.0.0.1:8080
}
```

```bash
sudo systemctl reload caddy
```

Caddy will automatically request a Let's Encrypt certificate the first time
the hostname is hit (the DuckDNS A record must already point at the VM, so
do this *after* step 6 reports `OK`).

### 8. Verify

From your laptop:

```bash
curl -I https://taskmessenger-rdv.duckdns.org/
# expect HTTP/2 200 (or whatever the dashboard root returns)
```

From a ZeroTier-joined dispatcher/worker (one that **does** have a system
ZeroTier daemon, or that runs another libzt-based TaskMessenger component):

```bash
# rendezvous service should respond on its libzt ZT IP, but NOT on the public IP
nc -zv <zt-ip-of-rendezvous> <rendezvous-port>      # OK
nc -zv <public-ip-of-rendezvous> <rendezvous-port>  # should refuse / time out
```

---

## Operational notes

- **Ephemeral IP changes** happen only when the VM is **stopped and started**
  (a reboot keeps the same address). The DuckDNS timer corrects DNS within
  five minutes.
- **DNS propagation** at DuckDNS is fast (~60 s TTL), so brief outages after
  an IP change are limited by the timer interval, not DNS.
- **Outbound egress** is metered. Keep dashboard payloads small or budget for
  it (~$0.12/GB beyond the free 1 GB).
- **Monthly billing alert**: set a budget alert at $1 in **Billing → Budgets &
  alerts** as a tripwire — any unexpected charge means something fell out of
  the free tier.
- **Backups**: snapshot the boot disk on a schedule, or copy the
  `vn-rendezvous-identity/` directory off-host. Losing `identity.secret`
  means dispatchers/workers will reject the rebuilt rendezvous server.

---

## Alternative: Oracle Cloud Always Free

If you want a static public IPv4 included for free (instead of relying on
DuckDNS), **Oracle Cloud Always Free** is worth knowing about:

- Always Free includes up to **2 reserved public IPv4 addresses** at no cost
  while they're attached to running instances.
- The Ampere ARM (`VM.Standard.A1.Flex`) shape gives you up to **4 OCPUs and
  24 GB RAM** total across up to 4 VMs, free indefinitely.
- ZeroTier and the rendezvous binary build cleanly on ARM64 Linux.

Trade-offs vs. GCP free tier:

- Oracle has historically reclaimed Always Free instances during regional
  capacity pressure; GCP has not.
- The Ampere ARM shape can be hard to provision in popular regions — you may
  need to retry over several days.
- ARM64 means you'd need an ARM64 Linux build of `tm-rendezvous` (the
  current release matrix only produces `linux-x86_64`).

If a stable static IP without DDNS hassle matters more than ARM64 build work,
Oracle Always Free is a viable alternative. Otherwise, the GCP + DuckDNS
recipe above is the simpler path.
