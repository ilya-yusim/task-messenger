# Hotspot + VNC in Codespaces

This guide explains how to start the headless Hotspot GUI in the Codespace and connect to it from your laptop using a secure SSH tunnel via the GitHub CLI (recommended). It also covers starting/stopping the VNC stack, creating a VNC password, and troubleshooting.

## Files
- `scripts/start_hotspot_vnc.sh` — management script (start|stop|status) for Xvfb + hotspot + x11vnc. PID files and logs live under `/tmp/hotspot_vnc`.

## Quick summary
1. Start the VNC stack in the Codespace:

```bash
./scripts/start_hotspot_vnc.sh start
```

2. On your laptop, create an SSH tunnel that forwards your local port 5901 to the Codespace's 5901 using the GitHub CLI (replace `<CODESPACE>` with your codespace name):

```powershell
gh codespace ssh --codespace <CODESPACE> -- -L 5901:localhost:5901
```

3. In RealVNC Viewer on your laptop, connect to `localhost:5901` and enter the VNC password if prompted.

---

## Detailed steps

### 1) Start the VNC stack in the Codespace

Open a terminal inside the Codespace and run:

```bash
# start Xvfb + hotspot + x11vnc
./scripts/start_hotspot_vnc.sh start

# check status
./scripts/start_hotspot_vnc.sh status
```

Logs and pid files are written to `/tmp/hotspot_vnc`:

- `/tmp/hotspot_vnc/xvfb.log`
- `/tmp/hotspot_vnc/hotspot.log`
- `/tmp/hotspot_vnc/x11vnc.log`
- PID files: `/tmp/hotspot_vnc/*.pid`

If the script errors about an existing display (e.g. "Server is already active for display 99"), see the Troubleshooting section below.

### 2) Secure the VNC server (highly recommended)

Create a VNC password file and restart the stack with it:

```bash
# create password file inside the codespace
x11vnc -storepasswd <your_password> /tmp/vncpass
chmod 600 /tmp/vncpass

# restart the stack using the password file
./scripts/start_hotspot_vnc.sh stop || true
./scripts/start_hotspot_vnc.sh start --password /tmp/vncpass
```

When a password is configured RealVNC will prompt for it when you connect.

### 3) Create an SSH tunnel from your laptop (recommended)

Use the GitHub CLI to connect to your Codespace and open an SSH tunnel. Replace the codespace name with the one from `gh codespace list`.

PowerShell (Windows):

```powershell
gh codespace ssh --codespace probable-acorn-x59w5wgv7rwgfv94q -- -L 5901:localhost:5901
```

Keep that terminal open while you use RealVNC. Then in RealVNC Viewer connect to:

```
localhost:5901
```

If `localhost:5901` is taken locally, forward a different local port:

```powershell
gh codespace ssh --codespace <CODESPACE> -- -L 5902:localhost:5901
# then connect RealVNC to localhost:5902
```

### 4) PuTTY users

If you prefer PuTTY instead of the GitHub CLI:

1. Open PuTTY and set the Host Name to the Codespace host (see `gh codespace ssh --show-connection-string` to obtain the host and port).
2. In PuTTY, go to Connection → SSH → Tunnels.
   - Source port: `5901`
   - Destination: `localhost:5901`
   - Click Add.
3. Open the PuTTY session; while it is open, connect RealVNC to `localhost:5901`.

### 5) Codespaces port forwarding (browser preview)

You can also forward port 5901 using the Codespaces "Ports" panel. Beware: the preview URL is an HTTP(S) proxy for browsers and does not provide a raw TCP socket suitable for native RealVNC.

If you want to use the Codespaces preview URL, run a websocket/VNC web bridge (noVNC) inside the Codespace and forward the web port (see Optional: Browser-based access below).

### Optional: Browser-based access with noVNC (if you can't tunnel SSH)

This exposes a browser-based VNC client (noVNC) through an HTTP port that Codespaces preview can proxy.

```bash
# install websockify / noVNC if needed
sudo apt-get install -y python3-websockify
git clone https://github.com/novnc/noVNC.git /tmp/noVNC

# run websockify and serve the noVNC UI on port 6080
websockify --web=/tmp/noVNC 6080 localhost:5901
```

Forward port 6080 in Codespaces and open the preview URL in your browser. Use the noVNC web UI to connect to the VNC session.

### 6) Stopping the stack

```bash
./scripts/start_hotspot_vnc.sh stop
```

### 7) Troubleshooting

- Timeout when connecting from RealVNC:
  - If you used the browser preview URL (https://...-5901.app.github.dev), that is an HTTP proxy and won't work with RealVNC. Use an SSH tunnel or noVNC instead.
  - Ensure the SSH tunnel is active and the local port bound: on Windows `netstat -ano | findstr 5901`.
  - Check Codespaces Ports panel shows the forwarded port if using that method.

- Blank screen or no response:
  - Confirm `x11vnc` is listening: `ss -ltnp | grep 5901` inside the Codespace.
  - Confirm hotspot is running and bound to the same display as x11vnc: `ps aux | egrep 'Xvfb|hotspot|x11vnc'`.
  - Check logs: `/tmp/hotspot_vnc/x11vnc.log`, `/tmp/hotspot_vnc/hotspot.log`, `/tmp/hotspot_vnc/xvfb.log`.

- "Server is already active for display 99":
  - If a process truly owns the display, stop it first. If the lock is stale and no process owns it, remove `/tmp/.X99-lock` and `/tmp/.X11-unix/X99` then start the stack again.

## Example workflow (full)

1. In Codespace terminal:
```bash
./scripts/start_hotspot_vnc.sh start
```
2. On your laptop (PowerShell):
```powershell
gh codespace ssh --codespace probable-acorn-x59w5wgv7rwgfv94q -- -L 5901:localhost:5901
```
3. In RealVNC: connect to `localhost:5901` and enter the password if prompted.

---

If you want, I can:
- Add a VS Code task that runs the `gh codespace ssh ... -L ...` tunnel command and opens a terminal for you to keep it running.
- Or start the noVNC bridge and forward port 6080 now so you can use the browser-based UI.

## Installing required tools (Xvfb, x11vnc, Hotspot)

If your Codespace doesn't have the required tools installed, use the helper script `scripts/install_hotspot_deps.sh` to install them. The script targets Debian/Ubuntu (apt) and will:

- Install `x11vnc`, `xvfb` and common utilities (`curl`, `wget`, `ca-certificates`).
- Try to install a `hotspot` package via apt; if unavailable it downloads the latest KDAB Hotspot release (AppImage) and installs it to `/opt/hotspot` with a wrapper at `/usr/local/bin/hotspot`.

Usage (interactive):

```bash
./scripts/install_hotspot_deps.sh
```

Usage (non-interactive / automated):

```bash
sudo ./scripts/install_hotspot_deps.sh --yes
```

Notes
- The script requires sudo/root for package installs and to write into `/opt` and `/usr/local/bin`.
- After installation, start the VNC stack with:

```bash
./scripts/start_hotspot_vnc.sh start
./scripts/start_hotspot_vnc.sh status
```

*** End of document
