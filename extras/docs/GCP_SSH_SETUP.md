# Connecting to a GCP Compute Engine VM via `gcloud compute ssh` on Windows

This document captures the steps that were needed to get `gcloud compute ssh`
working from PowerShell on Windows, and the debugging tools that helped pinpoint
each problem.

## Symptom

```powershell
gcloud compute ssh tm-manager-prod --zone=us-west1-a --project=task-messenger-prod
```

Printed:

```
External IP address was not found; defaulting to using IAP tunneling.
```

Then a PuTTY window opened and just hung — no prompt, no error.

## Root cause

The VM was **stopped**. PuTTY was waiting on a TCP connection to port 22 that
could never establish. The IAP fallback message was a red herring (it only
happens when the VM has no external IP, which a stopped VM doesn't).

Once the VM was started, SSH worked with no other changes.

## Key steps that fixed it

1. **Authenticate gcloud** (the active account had been lost):
   ```powershell
   gcloud auth login
   ```

2. **Set the default project** so it doesn't have to be passed every time:
   ```powershell
   gcloud config set project task-messenger-prod
   ```

3. **Start the VM** (this was the actual fix):
   ```powershell
   gcloud compute instances start tm-manager-prod --zone=us-west1-a
   ```

4. **SSH normally**:
   ```powershell
   gcloud compute ssh tm-manager-prod --zone=us-west1-a
   ```

## Useful debugging steps along the way

### Check the VM status before anything else

```powershell
gcloud compute instances describe tm-manager-prod --zone=us-west1-a --format="value(status)"
```

If this returns `TERMINATED` / `STOPPED`, that alone explains a hung SSH.

### Test IAP independently of SSH

This isolates IAP/firewall/IAM problems from SSH/PuTTY problems:

```powershell
gcloud compute start-iap-tunnel tm-manager-prod 22 --local-host-port=localhost:2222 --zone=us-west1-a
```

- `Listening on port [2222]` → IAP works; any remaining hang is SSH-side.
- `4003: 'failed to connect to backend'` → the VM isn't accepting on port 22
  (stopped, no firewall rule, or sshd not running).

### Verbose gcloud output

```powershell
gcloud compute ssh tm-manager-prod --zone=us-west1-a --tunnel-through-iap --verbosity=debug
```

The `Running command [...]` line shows exactly which SSH client and proxy
command gcloud invokes. Useful to confirm whether PuTTY or OpenSSH is being
used, and which Python is running the IAP proxy.

### Check IAP firewall rule (if `start-iap-tunnel` fails with 4003)

```powershell
gcloud compute firewall-rules list --filter="(sourceRanges:35.235.240.0/20) AND (allowed.ports:22 OR allowed.IPProtocol:tcp)" --format="table(name,sourceRanges,allowed)"
```

Create one if missing:

```powershell
gcloud compute firewall-rules create allow-iap-ssh `
  --direction=INGRESS `
  --action=ALLOW `
  --rules=tcp:22 `
  --source-ranges=35.235.240.0/20
```

### Check VM serial output (if SSH connects but the OS is broken)

```powershell
gcloud compute instances get-serial-port-output tm-manager-prod --zone=us-west1-a |
    Select-String -Pattern "sshd|Failed|error" -Context 0,1 |
    Select-Object -Last 30
```

## Windows-specific gotchas worth knowing

These were investigated but turned out **not** to be the cause this time. They
are still common pitfalls on Windows:

### Microsoft Store Python breaks gcloud's IAP proxy

`gcloud compute ssh --tunnel-through-iap` invokes Python as a subprocess to run
the IAP tunnel. If `where.exe python` returns:

```
C:\Users\<user>\AppData\Local\Microsoft\WindowsApps\python.exe
```

that's the **Store stub**, which runs in an app sandbox and can silently fail
when invoked by another process. Fix:

1. Install Python from <https://www.python.org/downloads/windows/> with
   "Add python.exe to PATH" checked.
2. Optionally disable the Store aliases: Settings → Apps → Advanced app
   settings → App execution aliases → turn off `python.exe` and `python3.exe`.
3. Optionally point gcloud explicitly at the real interpreter:
   ```powershell
   [Environment]::SetEnvironmentVariable(
       "CLOUDSDK_PYTHON",
       "C:\Users\iyusi\AppData\Local\Programs\Python\Python313\python.exe",
       "User")
   ```

### Bundled PuTTY hides host-key prompts

On first connect, PuTTY displays a "trust this host key?" dialog. If it opens
hidden behind another window or off-screen, the SSH session looks hung.

To force gcloud to use OpenSSH (already present at
`C:\Windows\System32\OpenSSH\ssh.exe`), rename the bundled PuTTY binaries:

```powershell
Rename-Item "C:\Users\iyusi\AppData\Local\Google\Cloud SDK\google-cloud-sdk\bin\sdk\putty.exe" "putty.exe.bak"
Rename-Item "C:\Users\iyusi\AppData\Local\Google\Cloud SDK\google-cloud-sdk\bin\sdk\plink.exe" "plink.exe.bak"
```

OpenSSH then prompts for the host-key fingerprint inline in PowerShell.

Note: `gcloud config set ssh/putty_force false` does **not** exist on current
gcloud versions (returns `Section [ssh] has no property [putty_force]`). The
env var `CLOUDSDK_COMPUTE_SSH_USE_PUTTY` is similarly unreliable. Renaming the
binaries is the dependable workaround.

## Cleanup

To avoid charges when done:

```powershell
gcloud compute instances stop tm-manager-prod --zone=us-west1-a
```
