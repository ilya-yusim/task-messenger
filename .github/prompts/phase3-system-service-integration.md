# Phase 3: System Service Integration

## Context
This plan assumes Phase 2 (distribution packaging) is complete and working. The project has distributable archives for manager and worker components on both Windows and Linux. Phase 3 focuses on enabling these components to run as persistent background services.

## Plan: Implement System Service Integration

Implement system service configurations for manager and worker components to run as persistent background services on Linux (systemd) and Windows (using NSSM), including installation scripts, service management utilities, and logging configuration for production deployments.

### Steps

1. **Create systemd unit files** by adding `extras/systemd/task-messenger-manager.service` and `extras/systemd/task-messenger-worker.service` with proper service type (simple/forking based on runtime behavior), working directory set to installation path, restart policies (on-failure), user/group configuration for non-root execution, and dependencies on network-online.target for ZeroTier connectivity

2. **Implement systemd installation script** at `extras/scripts/install_service_linux.sh` that accepts component argument (manager/worker), copies unit files to `/etc/systemd/system/`, runs `systemctl daemon-reload`, enables service with `systemctl enable`, and provides instructions for starting/checking status without auto-starting

3. **Create Windows service wrapper configuration** at `extras/windows-service/` directory with NSSM configuration files for manager and worker including executable paths, working directory, log file locations (stdout/stderr redirection), startup type (automatic/manual), and service recovery options (restart on failure with delays)

4. **Implement Windows service installation script** at `extras/scripts/install_service_windows.ps1` that downloads/verifies NSSM if not present, installs service using NSSM with proper configuration, sets service description and display name, configures log rotation settings, and provides instructions without auto-starting

5. **Add service management utilities** by creating `extras/scripts/service_control.sh` (Linux) and `extras/scripts/service_control.ps1` (Windows) that provide unified commands (start/stop/restart/status/logs) wrapping systemctl and NSSM/sc.exe respectively, with colored output and clear status reporting

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
