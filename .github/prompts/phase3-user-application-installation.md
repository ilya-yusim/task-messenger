# Phase 3: User Application Installation

## Context
This plan assumes Phase 2 (distribution packaging) is complete and working. The project has distributable archives for manager and worker components on both Windows and Linux. Phase 3 focuses on providing easy installation for users to run these as regular applications (not system services).

## Plan: Implement User Application Installation

Create installation scripts that extract distribution archives, set up application directories, create desktop shortcuts/menu entries, and provide easy start/stop mechanisms for users to run manager and worker as regular applications.

### Steps

1. **Create Linux installation script** at `extras/scripts/install_linux.sh` that accepts component argument (manager/worker/all) and optional installation directory, prompts user for installation path with default `~/.local/share/task-messenger`, detects existing installation and offers upgrade (preserving config files), extracts tar.gz archive to chosen directory, creates `~/.local/bin` symlinks for executables (manager/worker), adds `~/.local/bin` to PATH if not present (via `.bashrc`/`.profile`), and copies config templates to `~/.config/task-messenger/` only if they don't exist

2. **Create desktop entry files** at `extras/desktop/task-messenger-manager.desktop` and `extras/desktop/task-messenger-worker.desktop` following freedesktop.org specification with Name, Comment, Exec pointing to installed location (dynamically updated by install script), using generic system icons, Terminal=false for manager and Terminal=true for worker (interactive UI), and Categories (Network;System)

3. **Implement desktop integration** by modifying Linux install script to copy `.desktop` files to `~/.local/share/applications/` with paths updated to actual installation directory, run `update-desktop-database ~/.local/share/applications/` if available, and optionally create desktop shortcuts in `~/Desktop/` with user confirmation

