@echo off
REM TaskMessenger Dispatcher Launcher Script for Windows
REM This script checks for required configuration and launches the dispatcher

setlocal enabledelayedexpansion

REM Configuration
set "CONFIG_DIR=%APPDATA%\task-messenger"
set "CONFIG_FILE=%CONFIG_DIR%\config-dispatcher.json"
set "DEFAULT_INSTALL_DIR=%LOCALAPPDATA%\TaskMessenger"

REM Get the directory of this script
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

REM Determine dispatcher executable path
if exist "%SCRIPT_DIR%\..\dispatcher\tm-dispatcher.exe" (
    set "DISPATCHER_BIN=%SCRIPT_DIR%\..\dispatcher\tm-dispatcher.exe"
) else if exist "%DEFAULT_INSTALL_DIR%\tm-dispatcher\tm-dispatcher.exe" (
    set "DISPATCHER_BIN=%DEFAULT_INSTALL_DIR%\tm-dispatcher\tm-dispatcher.exe"
) else (
    where tm-dispatcher.exe >nul 2>&1
    if !errorlevel! equ 0 (
        set "DISPATCHER_BIN=tm-dispatcher.exe"
    ) else (
        echo [ERROR] Could not find dispatcher executable >&2
        exit /b 1
    )
)

REM Check if config file exists
if not exist "%CONFIG_FILE%" (
    echo [WARNING] Configuration file not found: %CONFIG_FILE% >&2
    echo Please create a configuration file or run the dispatcher with appropriate arguments. >&2
    echo. >&2
    echo You can create a template config by running the installation script, >&2
    echo or create it manually with the following structure: >&2
    echo. >&2
    echo { >&2
    echo   "network": { >&2
    echo     "zerotier_network_id": "your_network_id", >&2
    echo     "zerotier_identity_path": "" >&2
    echo   }, >&2
    echo   "logging": { >&2
    echo     "level": "info", >&2
    echo     "file": "" >&2
    echo   } >&2
    echo } >&2
    echo. >&2
)

REM Launch dispatcher with all passed arguments
"%DISPATCHER_BIN%" %*
