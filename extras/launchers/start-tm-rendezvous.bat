@echo off
REM TaskMessenger Rendezvous Launcher Script for Windows

setlocal enabledelayedexpansion

REM Configuration
set "CONFIG_DIR=%APPDATA%\task-messenger"
set "CONFIG_FILE=%CONFIG_DIR%\config-rendezvous.json"
set "DEFAULT_INSTALL_DIR=%LOCALAPPDATA%\TaskMessenger"

REM Get the directory of this script
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

REM Determine rendezvous executable path
if exist "%SCRIPT_DIR%\..\rendezvous\tm-rendezvous.exe" (
    set "RENDEZVOUS_BIN=%SCRIPT_DIR%\..\rendezvous\tm-rendezvous.exe"
) else if exist "%DEFAULT_INSTALL_DIR%\tm-rendezvous\tm-rendezvous.exe" (
    set "RENDEZVOUS_BIN=%DEFAULT_INSTALL_DIR%\tm-rendezvous\tm-rendezvous.exe"
) else (
    where tm-rendezvous.exe >nul 2>&1
    if !errorlevel! equ 0 (
        set "RENDEZVOUS_BIN=tm-rendezvous.exe"
    ) else (
        echo [ERROR] Could not find tm-rendezvous executable >&2
        exit /b 1
    )
)

REM Check if config file exists
if not exist "%CONFIG_FILE%" (
    echo [WARNING] Configuration file not found: %CONFIG_FILE% >&2
    echo Run the installer to deploy the default rendezvous config, or pass -c ^<path^>. >&2
)

REM Launch rendezvous with all passed arguments
"%RENDEZVOUS_BIN%" %*
