# GCP Manager Deployment Plan

Deploy the task-messenger manager as a continuously-running service on a GCP e2-micro VM (free tier), using ZeroTier for secure worker connectivity, with automatic task generation (refill when pool < 10 tasks, generate 100 tasks per batch), graceful shutdown handling, minimal logging (errors, refill events, worker connections only), and GCP Cloud Logging for web-based monitoring.

## Deployment Steps

### 1. Modify Manager for Production Mode ✅ COMPLETED

**Implementation Summary:**
- ✅ Replaced interactive loop with autonomous monitoring thread (1-second polling interval)
- ✅ Auto-refill logic: generates 100 tasks when pool size < 10
- ✅ Signal handlers: SIGTERM/SIGINT set atomic flag for graceful shutdown
- ✅ Initial startup: generates 100 tasks immediately
- ✅ Minimal logging: Info level for refill events, errors, and connection state only
- ✅ Thread-safe: uses atomic boolean flag and thread-safe `get_task_pool_stats()` API

**Files Modified:**
- [managerMain.cpp](../../manager/managerMain.cpp): Production mode implementation (167 → 114 lines)
- [AsyncTransportServer.hpp](../../manager/transport/AsyncTransportServer.hpp): Added `get_task_pool_stats()` method
- [AsyncTransportServer.cpp](../../manager/transport/AsyncTransportServer.cpp): Implemented stats delegation

**Build Status:** ✅ Compiled successfully with no errors

### 2. GCP Project Setup ✅ COMPLETED

