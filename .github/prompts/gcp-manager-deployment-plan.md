# GCP Manager Deployment Plan

Deploy the task-messenger manager as a continuously-running service on a GCP e2-micro VM (free tier), using ZeroTier for secure worker connectivity, with automatic task generation (refill when pool < 10 tasks, generate 100 tasks per batch), graceful shutdown handling, minimal logging (errors, refill events, worker connections only), and GCP Cloud Logging for web-based monitoring.

## Deployment Steps

### 1. Modify Manager for Production Mode

Replace the interactive loop in [managerMain.cpp](../../manager/managerMain.cpp) (lines 41-56) with:

- **Monitoring thread** that polls `server.get_task_pool_stats()` every 1-2 seconds
- **Auto-refill logic**: Generate 100 tasks via `DefaultTaskGenerator::make_tasks(100)` when pool size < 10
- **Logging**: Log refill events (e.g., "Pool low (7 tasks), generating 100 more")
- **Signal handlers**: Add SIGTERM/SIGINT handlers to stop the monitoring thread and call `server.stop()` for clean shutdown

**Requirements:**
- Keep logging minimal: errors, refill events, worker connect/disconnect only
- Initial startup: Generate 100 tasks immediately (acceptable baseline after restart)
- Thread-safe shutdown flag checked by monitoring loop

### 2. Set Up GCP e2-micro VM

**Instance Configuration:**
- **Machine type**: e2-micro (2 vCPUs shared, 1 GB RAM)
- **Region**: US free-tier eligible (us-west1, us-central1, or us-east1)
- **OS**: Ubuntu 24.04 LTS
- **Boot disk**: 30 GB standard persistent disk (free tier)

**Firewall Rules:**
- Outbound HTTPS (443): Allow - for ZeroTier control plane
- Outbound UDP 9993: Allow - for ZeroTier traffic
- Inbound port 8080: NOT needed (manager listens on ZeroTier network only)

### 3. Install Cloud Logging Agent

On the VM, install Google Cloud Logging agent to forward journald logs to GCP:

```bash
curl -sSO https://dl.google.com/cloudagents/add-logging-agent-repo.sh
sudo bash add-logging-agent-repo.sh --also-install
sudo systemctl enable --now google-fluentd
```

**Access logs:**
- Web: `console.cloud.google.com/logs`
- Query filter: `resource.type="gce_instance" AND jsonPayload.SYSLOG_IDENTIFIER="manager"`
- Local: `journalctl --user -u task-messenger-manager -f` (still works via SSH)

**Free tier:** 50 GB logs/month

### 4. Build Manager Distribution

On local development machine:

```bash
cd ~/projects/task-messenger
./extras/scripts/build_distribution.sh
```

**Output:**
- `task-messenger-manager-v{VERSION}-linux-x86_64.tar.gz`
- `task-messenger-manager-v{VERSION}-linux-x86_64.run` (self-extracting)

**Upload to VM:**
```bash
gcloud compute scp task-messenger-manager-*.run your-vm-name:~/ --zone=us-west1-a
```

### 5. Install and Configure Manager

On the VM:

```bash
# Run self-extracting installer (or use install_linux.sh)
chmod +x task-messenger-manager-*.run
./task-messenger-manager-*.run

# Or extract and install manually
tar -xzf task-messenger-manager-*.tar.gz
cd task-messenger-manager-*/
./scripts/install_linux.sh
```

**Installation paths (user install):**
- Binaries: `~/.local/share/task-messenger-manager/bin/manager`
- Libraries: `~/.local/share/task-messenger-manager/lib/libzt.so`
- Config: `~/.config/task-message-manager/config-manager.json`
- Identity: `~/.config/task-message-manager/vn-manager-identity/`

**Configure ZeroTier network:**

Edit `~/.config/task-message-manager/config-manager.json`:
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

### 6. Join ZeroTier Network

**Option A: Use existing identity (recommended)**

Copy your existing manager identity to the VM:

```bash
# On local machine
gcloud compute scp --recurse config/vn-manager-identity/ your-vm-name:~/.config/task-message-manager/ --zone=us-west1-a
```

**Option B: Generate new identity**

The manager will auto-generate identity files on first run at:
`~/.config/task-message-manager/vn-manager-identity/`

**Authorize in ZeroTier Central:**
1. Start manager (generates node ID)
2. Log in to my.zerotier.com
3. Go to your network
4. Find the new node ID in "Members"
5. Check "Authorized"
6. Note the assigned ZeroTier IP (e.g., 10.147.x.x)

Workers will connect to this ZeroTier IP on port 8080.

### 7. Create Systemd User Service

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
WorkingDirectory=%h/.local/share/task-messenger-manager
Environment="LD_LIBRARY_PATH=%h/.local/share/task-messenger-manager/lib"
ExecStart=%h/.local/share/task-messenger-manager/bin/manager -c %h/.config/task-message-manager/config-manager.json
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