4. **Create Windows installation script** at `extras/scripts/install_windows.ps1` that accepts component argument (manager/worker/all) and optional installation directory, prompts user for installation path with default `C:\Users\<username>\AppData\Local\TaskMessenger`, detects existing installation and offers upgrade (preserving config files), extracts ZIP archive to chosen directory, adds installation directory to user PATH environment variable, and copies config templates to `%APPDATA%\task-messenger\` only if they don't exist

5. **Implement Windows shortcuts** by having install script create Start Menu shortcuts in `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Task Messenger\` using generic Windows icons, target paths pointing to actual installation, working directories set correctly, and optionally create Desktop shortcuts with user confirmation

6. **Create launcher scripts** at `extras/launchers/` directory with `start-manager.sh`/`start-manager.bat` and `start-worker.sh`/`start-worker.bat` that change to installation directory, check for config files (display helpful message if missing), set required environment variables if needed, and launch the executable with proper arguments

7. **Add uninstallation scripts** at `extras/scripts/uninstall_linux.sh` and `extras/scripts/uninstall_windows.ps1` that detect installation directory (from saved install location or prompt user), remove installed files from application directory, remove symlinks/PATH entries, delete desktop entries and shortcuts, prompt before removing config files (keep user data by default), and provide complete cleanup option

8. **Update distribution packages** by modifying `build_distribution.sh` and `build_distribution.ps1` to include installation scripts in archive root, add launcher scripts to `scripts/` directory, include desktop files in `share/` directory, and add INSTALL.txt with quick start instructions

9. **Create installation documentation** at `docs/INSTALLATION.md` with platform-specific sections covering extraction steps, running installation script with examples (including custom paths and upgrade scenarios), configuration file setup (editing templates), launching applications (via shortcuts or command line), troubleshooting common issues, and uninstallation procedures

10. **Test installation workflow** on clean user accounts (non-admin) for both platforms, verify extraction and installation script execution with both default and custom paths, test upgrade scenario (install v1.0.0, then upgrade to v1.0.1 preserving config), test desktop shortcuts and menu entries, confirm applications launch correctly and find config files, validate PATH integration works, and test uninstallation leaves no artifacts (except preserved configs)

### Design Decisions

- ✅ **Installation location**: Allow users to choose installation directory or accept defaults (`~/.local/share/task-messenger` on Linux, `%LOCALAPPDATA%\TaskMessenger` on Windows)
- ✅ **Configuration management**: Just copy templates - no interactive config prompts
- ✅ **Auto-start on login**: Keep purely manual start - no automatic startup mechanisms
- ✅ **Update mechanism**: Detect existing installation and offer upgrade path preserving configs
- ✅ **Icon/branding**: Use generic system icons initially
- ✅ **Portable vs installed mode**: No portable mode needed - standard installation only

### Further Considerations

1. **Upgrade detection**: Store installation metadata (version, path) in `~/.config/task-messenger/.install-info` (Linux) or registry/file (Windows) to enable upgrade detection?

2. **Config migration**: If config file format changes between versions, should upgrade script attempt migration or just preserve old configs and document manual changes?

3. **Multi-user installations**: Currently designed for single-user installs - document that each user needs separate installation, or support optional system-wide install to `/opt` (Linux) or `C:\Program Files` (Windows)?

## Prerequisites

Before starting Phase 3:
- Phase 2 must be complete (distribution packages working)
- Manager and worker executables must run correctly from command line
- Configuration files must be functional
- Distribution archives must be tested and validated

## Expected Deliverables

- Linux installation script (`install_linux.sh`) with upgrade detection
- Windows installation script (`install_windows.ps1`) with upgrade detection
- Desktop entry files for Linux (`.desktop` files)
- Windows shortcuts configuration
- Launcher scripts for both platforms
- Uninstallation scripts
- Installation documentation (`INSTALLATION.md`)
- Updated distribution packages including install scripts
- Testing validation on both platforms including upgrade scenarios

## Note on System Services

This phase focuses on user application installation with manual start/stop. For production deployments requiring system services (automatic startup, running as daemon, etc.), see the separate system service integration plan at [system-service-integration-future.md](system-service-integration-future.md).

6. **Update distribution packages** by modifying packaging scripts to optionally include service files in `share/systemd/` (Linux) or `share/windows-service/` (Windows) directories within archives, adding service installation documentation to README files, and creating separate service installation guides

7. **Implement logging configuration** by adding log rotation config for Linux (`/etc/logrotate.d/task-messenger-{component}`), configuring systemd journal settings in unit files, setting up Windows Event Log integration or file-based logging with rotation, and documenting log file locations and troubleshooting procedures

8. **Test service deployments** by installing services on clean test VMs for both platforms, verifying automatic startup after reboot, testing restart on failure behavior, confirming proper permissions and security context, validating log output and rotation, and ensuring clean uninstallation process

### Further Considerations

1. **Service user/group management**: Should services run as dedicated `task-messenger` user/group on Linux for security isolation, or allow configuration to run as any user? Windows services typically run as NetworkService or LocalSystem - which is appropriate?

2. **Configuration file location**: Services need to locate config files - should we use environment variables in unit files (`Environment=CONFIG_FILE=/etc/task-messenger/config-manager.json`) or rely on relative paths from working directory?

3. **Security hardening**: Consider adding systemd hardening options like `ProtectSystem=strict`, `ProtectHome=true`, `NoNewPrivileges=true`, or keep minimal for flexibility? Windows equivalent could use service-specific permissions.

4. **Multiple instances**: Should the service setup support running multiple worker instances on the same machine (e.g., `task-messenger-worker@1.service` template), or assume single instance per component?

5. **Auto-update capability**: Include mechanism for services to detect and apply updates gracefully (restart with new binary), or handle updates manually with service stop/update/start cycle?

6. **Monitoring integration**: Add support for systemd notify (`Type=notify`) to signal when service is ready, or provide health check endpoints that monitoring systems like Prometheus can scrape?

## Prerequisites

Before starting Phase 3:
- Phase 2 must be complete (distribution packages working)
- Manager and worker executables must run correctly from command line
- Configuration files must be functional
- Logging infrastructure should be in place (or will be added in this phase)

## Expected Deliverables

- Systemd unit files for manager and worker
- Windows service configuration for NSSM
- Installation scripts for both platforms
- Service management utilities
- Logging and log rotation configuration
- Documentation for service deployment
- Uninstallation scripts
- Testing validation on both platforms