**Prerequisites:**
- Google Account (Gmail or Google Workspace)
- Credit/debit card for billing verification (free tier won't charge if within limits)
- gcloud CLI installed locally (optional but recommended)

**Steps:**

1. **Create or select a GCP project:**
   - Console: [console.cloud.google.com/projectcreate](https://console.cloud.google.com/projectcreate)
   - CLI: `gcloud projects create task-messenger-prod --name="Task Messenger Production"`

2. **Link billing account:**
   - Console: [console.cloud.google.com/billing](https://console.cloud.google.com/billing)
   - Select project → Link a billing account
   - Required even for free tier (no charges if within limits)

3. **Set active project:**
   ```bash
   gcloud config set project task-messenger-prod
   ```

4. **Enable required APIs:**
   ```bash
   gcloud services enable compute.googleapis.com
   gcloud services enable logging.googleapis.com
   ```

5. **Verify free tier eligibility:**
   - Check project is in a free-tier eligible region: us-west1, us-central1, or us-east1
   - Confirm billing account has free tier credits available
   - Review limits: [cloud.google.com/free](https://cloud.google.com/free)

**Completion Status:**
- ✅ Project created: `task-messenger-prod`
- ✅ Project set as active
- ✅ Compute Engine API enabled
- ✅ Cloud Logging API enabled

**Notes:**
- No organization required for individual/small deployments
- Organization only needed for corporate multi-project management
- Free tier includes: 1 e2-micro VM, 30GB storage, 50GB logs/month

### 3. Create GCP e2-micro VM Instance ✅ COMPLETED

**Instance Configuration:**
- **Machine type**: e2-micro (2 vCPUs shared, 1 GB RAM)
- **Region**: US free-tier eligible (us-west1, us-central1, or us-east1)
- **OS**: Ubuntu 24.04 LTS
- **Boot disk**: 30 GB standard persistent disk (free tier)

**Firewall Rules:**
- Outbound HTTPS (443): Allow - for ZeroTier control plane
- Outbound UDP 9993: Allow - for ZeroTier traffic
- Inbound port 8080: NOT needed (manager listens on ZeroTier network only)

**Create VM Instance:**

```bash
# Set variables
PROJECT_ID="task-messenger-prod"
VM_NAME="tm-manager-prod"
ZONE="us-west1-a"  # or us-central1-a, us-east1-b

# Create e2-micro instance
gcloud compute instances create $VM_NAME \
  --project=$PROJECT_ID \
  --zone=$ZONE \
  --machine-type=e2-micro \
  --image-family=ubuntu-2404-lts-amd64 \
  --image-project=ubuntu-os-cloud \
  --boot-disk-size=30GB \
  --boot-disk-type=pd-standard
```

**Configure SSH Access:**

```bash
# SSH to instance
gcloud compute ssh $VM_NAME --zone=$ZONE

# Or add SSH key for direct access
gcloud compute os-login ssh-keys add \
  --key-file=~/.ssh/id_rsa.pub
```

**Completion Status:**
- ✅ VM instance created: `tm-manager-prod`
- ✅ Zone: `us-west1-a` (free tier eligible)
- ✅ Internal IP: `10.138.0.2`
- ✅ External IP: `34.83.119.164`
- ✅ Status: `RUNNING`

**Notes:**
- No network tags needed - default VPC firewall allows all egress (HTTPS and UDP 9993)
- No ZeroTier system package required - manager uses libzt (ZeroTier SDK) bundled in distribution
- Manager listens only on ZeroTier network, not public internet (no inbound firewall rules needed)

### 4. Install Google Cloud Ops Agent ✅ COMPLETED

On the VM, install Google Cloud Ops Agent to forward journald logs to GCP:

```bash
curl -sSO https://dl.google.com/cloudagents/add-google-cloud-ops-agent-repo.sh
sudo bash add-google-cloud-ops-agent-repo.sh --also-install
```

**Access logs:**
- Web: `console.cloud.google.com/logs`
- Query filter: `resource.type="gce_instance" AND jsonPayload.SYSLOG_IDENTIFIER="tm-manager"`
- Local: `journalctl --user -u task-messenger-manager -f` (still works via SSH)

**Completion Status:**
- ✅ Ops Agent installed successfully
- ✅ Logging and monitoring agents enabled
- ✅ Logs forwarding to Cloud Logging

**Notes:**
- Ops Agent replaces the legacy Cloud Logging agent
- Supports Ubuntu 24.04 (noble)
- Includes both logging and monitoring capabilities
- Free tier: 50 GB logs/month

### 5. Download and Install Manager ✅ COMPLETED

**Download release from GitHub (on the VM):**

```bash
# Download the pre-built installer
wget https://github.com/ilya-yusim/task-messenger/releases/download/vtest/tm-manager-vtest-linux-x86_64.run

# Make it executable
chmod +x tm-manager-vtest-linux-x86_64.run

# Run the installer
./tm-manager-vtest-linux-x86_64.run
```

**Alternative: Build locally and upload**

If you prefer to build from source:

```bash
# On local development machine
cd ~/projects/task-messenger
./extras/scripts/build_distribution.sh manager

# Upload to VM
gcloud compute scp task-messenger-manager-*.run tm-manager-prod:~/ --zone=us-west1-a
```

**Completion Status:**
- ✅ Manager installer downloaded from GitHub
- ✅ Installation completed successfully
- ✅ Binaries installed to `~/.local/share/task-messenger/tm-manager/`
- ✅ Configuration created at `~/.config/task-messenger/tm-manager/`

**Installation paths (user install):**
- Binaries: `~/.local/share/task-messenger/tm-manager/bin/tm-manager`
- Libraries: `~/.local/share/task-messenger/tm-manager/lib/libzt.so`
- Config: `~/.config/task-messenger/tm-manager/config-manager.json`
- Identity: `~/.config/task-messenger/tm-manager/vn-manager-identity/`

### 6. Configure Manager ✅ COMPLETED

**Configure ZeroTier network:**

Edit `~/.config/task-messenger/tm-manager/config-manager.json`:
```json
{
  "transport_server": {
    "listen_host": "0.0.0.0",
    "listen_port": 8080,
    "io_threads": 1
  },
  "zerotier": {
    "default_network": "YOUR_16_CHAR_NETWORK_ID",
    "identity_path": "vn-manager-identity"
  }
}
```

**Completion Status:**
- ✅ Configuration file updated with ZeroTier network ID
- ✅ Manager ready for ZeroTier connectivity

### 7. Join ZeroTier Network ✅ COMPLETED

**Option A: Use existing identity (recommended)**

Copy your existing manager identity to the VM:

```bash
# On local machine
gcloud compute scp --recurse config/vn-manager-identity/ your-vm-name:~/.config/task-messenger/tm-manager/ --zone=us-west1-a
```

**Option B: Generate new identity**

The manager will auto-generate identity files on first run at:
`~/.config/task-messenger/tm-manager/vn-manager-identity/`

**Authorize in ZeroTier Central:**
1. Start manager (generates node ID)
2. Log in to my.zerotier.com
3. Go to your network
4. Find the new node ID in "Members"
5. Check "Authorized"
6. Note the assigned ZeroTier IP (e.g., 10.147.x.x)

Workers will connect to this ZeroTier IP on port 8080.

**Completion Status:**
- ✅ ZeroTier identity configured
- ✅ Manager authorized in ZeroTier Central
- ✅ ZeroTier IP assigned

### 8. Create Systemd User Service

Since this is a user install, create a systemd user service:

```bash
mkdir -p ~/.config/systemd/user
nano ~/.config/systemd/user/task-messenger-manager.service
```

**Service file content:**
```ini
[Unit]
Description=Task Messenger Manager
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=%h/.local/share/task-messenger/tm-manager
Environment="LD_LIBRARY_PATH=%h/.local/share/task-messenger/tm-manager/lib"
ExecStart=%h/.local/share/task-messenger/tm-manager/bin/tm-manager -c %h/.config/task-messenger/tm-manager/config-manager.json
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=default.target
```

**Enable and start:**
```bash
# Reload systemd user daemon
systemctl --user daemon-reload

# Enable service to start on boot
systemctl --user enable task-messenger-manager.service

# Start service now
systemctl --user start task-messenger-manager.service

# Check status
systemctl --user status task-messenger-manager.service

# View logs
journalctl --user -u task-messenger-manager -f
```

**Enable user services to run without login:**
```bash
sudo loginctl enable-linger $USER
```

This ensures the manager starts automatically on VM boot, even without SSH login.

## Verification Steps

1. **Check service status:**
   ```bash
   systemctl --user status task-messenger-manager
   ```

2. **View logs (local):**
   ```bash
   journalctl --user -u task-messenger-manager -f
   ```

3. **View logs (web):**
   - Go to `console.cloud.google.com/logs`
   - Filter: `resource.type="gce_instance" AND jsonPayload.SYSLOG_IDENTIFIER="manager"`

4. **Verify ZeroTier:**
   ```bash
   # Check manager's ZeroTier IP
   ip addr show zt0
   ```

5. **Test worker connection:**
   - Configure worker with manager's ZeroTier IP
   - Start worker: `./worker -c config-worker.json --mode async`
   - Check manager logs for "New connection" message

## Monitoring

**GCP Cloud Logging:**
- Real-time: `console.cloud.google.com/logs/query`
- Set up alerts: "Email me if no refill events in 10 minutes"
- Export to Cloud Storage (optional): For long-term archival

**Expected log events:**
- Manager startup: "Async Transport Server started successfully"
- Auto-refill: "Pool low (7 tasks), generating 100 more"
- Worker connections: "New connection from worker"
- Worker disconnections: "Worker disconnected"
- Errors: Any exceptions or failures

**Health checks:**
- Local logs still available via SSH: `journalctl --user -u task-messenger-manager`
- ZeroTier connectivity: Workers should connect within 5-10 seconds
- Task pool: Monitor refill frequency (should decrease as workers complete tasks faster)

## Cost Estimate

**Free tier components:**
- e2-micro VM: Free (1 instance in us-west1, us-central1, or us-east1)
- 30 GB persistent disk: Free
- Cloud Logging: Free (first 50 GB/month)
- Egress: 1 GB/month free (should be sufficient for ZeroTier + task traffic)

**Expected cost:** $0/month if staying within free tier limits

## Troubleshooting

**Service won't start:**
```bash
journalctl --user -u task-messenger-manager -n 50
# Check for libzt.so loading errors or config file issues
```

**ZeroTier not connecting:**
```bash
# Verify outbound firewall allows UDP 9993 and HTTPS
curl -I https://my.zerotier.com
```

**Workers can't connect:**
- Verify manager's ZeroTier IP: `ip addr show zt0`
- Check manager is authorized in ZeroTier Central
- Verify manager listening on port 8080: `ss -tlnp | grep 8080`
- Test connectivity from worker's ZeroTier IP: `nc -zv <manager-zt-ip> 8080`

## Future Enhancements

1. **Metrics dashboard**: Add `/stats` HTTP endpoint for real-time pool/worker stats
2. **Task persistence**: Store task queue to disk for recovery after restarts
3. **Dynamic refill**: Adjust batch size based on worker connection count
4. **Multi-region**: Deploy multiple managers in different GCP regions
5. **Load balancing**: Use ZeroTier DNS or round-robin for worker distribution

## References

- Manager source: [manager/managerMain.cpp](../../manager/managerMain.cpp)
- Task pool: [message/TaskMessagePool.cpp](../../message/TaskMessagePool.cpp)
- Installation script: [extras/scripts/install_linux.sh](../../extras/scripts/install_linux.sh)
- Distribution builder: [extras/scripts/build_distribution.sh](../../extras/scripts/build_distribution.sh)
- GCP Free Tier: https://cloud.google.com/free
- ZeroTier Central: https://my.zerotier.com
