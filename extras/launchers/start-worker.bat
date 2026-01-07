@echo off
REM TaskMessenger Worker Launcher Script for Windows
REM This script checks for required configuration and launches the worker

setlocal enabledelayedexpansion

REM Configuration
set "CONFIG_DIR=%APPDATA%\task-messenger"
set "CONFIG_FILE=%CONFIG_DIR%\config-worker.json"
set "DEFAULT_INSTALL_DIR=%LOCALAPPDATA%\TaskMessenger"

REM Get the directory of this script
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

REM Determine worker executable path
if exist "%SCRIPT_DIR%\..\worker\worker.exe" (
    set "WORKER_BIN=%SCRIPT_DIR%\..\worker\worker.exe"
) else if exist "%DEFAULT_INSTALL_DIR%\worker\worker.exe" (
    set "WORKER_BIN=%DEFAULT_INSTALL_DIR%\worker\worker.exe"
) else (
    where worker.exe >nul 2>&1
    if !errorlevel! equ 0 (
        set "WORKER_BIN=worker.exe"
    ) else (
        echo [ERROR] Could not find worker executable >&2
        exit /b 1
    )
)

REM Check if config file exists
if not exist "%CONFIG_FILE%" (
    echo [WARNING] Configuration file not found: %CONFIG_FILE% >&2
    echo Please create a configuration file or run the worker with appropriate arguments. >&2
    echo. >&2
    echo You can create a template config by running the installation script, >&2
    echo or create it manually with the following structure: >&2
    echo. >&2
    echo { >&2
    echo   "network": { >&2
    echo     "zerotier_network_id": "your_network_id", >&2
    echo     "zezerotier_identity_path": "" >&2
    echo   }, >&2
    echo   "logging": { >&2
    echo     "level": "info", >&2
    echo     "file": "" >&2
    echo   } >&2
    echo } >&2
    echo. >&2
)

REM Launch worker with all passed arguments
"%WORKER_BIN%" %*
